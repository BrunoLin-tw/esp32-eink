# Bring-up 驗證韌體實作計畫

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立 PlatformIO 工程與單檔 serial 選單測試韌體，在實機上完成 display／partial／buttons／SD／Wi-Fi／sleep 全套週邊驗證並記錄數據。

**Architecture:** PlatformIO + Arduino framework，面板 class `GxEPD2_579_GDEY0579T93`。單一 `src/main.cpp` 以 serial 單鍵指令分派七個獨立測試函式；所有輸出帶 `millis()` 時間戳。硬體觀察步驟需使用者配合操作（看畫面、插卡、按鍵），代理人負責編譯、上傳與記錄。

**Tech Stack:** PlatformIO CLI、espressif32 platform、Arduino framework、GxEPD2 1.6.9（依賴 Adafruit GFX）、SD library、WiFi library。

**規格來源:** `docs/superpowers/specs/2026-08-24-bringup-verification-design.md`（已核准）

**驗證界線:** 無硬體步驟只允許 `pio run` 編譯驗證；標示「硬體檢查點」的步驟需要使用者實機操作與回報結果。不得宣稱未實際執行的驗證。

---

### Task 1: 安裝 PlatformIO 並建立工程

**Files:**
- Create: `platformio.ini`
- Create: `src/main.cpp`

- [ ] **Step 1: 安裝 PlatformIO CLI**

```bash
command -v pio || (command -v pipx >/dev/null && pipx install platformio) || python3 -m pip install --user platformio
pio --version
```

Expected: 印出版本（例如 `PlatformIO Core, version 6.x.x`）。若 `pipx` 與 `python3 -m pip` 都失敗，回報 BLOCKED，不得改用其他工具鏈。

- [ ] **Step 2: 建立 `platformio.ini`（初版）**

```ini
; esp32-eink bring-up 工程
; ELECROW CrowPanel DIS08792E：ESP32-S3-WROOM-1-N8R8，8 MB Flash，8 MB OPI PSRAM
[env:esp32eink]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.flash_mode = qio
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 8MB
board_build.partitions = default_8MB.csv
build_flags =
    -DBOARD_HAS_PSRAM
monitor_speed = 115200
monitor_filters = direct
upload_speed = 460800
lib_deps =
    zinggjm/GxEPD2@1.6.9
```

說明：`memory_type = qio_opi` 對應 N8R8 的 quad flash + octal PSRAM；`default_8MB.csv` 是 arduino-esp32 內建、涵蓋完整 8 MiB 的 partition layout，符合 AGENTS.md 不得只映射 4 MiB 的規則；GxEPD2 版本依研究文件核對值固定為 1.6.9。

- [ ] **Step 3: 建立最小 `src/main.cpp`**

```cpp
#include <Arduino.h>
#include <esp_sleep.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("bring-up skeleton");
}

void loop() {
  delay(100);
}
```

- [ ] **Step 4: 編譯**

Run: `pio run`
Expected: `SUCCESS`，無 error。首次會下載 toolchain，耗時較長屬正常。

- [ ] **Step 5: 核對並固定 platform／framework 版本**

Run: `pio pkg list -e esp32eink`
Expected: 列出 espressif32 platform、framework-arduinoespressif32 及 GxEPD2 1.6.9 的確切版本號。
將 Step 2 的 `platform = espressif32` 改為實際解析到的確切版本（如 `platform = espressif32@6.12.0`），重新執行 `pio run` 確認仍 SUCCESS。

- [ ] **Step 6: 更新規格文件的未定事項**

將 `docs/superpowers/specs/2026-08-24-bringup-verification-design.md` 的「未定事項」段落替換為：

```markdown
## 已定事項補記

- platform／framework pinned 版本（首次編譯成功後核對）：<填入 Task 1 Step 5 的實際版本號>。
```

- [ ] **Step 7: Commit**

```bash
git add platformio.ini src/main.cpp docs/superpowers/specs/2026-08-24-bringup-verification-design.md
git commit -m "建立 PlatformIO 工程與最小韌體骨架

platformio.ini 固定 GxEPD2 1.6.9 與 platform 版本；8 MiB partition；
qio_opi PSRAM 設定。pio run 編譯通過。

驗證等級：無硬體（僅編譯）。"
```

---

### Task 2: Serial 選單骨架與 `i` info 指令

**Files:**
- Modify: `src/main.cpp`（整檔替換為下方內容）

- [ ] **Step 1: 整檔替換 `src/main.cpp`**

