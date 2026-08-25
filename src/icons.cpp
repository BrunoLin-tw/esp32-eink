#include "icons.h"
#include "icons_bitmaps.h"
#include <GxEPD2_BW.h>

extern GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display;

IconId iconForCode(int wmo) {
  if (wmo == 0) return IconId::SUN;
  if (wmo <= 2) return IconId::PARTLY;
  if (wmo == 3) return IconId::CLOUDY;
  if (wmo == 45 || wmo == 48) return IconId::FOG;
  if (wmo >= 51 && wmo <= 57) return IconId::DRIZZLE;
  if ((wmo >= 61 && wmo <= 67)) return IconId::RAIN;
  if (wmo >= 71 && wmo <= 77) return IconId::SNOW;
  if (wmo >= 80 && wmo <= 82) return IconId::SHOWERS;
  if (wmo == 85 || wmo == 86) return IconId::SLEET;
  if (wmo >= 95) return IconId::THUNDER;
  return IconId::CLOUDY;  // 後備
}

void drawIcon(int x, int y, int size, IconId id) {
  const IconBmp& bmp = (size >= 48) ? ICON_SET_64[(int)id]
                                    : ICON_SET_32[(int)id];
  display.drawBitmap(x, y, bmp.data, bmp.w, bmp.h, GxEPD_BLACK);
}
