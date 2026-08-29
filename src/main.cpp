#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"
#include "log.h"
#include "photo_store.h"
#include "ui.h"

// 按鍵（active-low，docs/device-research.md）
#define BTN_UP    6
#define BTN_DOWN  4
#define BTN_PRESS 5

#define WAIT_RELEASE_MS 2000
#define SLEEP_US_RETRY  (5ULL * 60ULL * 1000000ULL)

// 輪播間隔（秒）；與 ui.cpp MENU_OPTIONS 順序對應
static const uint32_t SLIDE_OPTIONS_SEC[] = {0, 60, 300, 900, 1800};
static const int SLIDE_OPTIONS_COUNT = 5;
static const uint32_t SLIDE_DEFAULT_SEC = 0;   // 預設 OFF

static esp_sleep_wakeup_cause_t g_wake;
static uint64_t g_wakeMask = 0;
static bool stuckGuard = false;

// ---------- NVS ----------
static int loadIdx() {
  Preferences prefs;
  prefs.begin("photo", true);
  int v = prefs.getInt("idx", 0);
  prefs.end();
  return v;
}

static void saveIdx(int v) {
  Preferences prefs;
  prefs.begin("photo", false);
  prefs.putInt("idx", v);
  prefs.end();
}

static int loadSlideIdx() {
  Preferences prefs;
  prefs.begin("photo", true);
  uint32_t sec = prefs.getUInt("slide", SLIDE_DEFAULT_SEC);
  prefs.end();
  for (int i = 0; i < SLIDE_OPTIONS_COUNT; i++) {
    if (SLIDE_OPTIONS_SEC[i] == sec) return i;
  }
  return 0;
}

static void saveSlideIdx(int v) {
  Preferences prefs;
  prefs.begin("photo", false);
  prefs.putUInt("slide", SLIDE_OPTIONS_SEC[v]);
  prefs.end();
}

// 選單互動：回傳確認的游標；PRESS 或 idle 20s 皆保存（語意相同）
static int menuLoop(int cur) {
  uiMenuScreen(cur);
  uint32_t lastAct = millis();
  uint32_t debounceAt = 0;
  bool last[2] = {true, true};   // UP, DOWN
  while (true) {
    uint32_t now = millis();
    if (now - lastAct > 20000) {
      LOGF("menu idle, save cursor %d\n", cur);
      return cur;
    }
    if (now - debounceAt < 30) { delay(5); yield(); continue; }
    debounceAt = now;
    if (!digitalRead(BTN_PRESS)) {
      LOGF("menu confirm %d\n", cur);
      return cur;
    }
    for (int i = 0; i < 2; i++) {
      uint8_t pin = (i == 0) ? BTN_UP : BTN_DOWN;
      bool released = digitalRead(pin);
      if (released != last[i]) {
        last[i] = released;
        if (!released) {
          lastAct = millis();
          cur = (i == 0) ? (cur + SLIDE_OPTIONS_COUNT - 1) % SLIDE_OPTIONS_COUNT   // UP=往上
                         : (cur + 1) % SLIDE_OPTIONS_COUNT;                        // DOWN=往下
          uiMenuScreen(cur);   // 整頁重繪（partial 在此面板有對齊問題，改 full refresh）
          LOGF("menu cursor %d (%lu s)\n", cur,
               (unsigned long)SLIDE_OPTIONS_SEC[cur]);
        }
      }
    }
    delay(5);
    yield();
  }
}

// ---------- 喚醒與睡眠 ----------
static bool waitButtonsReleased(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (digitalRead(BTN_UP) && digitalRead(BTN_DOWN) && digitalRead(BTN_PRESS)) {
      return true;
    }
    delay(10);
    yield();
  }
  LOGF("[warn] buttons still held after %lu ms\n", (unsigned long)timeoutMs);
  return false;
}

static bool anyWakePinLow() {
  return !digitalRead(BTN_UP) || !digitalRead(BTN_DOWN) || !digitalRead(BTN_PRESS);
}

