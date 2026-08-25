#include <Arduino.h>
#include "log.h"
#include "weather.h"
#include "locations.h"
#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "缺少 src/secrets.h：複製 secrets.h.example 並填入 Wi-Fi 憑證"
#endif

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("weather station boot\n");

  uiPowerOnInit();

  if (!wifiConnect(15000)) {
    uiShowOffline("wifi failed");
    return;  // Task 8 接手 sleep
  }
  syncClock(10000);

  const Location& loc = LOCATIONS[DEFAULT_LOCATION];
  WeatherData data;
  if (fetchWeather(loc.lat, loc.lon, &data)) {
    dumpWeather(data);
    uiRenderDashboard(loc, data);
  } else {
    uiShowOffline("fetch failed");
  }
}

void loop() {
  delay(100);
}
