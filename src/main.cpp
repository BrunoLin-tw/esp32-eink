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
  if (!photoBegin()) {
    uiShowMessage("NO SD", "insert card with /raw_photos");
    photoEnd();
    uiHibernate();
    goToDeepSleep(SLEEP_US_RETRY, !stuckGuard && !anyWakePinLow());
    return;
  }
  int n = photoScan();
  if (n <= 0) {
    const char* t = (n == -2) ? "TOO MANY PHOTOS" : "NO PHOTOS";
    uiShowMessage(t, "fix card /raw_photos");
    photoEnd();
    uiHibernate();
    goToDeepSleep(stuckGuard ? SLEEP_US_RETRY : 0, !stuckGuard && !anyWakePinLow());
    return;
  }
  int idx = computeIndex(loadIdx(), n);
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
  uiShowPhoto();
  photoEnd();
  saveIdx(shown);
  LOGF("shown photo %d (%s)\n", shown, photoName(shown));
  uiHibernate();
  // 卡鍵：本輪 5 分鐘 timer-only（無 EXT1），下輪再試；正常路徑 timer 由 T7 接上
  goToDeepSleep(stuckGuard ? SLEEP_US_RETRY : 0, !stuckGuard && !anyWakePinLow());
}

void loop() {
  delay(100);
}