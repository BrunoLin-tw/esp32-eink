#pragma once
#include <Arduino.h>
#include "weather.h"
#include "locations.h"

// GPIO7 上電、SPI/display init（含首次 clean full refresh）。
void uiPowerOnInit();

// 渲染完整看板並 full refresh。
void uiRenderDashboard(const Location& loc, const WeatherData& d);

// 錯誤畫面（保留上次資料由呼叫端另行處理；此處顯示訊息）。
void uiShowOffline(const char* reason);

// hibernate controller（deep sleep 前呼叫）。
void uiHibernate();
