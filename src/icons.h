#pragma once
#include <Arduino.h>

enum class IconId : uint8_t {
  SUN, PARTLY, CLOUDY, FOG, DRIZZLE, RAIN, SNOW, SHOWERS, SLEET, THUNDER
};

// WMO weather_code -> IconId（涵蓋 spec 列出的全部碼段）
IconId iconForCode(int wmo);

// 在 (x,y) 為左上角、邊長 size 的方形內繪製圖示（黑白）。
void drawIcon(int x, int y, int size, IconId id);
