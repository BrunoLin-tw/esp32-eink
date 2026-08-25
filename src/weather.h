#pragma once
#include <Arduino.h>

// 連線 Wi-Fi（阻塞至成功或逾時）。回傳是否成功。
bool wifiConnect(uint32_t timeoutMs);

void wifiOff();
