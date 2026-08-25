#include <Arduino.h>
#include <Preferences.h>
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

static void runUpdateCycle(int locIdx) {
  const Location& loc = LOCATIONS[locIdx];
  if (!wifiConnect(15000)) {
    uiShowOffline("wifi failed");
    return;
  }
  syncClock(10000);
  WeatherData data;
  if (fetchWeather(loc.lat, loc.lon, &data)) {
    uiRenderDashboard(loc, data);
  } else {
    uiShowOffline("fetch failed");
  }
}

// awake 模式：撥桿上下選地點、下壓確認；idleMs 無操作即返回。
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
      uiClearHint();
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
          else { uiClearHint(); return curIdx; }
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
  LOGF("weather station boot\n");

  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_EXIT, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_PRESS, INPUT_PULLUP);

  uiPowerOnInit();
  int idx = loadLocationIdx();
  runUpdateCycle(idx);

  // T7 暫時驗證用：T8 將改為 sleep/wake 分流
  int picked = awakeLoop(idx, 20000);
  if (picked != idx) {
    saveLocationIdx(picked);
    runUpdateCycle(picked);
  } else {
    saveLocationIdx(idx);
  }
}

void loop() {
  delay(100);
}
