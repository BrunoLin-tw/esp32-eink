#include <Arduino.h>
#include "log.h"
#include "weather.h"
#include "locations.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "缺少 src/secrets.h：複製 secrets.h.example 並填入 Wi-Fi 憑證"
#endif

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("weather station skeleton, ssid set=%s\n",
       strlen(WIFI_SSID) > 0 ? "yes" : "no");
  wifiConnect(15000);
  if (syncClock(10000)) {
    struct tm tmLoc;
    // 板橋位移量暫代，Task 4 起改用 API 回傳值
    if (localTime(8 * 3600L, &tmLoc)) {
      LOGF("local time: %04d-%02d-%02d %02d:%02d:%02d\n",
           tmLoc.tm_year + 1900, tmLoc.tm_mon + 1, tmLoc.tm_mday,
           tmLoc.tm_hour, tmLoc.tm_min, tmLoc.tm_sec);
    }
  }

  const Location& loc = LOCATIONS[DEFAULT_LOCATION];
  WeatherData data;
  if (fetchWeather(loc.lat, loc.lon, &data)) {
    dumpWeather(data);
  }
}

void loop() {
  delay(100);
}
