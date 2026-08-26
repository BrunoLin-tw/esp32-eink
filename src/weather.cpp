#include "weather.h"
#include "log.h"
#include "secrets.h"
#include <WiFi.h>
#include "locations.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

bool wifiConnect(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  LOGF("wifi connecting...\n");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(100);
    yield();
  }
  if (WiFi.status() == WL_CONNECTED) {
    LOGF("wifi connected, ip=%s rssi=%d %lu ms\n",
         WiFi.localIP().toString().c_str(), WiFi.RSSI(),
         (unsigned long)(millis() - t0));
    return true;
  }
  LOGF("[fail] wifi connect timeout (%lu ms)\n",
       (unsigned long)(millis() - t0));
  return false;
}

void wifiOff() {
  WiFi.mode(WIFI_OFF);
  LOGF("wifi off\n");
}

bool syncClock(uint32_t timeoutMs) {
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  uint32_t t0 = millis();
  while (time(nullptr) < 100000 && millis() - t0 < timeoutMs) {
    delay(100);
    yield();
  }
  if (time(nullptr) < 100000) {
    LOGF("[fail] ntp sync timeout\n");
    return false;
  }
  LOGF("ntp synced in %lu ms\n", (unsigned long)(millis() - t0));
  return true;
}

bool localTime(long utcOffsetSec, struct tm* out) {
  time_t nowUtc = time(nullptr);
  if (nowUtc < 100000) return false;
  time_t local = nowUtc + utcOffsetSec;
  gmtime_r(&local, out);
  return true;
}

static bool buildUrl(const Location& loc, String& url) {
  url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude=" + String(loc.lat, 4);
  url += "&longitude=" + String(loc.lon, 4);
  url += "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m";
  url += "&hourly=temperature_2m,precipitation_probability,weather_code";
  url += "&forecast_days=2&timezone=auto";  // 兩日資料，支援跨日取樣
  return true;
}

bool fetchWeather(float lat, float lon, WeatherData* data) {
  data->valid = false;
  Location tmp = {"query", lat, lon};
  String url;
  buildUrl(tmp, url);

  WiFiClientSecure client;
  client.setInsecure();  // 不驗憑證鏈：單機裝置取公開資料的權衡（見 spec）
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    LOGF("[fail] http begin\n");
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    LOGF("[fail] http GET code=%d\n", code);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  LOGF("http ok, payload %u bytes\n", (unsigned)payload.length());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    LOGF("[fail] json parse: %s\n", err.c_str());
    return false;
  }

  data->temp = doc["current"]["temperature_2m"] | NAN;
  data->humidity = doc["current"]["relative_humidity_2m"] | -1;
  data->code = doc["current"]["weather_code"] | -1;
  data->windKmh = (int)(doc["current"]["wind_speed_10m"] | (float)-1);
  data->utcOffsetSec = doc["utc_offset_seconds"] | 0L;

  // 時鐘未同步（NTP 失敗）時不得以未初始化時間推導索引
  struct tm tmLoc;
  if (!localTime(data->utcOffsetSec, &tmLoc)) {
    LOGF("[fail] clock not synced, cannot index hourly\n");
    return false;
  }

  // hourly.time 以 timezone=auto 對齊「當地今日午夜」，共 48 格。
  // 當地小時 h 直接就是今日索引基準；下一個整點 = h+1，取樣點間隔 3h。
  time_t localEpoch = time(nullptr) + data->utcOffsetSec;
  int h = (int)((localEpoch % 86400) / 3600);
  for (int i = 0; i < FORECAST_POINTS; i++) {
    int idx = h + 1 + i * FORECAST_STEP_H;      // 最大 h=23 → idx_max=36 < 48
    data->hours[i].hourLabel = idx % 24;
    data->hours[i].temp = doc["hourly"]["temperature_2m"][idx] | NAN;
    data->hours[i].precipProb = doc["hourly"]["precipitation_probability"][idx] | -1;
    data->hours[i].code = doc["hourly"]["weather_code"][idx] | -1;
  }

  // 完整性核驗：任一欄位缺漏或無效即視為整份失敗，走 OFFLINE 路徑
  if (isnan(data->temp) || data->humidity < 0 || data->windKmh < 0 ||
      data->code < 0) {
    LOGF("[fail] incomplete current fields\n");
    return false;
  }
  for (int i = 0; i < FORECAST_POINTS; i++) {
    const HourPoint& p = data->hours[i];
    if (isnan(p.temp) || p.precipProb < 0 || p.code < 0) {
      LOGF("[fail] incomplete hourly field at point %d\n", i);
      return false;
    }
  }
  data->valid = true;
  return true;
}

void dumpWeather(const WeatherData& d) {
  if (!d.valid) {
    LOGF("weather invalid\n");
    return;
  }
  LOGF("now: %.1fC rh=%d%% wind=%dkm/h code=%d offset=%lds\n",
       d.temp, d.humidity, d.windKmh, d.code, d.utcOffsetSec);
  for (int i = 0; i < FORECAST_POINTS; i++) {
    LOGF("point%d %02d:00 %.1fC rain=%d%% code=%d\n",
         i + 1, d.hours[i].hourLabel,
         d.hours[i].temp, d.hours[i].precipProb, d.hours[i].code);
  }
}
