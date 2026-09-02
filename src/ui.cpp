#include "ui.h"
#include "log.h"
#include "fonts_quote.h"
#include "quote_logic.h"
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "driver/gpio.h"

#define EPD_SCK  12
#define EPD_MOSI 11
#define EPD_CS   45
#define EPD_DC   46
#define EPD_RST  47
#define EPD_BUSY 48
#define EPD_PWR   7

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;
static bool initialized = false;

void uiInit() {
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(50);
  gpio_hold_dis(GPIO_NUM_12);
  gpio_hold_dis(GPIO_NUM_11);
  gpio_hold_dis(GPIO_NUM_45);
  gpio_hold_dis(GPIO_NUM_46);
  gpio_hold_dis(GPIO_NUM_47);
  gpio_deep_sleep_hold_dis();
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  uint32_t t0 = millis();
  display.init(115200, true, 2, false);
  display.setRotation(3);   // 唯一旋轉層（硬體驗證 3 正立；spec 修訂七版）；init 一次
  u8g2.begin(display);
  initialized = true;
  LOGF("display init %lu ms rot=%d w=%d h=%d\n", (unsigned long)(millis() - t0),
       display.getRotation(), display.width(), display.height());
  // 期望：rot=3 w=272 h=792（R3：邏輯頂＝面板左緣——面板左側邊朝上安裝）
}

// 版面常數（像素級調校；row stride 須 > 漲跌基線 112 且 < 分隔線 130）
static const int ROW_Y0 = 56;
static const int ROW_STRIDE = 138;

// setFont 每次呼叫都會把 adapter 重設為 solid 模式（U8g2_for_Adafruit_GFX
// u8g2_SetFont 行為：is_transparent=0），solid 背景以未設定的 bg_color=0
// （GxEPD_BLACK）填格→glyph 變實心黑塊。故每次 setFont 後必須補透明模式。
static void setFontT(const uint8_t* f) {
  u8g2.setFont(f);
  u8g2.setFontMode(1);                        // 透明：只畫前景
  u8g2.setBackgroundColor(GxEPD_WHITE);       // 保險（本應用不用 solid 背景）
}

static void drawArrowUp(int x, int yBase) {
  display.fillTriangle(x, yBase, x + 18, yBase, x + 9, yBase - 14, GxEPD_BLACK);
}
static void drawArrowDown(int x, int yBase) {
  display.fillTriangle(x, yBase - 14, x + 18, yBase - 14, x + 9, yBase, GxEPD_BLACK);
}
static void drawFlatDash(int x, int yBase) {
  display.fillRect(x, yBase - 5, 18, 5, GxEPD_BLACK);
}

void uiShowQuotes(const QuoteView& v) {
  if (!initialized) return;
  // 全部圖元經已 setRotation(3) 之 display（邏輯 272x792）——spec 修訂七版
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    // header
    setFontT(u8g2_font_quote16);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(16, 26);
    u8g2.print(v.dateStr);
    int tw = u8g2.getUTF8Width(v.timeStr);
    u8g2.setCursor(272 - 16 - tw, 26);
    u8g2.print(v.timeStr);
    display.drawLine(16, 40, 256, 40, GxEPD_BLACK);
    // 5 列（row0=加權指數：名稱 28px）
    for (int i = 0; i < QUOTE_ROWS; i++) {
      int y0 = ROW_Y0 + i * ROW_STRIDE;
      setFontT(i == 0 ? u8g2_font_quote28 : u8g2_font_quote20);
      u8g2.setCursor(16, y0 + (i == 0 ? 26 : 20));
      if (v.names[i]) u8g2.print(v.names[i]);
      char buf[24];
      if (v.z[i] == 0.0) {
        // 今日未成交（z="-"，spec 修訂七版）：現價與漲跌行均 "--"（22px）、無箭頭；
        // 不得渲染為價格 0 或漲跌 -100%（y 必不為 0、真實價格必不為 0）
        setFontT(u8g2_font_logisoso22_tr);
        u8g2.setCursor(16, y0 + 72);
        u8g2.print("--");
        u8g2.setCursor(42, y0 + 112);
        u8g2.print("--");
      } else {
        qlogic::formatPrice(v.z[i], buf, sizeof buf);
        setFontT(u8g2_font_logisoso38_tr);
        u8g2.setCursor(16, y0 + 72);
        u8g2.print(buf);
        setFontT(u8g2_font_logisoso22_tr);
        if (v.chg[i] > 0.0001)      drawArrowUp(16, y0 + 112);
        else if (v.chg[i] < -0.0001) drawArrowDown(16, y0 + 112);
        else                         drawFlatDash(16, y0 + 112);
        snprintf(buf, sizeof buf, "%+.2f  %+.2f%%", v.chg[i], v.pct[i]);
        u8g2.setCursor(42, y0 + 112);
        u8g2.print(buf);
      }
      if (i < QUOTE_ROWS - 1) display.drawLine(16, y0 + 130, 256, y0 + 130, GxEPD_BLACK);
    }
    if (v.status) {
      setFontT(u8g2_font_quote16);
      u8g2.setCursor(16, 780);
      u8g2.print(v.status);
    }
  } while (display.nextPage());
}

void uiShowMessage(const char* line1, const char* line2) {
  if (!initialized) return;
  // 英文訊息用 helv 字型（相框已驗證含完整 ASCII；logisoso 字集不保證全字母）
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2.setForegroundColor(GxEPD_BLACK);
    setFontT(u8g2_font_helvB24_tf);
    u8g2.setCursor(16, 120);
    if (line1) u8g2.print(line1);
    setFontT(u8g2_font_helvR14_tf);
    u8g2.setCursor(16, 160);
    if (line2) u8g2.print(line2);
  } while (display.nextPage());
}

void uiHibernate() {
  if (!initialized) return;
  display.hibernate();
  initialized = false;   // 深睡前停用 guard，防 hibernate 後誤寫 SPI
}

void uiSleepHoldPins() {
  digitalWrite(EPD_PWR, LOW);   // GPIO7 拉低（顯示器斷電）
  gpio_hold_en(GPIO_NUM_12);
  gpio_hold_en(GPIO_NUM_11);
  gpio_hold_en(GPIO_NUM_45);
  gpio_hold_en(GPIO_NUM_46);
  gpio_hold_en(GPIO_NUM_47);
  gpio_deep_sleep_hold_en();
}
