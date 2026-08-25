#include "ui.h"
#include "log.h"
#include <SPI.h>
#include <GxEPD2_BW.h>

#define EPD_SCK  12
#define EPD_MOSI 11
#define EPD_CS   45
#define EPD_DC   46
#define EPD_RST  47
#define EPD_BUSY 48
#define EPD_PWR   7

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static bool initialized = false;

void uiPowerOnInit() {
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(50);
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.cp437(true);  // 使 \xF8 對應 ° 符號
  uint32_t t0 = millis();
  display.init(115200, true, 2, false);
  LOGF("display init %lu ms\n", (unsigned long)(millis() - t0));
  initialized = true;
}

static void drawText(int x, int y, int size, const char* s) {
  display.setTextSize(size);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(x, y);
  display.print(s);
}

void uiRenderDashboard(const Location& loc, const WeatherData& d) {
  if (!initialized) return;
  char buf[64];

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // 左上：地點與當地時間
    drawText(16, 28, 3, loc.name);
    struct tm tmLoc;
    if (localTime(d.utcOffsetSec, &tmLoc)) {
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d",
               tmLoc.tm_year + 1900, tmLoc.tm_mon + 1, tmLoc.tm_mday,
               tmLoc.tm_hour, tmLoc.tm_min);
      drawText(16, 60, 2, buf);
    }

    // 圖示區（Task 6 填入）：右上 96x96 留白框
    display.drawRect(680, 24, 96, 96, GxEPD_BLACK);

    // 目前狀態：大字溫度
    snprintf(buf, sizeof(buf), "%.1f\xF8" "C", d.temp);  // \xF8 = °(CP437)
    drawText(420, 90, 6, buf);

    // 高低/濕度/風速列
    snprintf(buf, sizeof(buf), "RH %d%%   Wind %dkm/h",
             d.humidity, d.windKmh);
    drawText(420, 160, 2, buf);

    // 分隔線
    display.drawFastHLine(0, 192, display.width(), GxEPD_BLACK);

    // 下半：6 格逐時預報（文字版；icon 於 Task 6 加入）
    int cellW = display.width() / 6;
    struct tm tmNow;
    localTime(d.utcOffsetSec, &tmNow);
    int startH = tmNow.tm_hour + 1;
    if (startH > 18) startH = 18;
    for (int i = 0; i < 6; i++) {
      int cx = i * cellW;
      if (i > 0) display.drawFastVLine(cx, 196, 76, GxEPD_BLACK);
      snprintf(buf, sizeof(buf), "%02d:00", (startH + i) % 24);
      drawText(cx + 12, 208, 2, buf);
      snprintf(buf, sizeof(buf), "%.0f\xF8%d%%",
               d.hours[i].temp, d.hours[i].precipProb);
      drawText(cx + 10, 240, 3, buf);
    }
  } while (display.nextPage());
}

void uiShowOffline(const char* reason) {
  if (!initialized) return;
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawText(40, 80, 5, "OFFLINE");
    drawText(40, 140, 2, reason);
    drawText(40, 170, 2, "retry in 5 min");
  } while (display.nextPage());
}

void uiHibernate() {
  if (initialized) {
    display.hibernate();
    initialized = false;
  }
  digitalWrite(EPD_PWR, LOW);  // GPIO7 拉低
}
