#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"
#include "log.h"
#include "weather.h"
#include "locations.h"
#include "ui.h"

// 按鍵（active-low，docs/device-research.md）
#define BTN_MENU  2
#define BTN_EXIT  1
#define BTN_UP    6
#define BTN_DOWN  4
#define BTN_PRESS 5

struct BtnDef {
  const char* name;
  uint8_t pin;
};

#define SLEEP_US_NORMAL (30ULL * 60ULL * 1000000ULL)
#define SLEEP_US_RETRY  (5ULL  * 60ULL * 1000000ULL)

// 最後成功看板的 RTC 快取：跨 deep sleep 存活；電源重置後 magic 歸零失效
static RTC_DATA_ATTR WeatherData g_cache;
static RTC_DATA_ATTR uint32_t g_cacheMagic = 0;
#define CACHE_MAGIC 0xCACE0FEEu

static bool cacheValid() {
  return g_cacheMagic == CACHE_MAGIC && g_cache.valid;
}

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "缺少 src/secrets.h：複製 secrets.h.example 並填入 Wi-Fi 憑證"
#endif

static int loadLocationIdx() {
  Preferences prefs;
  prefs.begin("weather", true);
  int idx = prefs.getInt("loc", DEFAULT_LOCATION);
  prefs.end();
  if (idx < 0 || idx >= LOCATION_COUNT) idx = DEFAULT_LOCATION;
  return idx;
}

static void saveLocationIdx(int idx) {
  Preferences prefs;
  prefs.begin("weather", false);
  prefs.putInt("loc", idx);
  prefs.end();
}

static bool runUpdateCycle(int locIdx, bool showOfflineOnFail = true) {
  const Location& loc = LOCATIONS[locIdx];
  if (!wifiConnect(15000)) {
    if (showOfflineOnFail) uiShowOffline("wifi failed");
    return false;
  }
  if (!syncClock(10000)) {
    // 時鐘未同步會使預報索引無意義，視同更新失敗
    if (showOfflineOnFail) uiShowOffline("time sync failed");
    return false;
  }
  WeatherData data;
  if (fetchWeather(loc.lat, loc.lon, &data)) {
    uiRenderDashboard(loc, data);
    g_cache = data;             // 成功即更新 RTC 快取
    g_cache.valid = true;
    g_cacheMagic = CACHE_MAGIC;
    return true;
  }
  if (showOfflineOnFail) uiShowOffline("fetch failed");
  return false;
}

static bool wokeByButton() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return false;
  return (esp_sleep_get_ext1_wakeup_status() >> BTN_PRESS) & 1ULL;
}

static void goToDeepSleep(bool retryShort) {
  LOGF("sleeping %s\n", retryShort ? "5 min (retry)" : "30 min");
  uiHibernate();            // controller hibernate + GPIO7 低
  wifiOff();                // 射頻停用
  uiSleepHoldPins();        // 控制線固定 LOW＋hold，避免深睡浮接
  // SD 未掛載；無需處理 GPIO42
  Serial.flush();
  delay(500);

  // 撥桿下壓（active-low）作為 EXT1 喚醒源；睡眠期間保持內部拉高
  esp_sleep_enable_timer_wakeup(retryShort ? SLEEP_US_RETRY : SLEEP_US_NORMAL);
  esp_sleep_enable_ext1_wakeup(1ULL << BTN_PRESS, ESP_EXT1_WAKEUP_ANY_LOW);
  rtc_gpio_init((gpio_num_t)BTN_PRESS);
  rtc_gpio_set_direction((gpio_num_t)BTN_PRESS, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)BTN_PRESS);
  rtc_gpio_pulldown_dis((gpio_num_t)BTN_PRESS);

  esp_deep_sleep_start();
}

// awake 模式：撥桿上下選地點、下壓確認；idleMs 無操作即返回。
// 只回傳選擇結果；提示條的清除與畫面還原由呼叫端處理。
static int awakeLoop(int curIdx, uint32_t idleTimeoutMs) {
  BtnDef awButtons[] = {
    {"UP", BTN_UP}, {"DOWN", BTN_DOWN}, {"PRESS", BTN_PRESS},
  };
  bool last[3] = {true, true, true};
  uint32_t lastAct = millis();
  uint32_t debounceAt = 0;
  uiAwakeHint(LOCATIONS[curIdx].name);
  while (true) {
    uint32_t now = millis();
    if (now - lastAct > idleTimeoutMs) {
      LOGF("awake idle timeout\n");
      return curIdx;
    }
    if (now - debounceAt < 30) {
      delay(5);
      yield();
      continue;
    }
    debounceAt = now;
    for (int i = 0; i < 3; i++) {
      bool released = digitalRead(awButtons[i].pin);  // active-low
      if (released != last[i]) {
        last[i] = released;
        if (!released) {
          lastAct = millis();
          if (i == 0) curIdx = (curIdx + 1) % LOCATION_COUNT;
          else if (i == 1) curIdx = (curIdx + LOCATION_COUNT - 1) % LOCATION_COUNT;
          else return curIdx;
          LOGF("select -> %s\n", LOCATIONS[curIdx].name);
          uiAwakeHint(LOCATIONS[curIdx].name);
        }
      }
    }
    delay(5);
    yield();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  bool btnWake = wokeByButton();
  LOGF("boot (%s)\n", btnWake ? "button wake" : "timer/power-on");

  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_EXIT, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_PRESS, INPUT_PULLUP);

  uiPowerOnInit();
  int idx = loadLocationIdx();

  bool haveCache = cacheValid();
  bool ok = runUpdateCycle(idx, !haveCache);

  if (!ok) {
    // 有快取：重繪最後成功看板＋離線 badge；無快取維持整頁 OFFLINE
    if (haveCache) {
      uiRenderDashboard(LOCATIONS[idx], g_cache);
      uiOfflineBadge();
      LOGF("showing cached dashboard\n");
    }
    goToDeepSleep(true);  // 失敗：5 分鐘後重試，不進 awake
    return;               // 不會到達，防禦性
  }

  // 到此代表初始更新成功（快取必然有效）；awake 退出後一律還原完整畫面
  if (btnWake) {
    int picked = awakeLoop(idx, 20000);
    if (picked != idx) {
      saveLocationIdx(picked);  // NVS 僅在實際變更時寫入
      ok = runUpdateCycle(picked, false);
      if (!ok) uiOfflineBadge();
    } else {
      // 同地點確認／idle 逾時：提示條曾覆蓋底部預報，用快取重繪還原
      uiRenderDashboard(LOCATIONS[picked], g_cache);
    }
  }
  goToDeepSleep(!ok);
}

void loop() {
  delay(100);
}
