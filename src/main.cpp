#include <Arduino.h>
#include <esp_sleep.h>
#include <SPI.h>
#include <GxEPD2_BW.h>

// ---------- 日誌 ----------
#define LOGF(...) do { \
  Serial.printf("[%010lu] ", (unsigned long)millis()); \
  Serial.printf(__VA_ARGS__); \
} while (0)

// 顯示器腳位（docs/device-research.md）
#define EPD_SCK  12
#define EPD_MOSI 11
#define EPD_CS   45
#define EPD_DC   46
#define EPD_RST  47
#define EPD_BUSY 48
#define EPD_PWR   7

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

bool epdInitialized = false;

void epdPowerOn() {
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(50);  // 電源穩定
}

void drawTestPattern() {
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(2);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    for (int x = 0; x < (int)display.width(); x += 24) {
      display.drawFastVLine(x, 0, display.height(), GxEPD_BLACK);
    }
    for (int y = 0; y < (int)display.height(); y += 24) {
      display.drawFastHLine(0, y, display.width(), GxEPD_BLACK);
    }
    display.fillRect(20, 20, 320, 80, GxEPD_WHITE);
    display.drawRect(20, 20, 320, 80, GxEPD_BLACK);
    display.setCursor(32, 48);
    display.print("ESP32-S3 ePaper");
    display.setCursor(32, 72);
    display.print("full refresh");
  } while (display.nextPage());
}

bool requireDisplay() {
  if (!epdInitialized) {
    LOGF("[skip] display not initialized, run 'd' first\n");
    return false;
  }
  return true;
}

// ---------- 前向宣告 ----------
void cmdInfo();
void cmdDisplay();

// ---------- 指令分派表 ----------
struct TestCmd {
  char key;
  const char* name;
  void (*fn)();
};

TestCmd commands[] = {
  {'i', "info", cmdInfo},
  {'d', "display", cmdDisplay},
};

void printMenu() {
  Serial.println();
  Serial.println("==== bring-up test menu ====");
  for (auto& c : commands) {
    Serial.printf(" %c  %s\n", c.key, c.name);
  }
}

// ---------- 測試 ----------
void cmdInfo() {
  LOGF("chip=%s rev=%d cores=%d\n",
       ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  LOGF("flash size=%u KB speed=%u MHz\n",
       (unsigned)(ESP.getFlashChipSize() / 1024),
       (unsigned)(ESP.getFlashChipSpeed() / 1000000));
  if (psramFound()) {
    LOGF("psram size=%u bytes (OK)\n", (unsigned)ESP.getPsramSize());
  } else {
    LOGF("[warn] PSRAM NOT FOUND\n");
  }
}

void cmdDisplay() {
  epdPowerOn();
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  // BUSY timeout 由 GxEPD2 1.6.9 內建：預設 10 s，等待中以 delay(1)+yield() 讓出執行權
  uint32_t t0 = millis();
  display.init(115200, true, 2, false);   // init 含第一次 clean full refresh
  LOGF("init + clean full refresh: %lu ms\n", (unsigned long)(millis() - t0));
  t0 = millis();
  drawTestPattern();  // firstPage/nextPage 迴圈結束時即完成刷新；不進 hibernate，供後續測試使用
  LOGF("test pattern refresh: %lu ms\n", (unsigned long)(millis() - t0));
  epdInitialized = true;
}

// ---------- 主程式 ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    LOGF("wake from deep sleep (timer)\n");
  } else {
    LOGF("power-on/reset (cause=%d)\n", (int)cause);
  }
  printMenu();
}

void loop() {
  if (Serial.available()) {
    char c = (char)Serial.read();
    for (auto& cmd : commands) {
      if (cmd.key == c) {
        LOGF("== start '%c' (%s)\n", c, cmd.name);
        cmd.fn();
        LOGF("== end '%c'\n", c);
        printMenu();
        break;
      }
    }
  }
  delay(10);
}
