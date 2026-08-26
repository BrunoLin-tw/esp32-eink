#pragma once
#include <Arduino.h>

// 連線 Wi-Fi（阻塞至成功或逾時）。回傳是否成功。
bool wifiConnect(uint32_t timeoutMs);

void wifiOff();

#include <time.h>

// 以 NTP 同步 UTC 時間（阻塞至成功或逾時）。回傳是否成功。
bool syncClock(uint32_t timeoutMs);

// 取得「加上位移量」的當地時間（呼叫端傳入 utc_offset_seconds）。
// 回傳 false 表示時鐘尚未同步。
bool localTime(long utcOffsetSec, struct tm* out);

#define FORECAST_POINTS 5   // 未來五個時間點，間隔三小時
#define FORECAST_STEP_H 3

struct HourPoint {
  int hourLabel;  // 實際時刻（0-23），因間隔取樣非連續小時
  float temp;
  int precipProb;
  int code;
};

struct WeatherData {
  bool valid;
  float temp;      // 目前溫度
  int humidity;    // 相對濕度 %
  int windKmh;     // 風速 km/h
  int code;        // 目前 WMO 天氣碼
  long utcOffsetSec;
  HourPoint hours[FORECAST_POINTS];
};

// 抓取並解析指定地點天氣；失敗時 data->valid = false。
bool fetchWeather(float lat, float lon, WeatherData* data);

// 列出資料內容供人工核對（serial log）。
void dumpWeather(const WeatherData& d);
