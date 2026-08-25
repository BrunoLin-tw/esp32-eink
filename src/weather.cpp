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
  url += "&forecast_days=1&timezone=auto";
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

  // 找出「下一個整點」起 6 格的索引：本地小時 + 1，上限 23-5
  struct tm tmLoc;
  localTime(data->utcOffsetSec, &tmLoc);
  int startH = tmLoc.tm_hour + 1;
  if (startH > 18) startH = 18;  // 一日資料最多取到 18..23
  for (int i = 0; i < 6; i++) {
    int idx = startH + i;
    data->hours[i].temp = doc["hourly"]["temperature_2m"][idx] | NAN;
    data->hours[i].precipProb = doc["hourly"]["precipitation_probability"][idx] | -1;
    data->hours[i].code = doc["hourly"]["weather_code"][idx] | -1;
  }
  data->valid = !isnan(data->temp) && data->code >= 0;
  return data->valid;
}

void dumpWeather(const WeatherData& d) {
  if (!d.valid) {
    LOGF("weather invalid\n");
    return;
  }
  LOGF("now: %.1fC rh=%d%% wind=%dkm/h code=%d offset=%lds\n",
       d.temp, d.humidity, d.windKmh, d.code, d.utcOffsetSec);
  struct tm tmLoc;
  localTime(d.utcOffsetSec, &tmLoc);
  int startH = tmLoc.tm_hour + 1;
  if (startH > 18) startH = 18;
  for (int i = 0; i < 6; i++) {
    LOGF("+%dh %02d:00 %.1fC rain=%d%% code=%d\n",
         i + 1, (startH + i) % 24,
         d.hours[i].temp, d.hours[i].precipProb, d.hours[i].code);
  }
}