static void goToDeepSleep(uint64_t timerUs, bool enableExt1) {
  uiHibernate();
  uiSleepHoldPins();
  Serial.flush();
  delay(500);
  // s_config 每次開機歸零，不可依其判斷 RTC 殘留狀態：timer 為一次性
  // （觸發即耗盡）無需清除；EXT1 為電位式殘留——以「零遮罩覆寫」確保
  // 停用，可避免 disable 未啟用源造成的錯誤 log 與立即喚醒迴圈。
  esp_sleep_enable_ext1_wakeup(0, ESP_EXT1_WAKEUP_ANY_LOW);  // 清除 RTC EXT1
  if (timerUs > 0) {
    esp_sleep_enable_timer_wakeup(timerUs);
  }
  if (enableExt1) {
    esp_sleep_enable_ext1_wakeup((1ULL << BTN_UP) | (1ULL << BTN_DOWN) |
                                 (1ULL << BTN_PRESS), ESP_EXT1_WAKEUP_ANY_LOW);
    for (uint8_t p : {(uint8_t)BTN_UP, (uint8_t)BTN_DOWN, (uint8_t)BTN_PRESS}) {
      rtc_gpio_init((gpio_num_t)p);
      rtc_gpio_set_direction((gpio_num_t)p, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_en((gpio_num_t)p);
      rtc_gpio_pulldown_dis((gpio_num_t)p);
    }
  }
  esp_deep_sleep_start();
}

// ---------- 索引計算（wake cause -> idx，全部 mod n） ----------
static int computeIndex(int cur, int count) {
  switch (g_wake) {
    case ESP_SLEEP_WAKEUP_EXT1:
      if (g_wakeMask & (1ULL << BTN_PRESS)) return cur;            // 選單用
      if (g_wakeMask & (1ULL << BTN_UP))    return (cur - 1 + count) % count;
      if (g_wakeMask & (1ULL << BTN_DOWN))  return (cur + 1) % count;
      return cur;
    case ESP_SLEEP_WAKEUP_TIMER:
      return (cur + 1) % count;                                    // 輪播
    default:
      return cur;                                                  // 開機：NVS
  }
}

// ---------- 主流程 ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  uint32_t tBoot0 = millis();
  uint32_t tBoot = tBoot0, tMount = tBoot0, tScan = tBoot0,
           tRead = tBoot0, tRender = tBoot0;
  g_wake = esp_sleep_get_wakeup_cause();
  g_wakeMask = esp_sleep_get_ext1_wakeup_status();
  LOGF("boot wake=%d mask=%llu\n", (int)g_wake, (unsigned long long)g_wakeMask);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_PRESS, INPUT_PULLUP);

  // 喚醒分流依暫存器；互動前等待釋放（卡鍵防護）
  bool released = waitButtonsReleased(WAIT_RELEASE_MS);
  stuckGuard = !released;
  if (stuckGuard) LOGF("[warn] stuck button detected\n");

  uiPowerOnInit();
  tBoot = millis();
  LOGF("boot+display init %lu ms\n", (unsigned long)(millis() - tBoot0));
  if (!photoBegin()) {
    uiShowMessage("NO SD", "insert card with /raw_photos");
    photoEnd();
    uiHibernate();
    // spec：失敗路徑本輪停用 timer，只留按鍵喚醒（卡鍵時才退 timer-only）
    goToDeepSleep(stuckGuard ? SLEEP_US_RETRY : 0, !stuckGuard && !anyWakePinLow());
    return;
  }
  tMount = millis();
  int n = photoScan();
  tScan = millis();
  if (n <= 0) {
    const char* t = (n == -2) ? "TOO MANY PHOTOS" : "NO PHOTOS";
    uiShowMessage(t, "fix card /raw_photos");
    photoEnd();
    uiHibernate();
    goToDeepSleep(stuckGuard ? SLEEP_US_RETRY : 0, !stuckGuard && !anyWakePinLow());
    return;
  }
  int prevIdx = loadIdx();
  int idx = computeIndex(prevIdx, n);
  int shown = -1;
  for (int attempt = 0; attempt < n; attempt++) {
    int cand = (idx + attempt) % n;                  // 只向前、最多一圈
    if (photoLoad(cand) == 0) { shown = cand; break; }
  }
  if (shown < 0) {
    uiShowMessage("NO VALID PHOTOS", "all files invalid");
    photoEnd();
    uiHibernate();
    goToDeepSleep(stuckGuard ? SLEEP_US_RETRY : 0, !stuckGuard && !anyWakePinLow());
    return;
  }
  tRead = millis();
  uiShowPhoto();
  tRender = millis();
  LOGF("perf boot=%lu mount=%lu scan_sort=%lu read=%lu render=%lu total=%lu ms\n",
       (unsigned long)(tBoot - tBoot0), (unsigned long)(tMount - tBoot),
       (unsigned long)(tScan - tMount), (unsigned long)(tRead - tScan),
       (unsigned long)(tRender - tRead), (unsigned long)(tRender - tBoot0));
  LOGF("shown photo %d (%s)\n", shown, photoName(shown));

  // PRESS 喚醒：直接進設定選單（不先重刷同一張照片）；SD 仍掛載以利還原
  if (g_wake == ESP_SLEEP_WAKEUP_EXT1 && (g_wakeMask & (1ULL << BTN_PRESS))) {
    waitButtonsReleased(WAIT_RELEASE_MS);       // 確認前釋放下壓
    int cur = loadSlideIdx();
    int picked = menuLoop(cur);
    if (picked != cur) saveSlideIdx(picked);    // 僅變更時寫入
    if (photoLoad(shown) != 0) {                // 內容被選單覆蓋，重讀原圖
      uiShowMessage("LOAD FAIL", "photo invalid");
      photoEnd();
      uiHibernate();
      goToDeepSleep(stuckGuard ? SLEEP_US_RETRY : 0, !stuckGuard && !anyWakePinLow());
      return;
    }
    uiShowPhoto();                              // 照片還原（第 2 次內容 full refresh）
    LOGF("menu done, slideshow idx=%d\n", picked);
  }
  photoEnd();
  if (shown != prevIdx) saveIdx(shown);   // 僅變更時寫入（降 flash 磨損）
  uiHibernate();
  // 依 NVS slide 掛 timer：OFF 或無照片一律不掛（只留三鍵 EXT1）
  int slideIdx = loadSlideIdx();
  uint64_t timerUs = 0;
  if (SLIDE_OPTIONS_SEC[slideIdx] > 0 && g_photoCount > 0) {
    timerUs = (uint64_t)SLIDE_OPTIONS_SEC[slideIdx] * 1000000ULL;
  }
  // 卡鍵：本輪 5 分鐘 timer-only（無 EXT1），下輪再試
  if (stuckGuard) {
    timerUs = SLEEP_US_RETRY;
  }
  goToDeepSleep(timerUs, !stuckGuard && !anyWakePinLow());
}

void loop() {
  delay(100);
}