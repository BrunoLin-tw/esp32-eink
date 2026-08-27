#include "ui.h"
#include "log.h"
#include "photo_store.h"
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

static const uint8_t* const F_TITLE = u8g2_font_helvB24_tf;
static const uint8_t* const F_BODY  = u8g2_font_helvR14_tf;
static const uint8_t* const F_SMALL = u8g2_font_helvR12_tf;

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;

static bool initialized = false;

static const char* MENU_OPTIONS[] = {"OFF", "1 min", "5 min", "15 min", "30 min"};
static const int MENU_COUNT = 5;

void uiPowerOnInit() {
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
  LOGF("display init %lu ms\n", (unsigned long)(millis() - t0));
  u8g2.begin(display);
  initialized = true;
}

static void drawText(int x, int yBaseline, const uint8_t* font, const char* s) {
  u8g2.setFont(font);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setCursor(x, yBaseline);
  u8g2.print(s);
}

void uiShowPhoto() {
  if (!initialized) return;
  display.setFullWindow();   // 離開任何 partial 殘留狀態
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(0, 0, g_bitmap, 792, 272, GxEPD_BLACK);
  } while (display.nextPage());
}

void uiShowMessage(const char* title, const char* detail) {
  if (!initialized) return;
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawText(40, 120, F_TITLE, title);
    drawText(40, 170, F_BODY, detail);
  } while (display.nextPage());
}

void uiMenuScreen(int cursor) {
  if (!initialized) return;
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawText(40, 80, F_TITLE, "SLIDESHOW");
    for (int i = 0; i < MENU_COUNT; i++) {
      char line[32];
      snprintf(line, sizeof(line), "%s  %s", i == cursor ? ">" : " ",
               MENU_OPTIONS[i]);
      drawText(60, 130 + i * 26, F_BODY, line);
    }
    drawText(40, 260, F_SMALL, "UP/DOWN select  PRESS ok  (20s = ok)");
  } while (display.nextPage());
}

void uiHibernate() {
  if (initialized) {
    display.hibernate();
    initialized = false;
  }
  digitalWrite(EPD_PWR, LOW);
}

void uiSleepHoldPins() {
  pinMode(EPD_CS, OUTPUT);   digitalWrite(EPD_CS, LOW);
  pinMode(EPD_DC, OUTPUT);   digitalWrite(EPD_DC, LOW);
  pinMode(EPD_RST, OUTPUT);  digitalWrite(EPD_RST, LOW);
  pinMode(EPD_SCK, OUTPUT);  digitalWrite(EPD_SCK, LOW);
  pinMode(EPD_MOSI, OUTPUT); digitalWrite(EPD_MOSI, LOW);
  gpio_hold_en(GPIO_NUM_12);
  gpio_hold_en(GPIO_NUM_11);
  gpio_hold_en(GPIO_NUM_45);
  gpio_hold_en(GPIO_NUM_46);
  gpio_hold_en(GPIO_NUM_47);
  gpio_deep_sleep_hold_en();
}
