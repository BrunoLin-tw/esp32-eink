#include <Arduino.h>
#include <esp_sleep.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <SD.h>
#include <WiFi.h>

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

// 按鍵（active-low，docs/device-research.md）
#define BTN_MENU  2
#define BTN_EXIT  1
#define BTN_UP    6
#define BTN_DOWN  4
#define BTN_PRESS 5

// microSD 腳位（docs/device-research.md）；與顯示器使用不同 SPI 匯流排
#define SD_CS   10
#define SD_SCK  39
#define SD_MISO 13
#define SD_MOSI 40
#define SD_PWR  42

SPIClass sdSPI(HSPI);

struct Btn {
  const char* name;
  uint8_t pin;
  bool last;  // true = released
};

Btn buttons[] = {
  {"MENU",  BTN_MENU,  true},
  {"EXIT",  BTN_EXIT,  true},
  {"UP",    BTN_UP,    true},
  {"DOWN",  BTN_DOWN,  true},
  {"PRESS", BTN_PRESS, true},
};

bool btnPollingEnabled = false;
uint32_t lastPollMs = 0;
const uint32_t DEBOUNCE_MS = 30;

void pollButtons() {
  if (!btnPollingEnabled) return;
  uint32_t now = millis();
  if (now - lastPollMs < DEBOUNCE_MS) return;
  lastPollMs = now;
  for (auto& b : buttons) {
    bool released = digitalRead(b.pin);  // active-low
    if (released != b.last) {
      b.last = released;
      LOGF("button %s (GPIO%u) %s\n",
           b.name, b.pin, released ? "released" : "pressed");
    }
  }
}

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

bool epdInitialized = false;

RTC_DATA_ATTR uint32_t sleepCount = 0;  // 跨 deep sleep 保存

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
void cmdPartial();
void cmdButtons();
void cmdSd();
void cmdWifi();
void cmdSleep();

// ---------- 指令分派表 ----------
struct TestCmd {
  char key;
  const char* name;
  void (*fn)();
};

TestCmd commands[] = {
  {'i', "info", cmdInfo},
  {'d', "display", cmdDisplay},
  {'p', "partial", cmdPartial},
  {'b', "buttons", cmdButtons},
  {'s', "sd", cmdSd},
  {'w', "wifi", cmdWifi},
  {'z', "sleep", cmdSleep},
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

const int PARTIAL_COUNT = 30;   // 本輪總更新次數
const int PARTIAL_BATCH = 10;   // 每 10 次 partial 插入一次 full refresh

void drawCounter(int n) {
  display.setPartialWindow(0, 0, display.width(), 96);
  display.firstPage();
  do {
    display.fillRect(0, 0, display.width(), 96, GxEPD_WHITE);
    display.setTextSize(3);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(16, 56);
    display.printf("partial #%d", n);
  } while (display.nextPage());
}

void cmdPartial() {
  if (!requireDisplay()) return;
  uint32_t tAll = millis();
  for (int n = 1; n <= PARTIAL_COUNT; n++) {
    uint32_t t0 = millis();
    drawCounter(n);
    LOGF("partial %d: %lu ms\n", n, (unsigned long)(millis() - t0));
    if (n % PARTIAL_BATCH == 0 && n < PARTIAL_COUNT) {
      LOGF("-- full refresh inserted (every %d partials)\n", PARTIAL_BATCH);
      display.setFullWindow();
      drawTestPattern();  // nextPage 迴圈結束即完成全刷
    }
  }
  LOGF("batch done: %d partials in %lu ms\n",
       PARTIAL_COUNT, (unsigned long)(millis() - tAll));
}

void cmdButtons() {
  btnPollingEnabled = !btnPollingEnabled;
  LOGF("button polling %s (run 'b' again to toggle off)\n",
       btnPollingEnabled ? "ON" : "OFF");
}

void sdPowerDown() {
  digitalWrite(SD_PWR, LOW);
}

void cmdSd() {
  pinMode(SD_PWR, OUTPUT);
  sdPowerDown();
  digitalWrite(SD_PWR, HIGH);  // 使用卡片前先拉高 GPIO42
  delay(10);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    LOGF("[fail] SD init failed (no card / not FAT32 / bad contact)\n");
    sdPowerDown();
    return;
  }
  LOGF("SD ok, card size=%llu MB\n",
       (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));

  const char* path = "/bringup.txt";
  const char* msg = "esp32-eink bring-up test";
  if (SD.exists(path)) SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    LOGF("[fail] open for write\n");
    SD.end();
    sdPowerDown();
    return;
  }
  f.print(msg);
  f.close();

  f = SD.open(path, FILE_READ);
  if (!f) {
    LOGF("[fail] open for read\n");
    SD.end();
    sdPowerDown();
    return;
  }
  String got = f.readString();
  f.close();

  if (got == msg) {
    LOGF("write/read verify OK (%u bytes)\n", (unsigned)got.length());
  } else {
    LOGF("[fail] readback mismatch\n");
  }
  SD.remove(path);
  SD.end();
  sdPowerDown();
  LOGF("SD unmounted, GPIO42 low\n");
}

void cmdWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  LOGF("wifi scanning...\n");
  int n = WiFi.scanNetworks();
  LOGF("found %d networks\n", n);
  for (int i = 0; i < n; i++) {
    LOGF("%2d ch=%02d rssi=%4d %s\n",
         i, WiFi.channel(i), WiFi.RSSI(i), WiFi.SSID(i).c_str());
  }
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  LOGF("wifi off\n");
}

void cmdSleep() {
  LOGF("shutdown sequence starting\n");
  if (epdInitialized) {
    display.hibernate();      // controller 深休眠（含內部 BUSY 等待）
    epdInitialized = false;
  }
  pinMode(EPD_PWR, OUTPUT);    // 未跑過 'd' 時也確保腳位已設定
  digitalWrite(EPD_PWR, LOW);  // GPIO7 拉低
  WiFi.mode(WIFI_OFF);         // 射頻停用
  // SD 已在每次 's' 結束時卸載並關閉 GPIO42，此處無需處理
  btnPollingEnabled = false;
  LOGF("entering deep sleep, timer wake in 10 s\n");
  Serial.flush();
  delay(1000);
  sleepCount++;
  esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL);
  esp_deep_sleep_start();
}

// ---------- 主程式 ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    LOGF("wake from deep sleep (timer)\n");
    LOGF("sleepCount=%lu\n", (unsigned long)sleepCount);
  } else {
    LOGF("power-on/reset (cause=%d)\n", (int)cause);
  }
  for (auto& b : buttons) {
    pinMode(b.pin, INPUT_PULLUP);
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
  pollButtons();
  delay(10);
}
