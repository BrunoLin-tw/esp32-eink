#include "ui.h"
#include "log.h"
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
  display.setRotation(1);   // 唯一旋轉層（spec 修訂三版）；init 一次
  u8g2.begin(display);
  initialized = true;
  LOGF("display init %lu ms rot=%d w=%d h=%d\n", (unsigned long)(millis() - t0),
       display.getRotation(), display.width(), display.height());
  // 期望：rot=1 w=272 h=792
}

void uiShowQuotes(const QuoteView& v) { (void)v; }
void uiShowMessage(const char* l1, const char* l2) { (void)l1; (void)l2; }

void uiHibernate() {
  if (!initialized) return;
  display.hibernate();
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
