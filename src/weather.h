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