```cpp
#include <Arduino.h>
#include <esp_sleep.h>

// ---------- 日誌 ----------
#define LOGF(...) do { \
  Serial.printf("[%010lu] ", (unsigned long)millis()); \
  Serial.printf(__VA_ARGS__); \
} while (0)

// ---------- 前向宣告 ----------
void cmdInfo();

// ---------- 指令分派表 ----------
struct TestCmd {
  char key;
  const char* name;
  void (*fn)();
};

TestCmd commands[] = {
  {'i', "info", cmdInfo},
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
```

- [ ] **Step 2: 編譯**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 3: 上傳（硬體檢查點）**

Run: `pio run -t upload`
Expected: 上傳成功。若下載模式失敗，依研究文件手動程序：按住 BOOT → 點按 RESET → 放開 RESET → 放開 BOOT → 重試上傳。上傳速度 460800 不穩時，把 `platformio.ini` 的 `upload_speed` 改為 `115200` 後重試，並記錄此異常。

- [ ] **Step 4: Serial 驗證（硬體檢查點）**

Run: `pio device monitor`
Expected 輸出：

```
[000000xxxx] power-on/reset (cause=0)
==== bring-up test menu ====
 i  info
```

輸入 `i`，Expected：

```
[000000xxxx] == start 'i' (info)
[000000xxxx] chip=ESP32-S3 rev=x cores=2
[000000xxxx] flash size=8192 KB speed=80 MHz
[000000xxxx] psram size=8388608 bytes (OK)
[000000xxxx] == end 'i'
```

若 PSRAM NOT FOUND 或 flash size 非 8192 KB：停止，回報 BLOCKED，檢查 `qio_opi` 設定。

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "加入 serial 選單骨架與 info 指令

實機確認晶片型號、8 MB Flash 與 8 MB OPI PSRAM 偵測。

驗證等級：有硬體（build/upload/serial，info 檢查通過）。"
```

---

### Task 3: 顯示器初始化與 clean full refresh（`d`）

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 加入顯示器定義與測試圖樣**

在 `#include <esp_sleep.h>` 之後加入：

```cpp
#include <SPI.h>
#include <GxEPD2_BW.h>

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
    display.fillRect(20, 20, 320, 64, GxEPD_WHITE);
    display.drawRect(20, 20, 320, 64, GxEPD_BLACK);
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
```

- [ ] **Step 2: 加入 `cmdDisplay()` 並註冊**

在 `commands[]` 的 `{'i', "info", cmdInfo},` 後加入一行：

```cpp
  {'d', "display", cmdDisplay},
```

在前向宣告區加入 `void cmdDisplay();`，並在 `cmdInfo()` 之後加入：

```cpp
void cmdDisplay() {
  epdPowerOn();
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.epd2.setBusyTimeout(15000000);  // BUSY 安全上限 15 s（microseconds）
  uint32_t t0 = millis();
  display.init(115200, true, 2, false);   // init 含第一次 clean full refresh
  LOGF("init + clean full refresh: %lu ms\n", (unsigned long)(millis() - t0));
  t0 = millis();
  drawTestPattern();  // firstPage/nextPage 迴圈結束時即完成刷新；不進 hibernate，供後續測試使用
  LOGF("test pattern refresh: %lu ms\n", (unsigned long)(millis() - t0));
  epdInitialized = true;
}
```

- [ ] **Step 3: 編譯**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 4: 上傳並驗證（硬體檢查點）**

Run: `pio run -t upload && pio device monitor`
輸入 `d`，Expected：
- serial 出現兩行耗時紀錄（clean refresh 與圖樣 refresh，各約 2–4 秒量級）
- 螢幕出現黑白格線＋文字框，無錯位、無接縫異常、無殘影

若 BUSY timeout（15 秒逾時）或畫面錯位：停止，回報 BLOCKED 並附完整 serial log。

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "加入顯示器初始化與 clean full refresh 測試

GxEPD2_579_GDEY0579T93，GPIO7 電源控制，格線＋文字框測試圖樣，
記錄刷新耗時。

驗證等級：有硬體（build/upload/serial/full refresh 通過）。"
```

---

### Task 4: 局部刷新循環（`p`）

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 加入 `drawCounter()` 與 `cmdPartial()` 並註冊**

在 `{'d', "display", cmdDisplay},` 後加入：

```cpp
  {'p', "partial", cmdPartial},
