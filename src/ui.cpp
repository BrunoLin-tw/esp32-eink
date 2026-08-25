#include "ui.h"
#include "log.h"
#include "icons.h"
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

#define EPD_SCK  12
#define EPD_MOSI 11
#define EPD_CS   45
#define EPD_DC   46
#define EPD_RST  47
#define EPD_BUSY 48
#define EPD_PWR   7

// 字型配置（U8g2 比例字型；cursor 以「基線」為準）
static const uint8_t* const F_LOCATION  = u8g2_font_helvB24_tf;
static const uint8_t* const F_DATETIME  = u8g2_font_helvR18_tf;
static const uint8_t* const F_BIGTEMP   = u8g2_font_logisoso62_tn;
static const uint8_t* const F_DETAIL    = u8g2_font_helvR18_tf;
static const uint8_t* const F_CELL_TIME = u8g2_font_helvR14_tf;
static const uint8_t* const F_CELL_VAL  = u8g2_font_helvB14_tf;
static const uint8_t* const F_OFFLINE   = u8g2_font_helvB24_tf;

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;

static bool initialized = false;

void uiPowerOnInit() {
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(50);
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  uint32_t t0 = millis();
  display.init(115200, true, 2, false);
  LOGF("display init %lu ms\n", (unsigned long)(millis() - t0));
  u8g2.begin(display);
  initialized = true;
}

static void drawText(int x, int yBaseline, const uint8_t* font, const char* s) {
  u8g2.setFont(font);
  u8g2.setFontMode(1);  // 透明背景：疊在白底上
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setCursor(x, yBaseline);
  u8g2.print(s);
}

void uiRenderDashboard(const Location& loc, const WeatherData& d) {
  if (!initialized) return;
  char buf[64];

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // 左上：地點與當地時間
    drawText(16, 44, F_LOCATION, loc.name);
    struct tm tmLoc;
    if (localTime(d.utcOffsetSec, &tmLoc)) {
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d",
               tmLoc.tm_year + 1900, tmLoc.tm_mon + 1, tmLoc.tm_mday,
               tmLoc.tm_hour, tmLoc.tm_min);
      drawText(16, 80, F_DATETIME, buf);
    }

    // 圖示區：右上 96x96
    drawIcon(680, 24, 96, iconForCode(d.code));

    // 目前狀態：大字溫度（logisoso62_tn 純數字；°C 以 helvR18 接在後）
    snprintf(buf, sizeof(buf), "%.1f", d.temp);
    u8g2.setFont(F_BIGTEMP);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(420, 136);
    u8g2.print(buf);
    drawText(u8g2.getCursorX() + 6, 136, F_DETAIL, "°C");

    // 濕度/風速列
    snprintf(buf, sizeof(buf), "RH %d%%   Wind %dkm/h",
             d.humidity, d.windKmh);
    drawText(432, 172, F_DETAIL, buf);

    // 分隔線
    display.drawFastHLine(0, 192, display.width(), GxEPD_BLACK);

    // 下半：6 格逐時預報
    int cellW = display.width() / 6;
    struct tm tmNow;
    localTime(d.utcOffsetSec, &tmNow);
    int startH = tmNow.tm_hour + 1;
    if (startH > 18) startH = 18;
    for (int i = 0; i < 6; i++) {
      int cx = i * cellW;
      if (i > 0) display.drawFastVLine(cx, 196, 76, GxEPD_BLACK);
      snprintf(buf, sizeof(buf), "%02d:00", (startH + i) % 24);
      drawText(cx + 10, 216, F_CELL_TIME, buf);
      snprintf(buf, sizeof(buf), "%.0f° %d%%",
               d.hours[i].temp, d.hours[i].precipProb);
      drawText(cx + 10, 254, F_CELL_VAL, buf);
      drawIcon(cx + cellW - 44, 202, 32, iconForCode(d.hours[i].code));
    }
  } while (display.nextPage());
}

void uiShowOffline(const char* reason) {
  if (!initialized) return;
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawText(40, 110, F_OFFLINE, "OFFLINE");
    drawText(40, 160, F_DETAIL, reason);
    drawText(40, 190, F_DETAIL, "retry in 5 min");
  } while (display.nextPage());
}

void uiHibernate() {
  if (initialized) {
    display.hibernate();
    initialized = false;
  }
  digitalWrite(EPD_PWR, LOW);  // GPIO7 拉低
}