```

加入前向宣告 `void cmdPartial();`，並在 `cmdDisplay()` 之後加入：

```cpp
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
```

注意（來自研究文件）：GxEPD2 局部刷新只限制資料寫入範圍，waveform 刷新仍是 controller 層級，因此單次耗時不會隨視窗面積等比縮短——預期每次約數百毫秒。

- [ ] **Step 2: 編譯**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 3: 上傳並驗證（硬體檢查點）**

Run: `pio run -t upload && pio device monitor`
依序輸入 `d`、`p`，Expected：
- 計數器視窗逐次更新至 `#30`，中途第 10、20 次後插入全刷
- serial 記錄每次耗時與批次總耗時
- 批次結束後無嚴重殘影（第 30 次後緊接一次全刷由使用者另行輸入 `p` 前先輸入 `d` 觀察亦可）

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "加入局部刷新批次測試

30 次 partial、每 10 次插入 full refresh，逐次計時。

驗證等級：有硬體（partial refresh 序列通過）。"
```

---

### Task 5: 按鍵測試（`b`）

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 加入按鍵定義與輪詢邏輯**

在顯示器腳位定義之後加入：

```cpp
// 按鍵（active-low，docs/device-research.md）
#define BTN_MENU  2
#define BTN_EXIT  1
#define BTN_UP    6
#define BTN_DOWN  4
#define BTN_PRESS 5

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
```

- [ ] **Step 2: 初始化腳位、註冊指令、掛進 loop()**

`setup()` 中 `printMenu();` 之前加入：

```cpp
  for (auto& b : buttons) {
    pinMode(b.pin, INPUT_PULLUP);
  }
```

`commands[]` 加入：

```cpp
  {'b', "buttons", cmdButtons},
```

加入前向宣告 `void cmdButtons();` 與函式本體：

```cpp
void cmdButtons() {
  btnPollingEnabled = !btnPollingEnabled;
  LOGF("button polling %s (run 'b' again to toggle off)\n",
       btnPollingEnabled ? "ON" : "OFF");
}
```

`loop()` 中 `delay(10);` 之前加入：

```cpp
  pollButtons();
```

- [ ] **Step 3: 編譯**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 4: 上傳並驗證（硬體檢查點）**

Run: `pio run -t upload && pio device monitor`
輸入 `b` 開啟輪詢，逐顆按下／放開 MENU、EXIT、撥桿上下與撥桿下壓，Expected：
- 每顆鍵的 pressed/released 都正確回報且名稱正確
- 無重複事件、無漏報

完成後再輸入 `b` 關閉輪詢。

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "加入五顆按鍵測試（active-low 輪詢＋debounce）

驗證等級：有硬體（按鍵逐一通過）。"
```

---

### Task 6: microSD 讀寫測試（`s`）

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 加入 SD 定義與測試函式**

在 include 區加入：

```cpp
#include <SD.h>
```

在按鍵定義之後加入：

```cpp
// microSD 腳位（docs/device-research.md）；與顯示器使用不同 SPI 匯流排
#define SD_CS   10
#define SD_SCK  39
#define SD_MISO 13
#define SD_MOSI 40
#define SD_PWR  42

SPIClass sdSPI(HSPI);
```

- [ ] **Step 2: 加入 `cmdSd()` 並註冊**

`commands[]` 加入：

```cpp
  {'s', "sd", cmdSd},
```

加入前向宣告與函式本體：

```cpp
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
```

注意：SD 卡必須事先格式化為 FAT32（SD library 不支援 exFAT；建議 ≤32 GB 卡）。

- [ ] **Step 3: 編譯**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 4: 上傳並驗證（硬體檢查點）**

Run: `pio run -t upload && pio device monitor`
插入 FAT32 卡後輸入 `s`，Expected：

```
[...] SD ok, card size=xxxx MB
[...] write/read verify OK (24 bytes)
[...] SD unmounted, GPIO42 low
```

拔卡後再輸入 `s`，Expected：`[fail] SD init failed` 且不影響選單運作。兩項都通過才算過關。

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "加入 microSD 讀寫測試

GPIO42 電源控制、獨立 HSPI 匯流排、寫讀比對、卸載後斷電；
含拔卡失敗路徑驗證。

驗證等級：有硬體（插卡讀寫＋拔卡失敗路徑通過）。"
```

---

### Task 7: Wi-Fi 掃描（`w`）

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 加入掃描函式並註冊**

include 區加入：

```cpp
#include <WiFi.h>
```

`commands[]` 加入：

```cpp
  {'w', "wifi", cmdWifi},
```

加入前向宣告與函式本體：

```cpp
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
```

注意：SSID 屬於環境資訊，若要把 serial log 存入 repo，先遮蔽 SSID 欄位，符合 AGENTS.md 密鑰規則精神。

- [ ] **Step 2: 編譯**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 3: 上傳並驗證（硬體檢查點）**

Run: `pio run -t upload && pio device monitor`
輸入 `w`，Expected：列出環境中可見網路的 channel/RSSI/SSID，結尾出現 `wifi off`。掃描期間顯示器等其他功能不受影響。

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "加入 Wi-Fi 掃描測試

STA 掃描後停用射頻；不設定任何憑證。

驗證等級：有硬體（掃描＋射頻關閉通過）。"
```

---

### Task 8: Deep sleep 與喚醒（`z`）

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 加入 RTC 變數**

在 `bool epdInitialized = false;` 之後加入：

```cpp
RTC_DATA_ATTR uint32_t sleepCount = 0;  // 跨 deep sleep 保存
```

- [ ] **Step 2: 加入 `cmdSleep()` 並註冊**

`commands[]` 加入：

```cpp
  {'z', "sleep", cmdSleep},
```

加入前向宣告與函式本體：

```cpp
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
```

- [ ] **Step 3: 確認喚醒路徑輸出**

Task 2 的 `setup()` 已印出 `wake from deep sleep (timer)`。在其中追加一行（`LOGF("wake from...")` 之後）：

```cpp
    LOGF("sleepCount=%lu\n", (unsigned long)sleepCount);
```

- [ ] **Step 4: 編譯**

Run: `pio run`
Expected: `SUCCESS`

- [ ] **Step 5: 上傳並驗證（硬體檢查點）**

Run: `pio run -t upload && pio device monitor`
依序輸入 `d`（先讓顯示器初始化）、`z`，Expected：
- serial 印 shutdown 序列後靜默
- 約 10 秒後自動重開機，印出 `wake from deep sleep (timer)` 與 `sleepCount=1`
- 顯示器處於斷電於斷電狀態（畫面維持最後影像屬電子紙特性，正常）

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "加入 deep sleep 與 timer 喚醒測試

hibernate → GPIO7 低 → Wi-Fi off → deep sleep；RTC memory 記錄
sleepCount，喚醒後列印 wake reason。

驗證等級：有硬體（sleep/wake 循環通過）。"
```

---

### Task 9: 量測記錄與收尾

**Files:**
- Modify: `docs/device-research.md`
- Modify: `docs/superpowers/specs/2026-08-24-bringup-verification-design.md`（若 Task 1 未完成版本補記）

- [ ] **Step 1: 整理 serial log 中的量測數據**

從各硬體檢查點的 serial log 彙整：
- clean full refresh 耗時（Task 3）
- 圖樣 full refresh 耗時（Task 3）
- 每次 partial 耗時與 30 次批次總耗時（Task 4）
- 上傳速度是否穩定在 460800（Task 2；若曾降速，記錄異常）
- 任何異常事件（BUSY timeout、下載模式失敗、SD 失敗次數等）

- [ ] **Step 2: 寫入 `docs/device-research.md` 新增小節**

在文件末尾加入（數值以實際 log 取代 `<...>`）：

```markdown
## 實機 bring-up 量測（2026-08-24）

量測方法：PlatformIO + `main.cpp` serial 選單測試韌體（見
`docs/superpowers/specs/2026-08-24-bringup-verification-design.md`），
時間戳取自 `millis()`，USB-C 經 CH340C，上傳速度 <460800/115200>。

| 項目 | 結果 |
| --- | --- |
| init + clean full refresh | <xxx> ms |
| 測試圖樣 full refresh | <xxx> ms |
| partial 單次耗時範圍 | <xxx–xxx> ms |
| 30 次 partial 批次總耗時 | <xxx> ms |
| 按鍵（5 顆） | 全部通過／<異常> |
| microSD 讀寫 | 通過（<容量> MB 卡）／<異常> |
| Wi-Fi 掃描 | 找到 <n> 個網路，射頻可正常關閉 |
| deep sleep → timer 喚醒 | 通過，sleepCount 正確累計 |

異常事件：<無／條列>
```

- [ ] **Step 3: 最終一致性檢查**

Run: `rg -n 'TBD|TODO|PLACEHOLDER|待補' src/main.cpp docs/superpowers/specs/2026-08-24-bringup-verification-design.md docs/superpowers/plans/2026-08-24-bringup-verification.md`
Expected: exit code 1（無匹配）

Run: `pio run`
Expected: 最終版仍編譯成功。

- [ ] **Step 4: Commit**

```bash
git add docs/device-research.md docs/superpowers/specs/2026-08-24-bringup-verification-design.md
git commit -m "記錄 bring-up 實機量測數據

full/partial 刷新耗時、上傳速度、週邊驗證結果與異常事件，
附量測方法說明。

驗證等級：有硬體（全套六步＋週邊驗證完成）。"
```
