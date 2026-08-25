# 天氣看板實作計畫

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在已驗證的硬體上建立四地點 Open-Meteo 天氣看板：30 分鐘睡眠週期自動更新、撥桿切換地點、離線短睡眠重試。

**Architecture:** 模組化多檔——`main.cpp` 只做狀態機與電源管理，`weather.*` 負責 Wi-Fi/NTP/Open-Meteo，`ui.*` 負責渲染，`locations.h`/`icons` 為資料。地點選擇存 NVS（Preferences）。deep sleep 以 timer 30 分＋EXT1（撥桿下壓 GPIO5）雙喚醒源。

**Tech Stack:** PlatformIO（既有工程）、Arduino framework、GxEPD2 1.6.9（既有）、ArduinoJson v7（新增）、Preferences、HTTPClient + WiFiClientSecure。

**規格來源:** `docs/superpowers/specs/2026-08-25-weather-station-design.md`（已核准）

**執行注意事項:**
- PlatformIO 二進位路徑：`/tmp/opencode/pio-venv/bin/pio`
- 使用者偏好逐步驗證：每個標「硬體檢查點」的步驟需停下請使用者上傳與回報，驗證通過後才 commit
- 編譯用本地 `src/secrets.h`（gitignored）可先填假憑證 `dummy`／`dummy`，僅供編譯，絕不 commit；使用者須自行填入真實憑證後才能連線成功
- 設計修訂（對 spec 的偏離，已確認方向）：天氣圖示以 **GFX 幾何繪製函式**（圓、弧、線段）取代 PROGMEM 點陣——視覺意圖相同、省 flash、免轉檔工具

---

### Task 1: 憑證機制、地點表、ArduinoJson 依賴與工程重整

**Files:**
- Create: `src/log.h`
- Create: `src/secrets.h.example`
- Create: `src/locations.h`
- Modify: `platformio.ini`（lib_deps 新增 ArduinoJson）
- Modify: `src/main.cpp`（改為天氣 app 最小骨架）

- [ ] **Step 1: 打標籤保留 bring-up 版本**

```bash
git tag bringup-v1
```

- [ ] **Step 2: 建立 `src/log.h`**

```cpp
#pragma once
#include <Arduino.h>

#define LOGF(...) do { \
  Serial.printf("[%010lu] ", (unsigned long)millis()); \
  Serial.printf(__VA_ARGS__); \
} while (0)
```

- [ ] **Step 3: 建立 `src/secrets.h.example`**

```cpp
// 複製本檔為同目錄的 secrets.h 並填入實際值。
// secrets.h 已被 .gitignore 排除，不得 commit。
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
```

- [ ] **Step 4: 建立 `src/locations.h`**

```cpp
#pragma once
#include <pgmspace.h>

struct Location {
  const char* name;
  float lat;
  float lon;
};

const Location LOCATIONS[] = {
  {"Banqiao",       25.0133f, 121.4619f},
  {"Dali Taichung", 24.1016f, 120.6825f},
  {"Sapporo",       43.0618f, 141.3545f},
  {"San Francisco", 37.7749f, -122.4194f},
};
const int LOCATION_COUNT = sizeof(LOCATIONS) / sizeof(LOCATIONS[0]);
const int DEFAULT_LOCATION = 0;
```

- [ ] **Step 5: 修改 `platformio.ini`**

lib_deps 區段改為：

```ini
lib_deps =
    zinggjm/GxEPD2@1.6.9
    bblanchon/ArduinoJson@^7
```

- [ ] **Step 6: 改寫 `src/main.cpp` 為最小骨架**

```cpp
#include <Arduino.h>
#include "log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "缺少 src/secrets.h：複製 secrets.h.example 並填入 Wi-Fi 憑證"
#endif

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("weather station skeleton, ssid set=%s\n",
       strlen(WIFI_SSID) > 0 ? "yes" : "no");
}

void loop() {
  delay(100);
}
```

- [ ] **Step 7: 建立本地編譯用假憑證檔（絕不 commit）**

```bash
printf '#define WIFI_SSID "dummy"\n#define WIFI_PASS "dummy"\n' > src/secrets.h
git check-ignore src/secrets.h && echo IGNORED || echo ERROR-NOT-IGNORED
```

Expected: `IGNORED`。若非 IGNORED，停止修正 `.gitignore`。

- [ ] **Step 8: 編譯並固定版本**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS（首次會下載 ArduinoJson）。

Run: `/tmp/opencode/pio-venv/bin/pio pkg list -e esp32eink`
把 `bblanchon/ArduinoJson@^7` 改為解析到的確切版本（如 `@7.4.2`），重跑 `pio run` 確認 SUCCESS。

- [ ] **Step 9: 更新規格未定事項**

`docs/superpowers/specs/2026-08-25-weather-station-design.md` 的「未定事項」段落替換為：

```markdown
## 已定事項補記

- ArduinoJson pinned 版本（首次編譯成功後核對）：<填入實際版本號>。
```

- [ ] **Step 10: Commit**

```bash
git add src/log.h src/secrets.h.example src/locations.h src/main.cpp platformio.ini docs/superpowers/specs/2026-08-25-weather-station-design.md
git commit -m "天氣看板：憑證機制、地點表與工程骨架

secrets.h.example 範本＋__has_include 防呆；四地點表；
新增 ArduinoJson（版本於首次編譯核對）。bring-up 版本已打標籤
bringup-v1。

驗證等級：無硬體（僅編譯）。"
```

---

### Task 2: Wi-Fi 連線模組

**Files:**
- Create: `src/weather.h`
- Create: `src/weather.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: 建立 `src/weather.h`**

```cpp
#pragma once
#include <Arduino.h>

// 連線 Wi-Fi（阻塞至成功或逾時）。回傳是否成功。
bool wifiConnect(uint32_t timeoutMs);

void wifiOff();
```

- [ ] **Step 2: 建立 `src/weather.cpp`**

```cpp
#include "weather.h"
#include "log.h"
#include "secrets.h"
#include <WiFi.h>

bool wifiConnect(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  LOGF("wifi connecting...\n");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(100);
    yield();
  }
  if (WiFi.status() == WL_CONNECTED) {
    LOGF("wifi connected, ip=%s rssi=%d %lu ms\n",
         WiFi.localIP().toString().c_str(), WiFi.RSSI(),
         (unsigned long)(millis() - t0));
    return true;
  }
  LOGF("[fail] wifi connect timeout (%lu ms)\n",
       (unsigned long)(millis() - t0));
  return false;
}

void wifiOff() {
  WiFi.mode(WIFI_OFF);
  LOGF("wifi off\n");
}
```

- [ ] **Step 3: 修改 `src/main.cpp`**

include 區加入：

```cpp
#include "weather.h"
```

setup() 中 `LOGF("weather station skeleton...")` 之後加入：

```cpp
  wifiConnect(15000);
```

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

請使用者先把 `src/secrets.h` 填入真實憑證（提醒：該檔不會被 commit），然後：

Run: `/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor`
Expected：

```
[...] weather station skeleton, ssid set=yes
[...] wifi connecting...
[...] wifi connected, ip=x.x.x.x rssi=-xx xxx ms
```

- [ ] **Step 6: Commit**

```bash
git add src/weather.h src/weather.cpp src/main.cpp
git commit -m "天氣看板：Wi-Fi 連線模組

15 秒逾時、RSSI 與耗時回報；不輸出任何憑證內容。

驗證等級：有硬體（Wi-Fi 實連通過）。"
```

---

### Task 3: NTP 對時與當地時間換算

**Files:**
- Modify: `src/weather.h`
- Modify: `src/weather.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: `weather.h` 加入宣告**

在 `void wifiOff();` 之後加入：

```cpp
#include <time.h>

// 以 NTP 同步 UTC 時間（阻塞至成功或逾時）。回傳是否成功。
bool syncClock(uint32_t timeoutMs);

// 取得「加上位移量」的當地時間（呼叫端傳入 utc_offset_seconds）。
// 回傳 false 表示時鐘尚未同步。
bool localTime(long utcOffsetSec, struct tm* out);
```

- [ ] **Step 2: `weather.cpp` 加入實作**

include 區加入：

```cpp
#include "locations.h"
```

檔尾加入：

```cpp
bool syncClock(uint32_t timeoutMs) {
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  uint32_t t0 = millis();
  while (time(nullptr) < 100000 && millis() - t0 < timeoutMs) {
    delay(100);
    yield();
  }
  if (time(nullptr) < 100000) {
    LOGF("[fail] ntp sync timeout\n");
    return false;
  }
  LOGF("ntp synced in %lu ms\n", (unsigned long)(millis() - t0));
  return true;
}

bool localTime(long utcOffsetSec, struct tm* out) {
  time_t nowUtc = time(nullptr);
  if (nowUtc < 100000) return false;
  time_t local = nowUtc + utcOffsetSec;
  gmtime_r(&local, out);
  return true;
}
```

說明：`configTzTime` 在 Wi-Fi 連線後才有效；統一以 UTC 取時，再加各城市
`utc_offset_seconds` 換算，避免重設 timezone 造成快取問題。

- [ ] **Step 3: 修改 `src/main.cpp`**

setup() 的 `wifiConnect(15000);` 之後加入：

```cpp
  if (syncClock(10000)) {
    struct tm tmLoc;
    // 板橋位移量暫代，Task 4 起改用 API 回傳值
    if (localTime(8 * 3600L, &tmLoc)) {
      LOGF("local time: %04d-%02d-%02d %02d:%02d:%02d\n",
           tmLoc.tm_year + 1900, tmLoc.tm_mon + 1, tmLoc.tm_mday,
           tmLoc.tm_hour, tmLoc.tm_min, tmLoc.tm_sec);
    }
  }
```

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

Run: `/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor`
Expected：`ntp synced in xxx ms` 與正確的台灣當地時間（分秒合理、日期正確）。

- [ ] **Step 6: Commit**

```bash
git add src/weather.h src/weather.cpp src/main.cpp
git commit -m "天氣看板：NTP 對時與當地時間換算

UTC 取時再以 utc_offset_seconds 換算，支援多時區地點。

驗證等級：有硬體（NTP 同步與時間顯示通過）。"
```

---

### Task 4: Open-Meteo 抓取與 JSON 解析

**Files:**
- Modify: `src/weather.h`
- Modify: `src/weather.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: `weather.h` 加入資料結構與介面**

在 `bool localTime(...)` 之後加入：

```cpp
#include <Arduino.h>

struct HourPoint {
  float temp;
  int precipProb;
  int code;
};

struct WeatherData {
  bool valid;
  float temp;      // 目前溫度
  int humidity;    // 相對濕度 %
  int windKmh;     // 風速 km/h
  int code;        // 目前 WMO 天氣碼
  long utcOffsetSec;
  HourPoint hours[6];  // 下一個整點起 6 小時
};

// 抓取並解析指定地點天氣；失敗時 data->valid = false。
bool fetchWeather(float lat, float lon, WeatherData* data);

// 列出資料內容供人工核對（serial log）。
void dumpWeather(const WeatherData& d);
```

- [ ] **Step 2: `weather.cpp` 加入實作**

include 區加入：

```cpp
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
```

檔尾加入：

```cpp
static bool buildUrl(const Location& loc, String& url) {
  url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude=" + String(loc.lat, 4);
  url += "&longitude=" + String(loc.lon, 4);
  url += "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m";
  url += "&hourly=temperature_2m,precipitation_probability,weather_code";
  url += "&forecast_days=1&timezone=auto";
  return true;
}

bool fetchWeather(float lat, float lon, WeatherData* data) {
  data->valid = false;
  Location tmp = {"query", lat, lon};
  String url;
  buildUrl(tmp, url);

  WiFiClientSecure client;
  client.setInsecure();  // 不驗憑證鏈：單機裝置取公開資料的權衡（見 spec）
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    LOGF("[fail] http begin\n");
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    LOGF("[fail] http GET code=%d\n", code);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  LOGF("http ok, payload %u bytes\n", (unsigned)payload.length());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    LOGF("[fail] json parse: %s\n", err.c_str());
    return false;
  }

  data->temp = doc["current"]["temperature_2m"] | NAN;
  data->humidity = doc["current"]["relative_humidity_2m"] | -1;
  data->code = doc["current"]["weather_code"] | -1;
  data->windKmh = (int)(doc["current"]["wind_speed_10m"] | (float)-1);
  data->utcOffsetSec = doc["utc_offset_seconds"] | 0L;

  // 找出「下一個整點」起 6 格的索引：本地小時 + 1，上限 23-5
  struct tm tmLoc;
  localTime(data->utcOffsetSec, &tmLoc);
  int startH = tmLoc.tm_hour + 1;
  if (startH > 18) startH = 18;  // 一日資料最多取到 18..23
  for (int i = 0; i < 6; i++) {
    int idx = startH + i;
    data->hours[i].temp = doc["hourly"]["temperature_2m"][idx] | NAN;
    data->hours[i].precipProb = doc["hourly"]["precipitation_probability"][idx] | -1;
    data->hours[i].code = doc["hourly"]["weather_code"][idx] | -1;
  }
  data->valid = !isnan(data->temp) && data->code >= 0;
  return data->valid;
}

void dumpWeather(const WeatherData& d) {
  if (!d.valid) {
    LOGF("weather invalid\n");
    return;
  }
  LOGF("now: %.1fC rh=%d%% wind=%dkm/h code=%d offset=%lds\n",
       d.temp, d.humidity, d.windKmh, d.code, d.utcOffsetSec);
  struct tm tmLoc;
  localTime(d.utcOffsetSec, &tmLoc);
  int startH = tmLoc.tm_hour + 1;
  if (startH > 18) startH = 18;
  for (int i = 0; i < 6; i++) {
    LOGF("+%dh %02d:00 %.1fC rain=%d%% code=%d\n",
         i + 1, (startH + i) % 24,
         d.hours[i].temp, d.hours[i].precipProb, d.hours[i].code);
  }
}
```

- [ ] **Step 3: 修改 `src/main.cpp`**

include 區加入：

```cpp
#include "locations.h"
```

setup() 中 NTP 區塊之後加入：

```cpp
  const Location& loc = LOCATIONS[DEFAULT_LOCATION];
  WeatherData data;
  if (fetchWeather(loc.lat, loc.lon, &data)) {
    dumpWeather(data);
  }
```

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

Run: `/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor`
Expected：`http ok, payload xxxx bytes`、目前溫度/濕度/風速/WMO 碼，與 6 格逐時預報。
使用者對照手機天氣 App 人工核對數值合理性（溫差 ±3°C 內屬正常範圍）。

- [ ] **Step 6: Commit**

```bash
git add src/weather.h src/weather.cpp src/main.cpp
git commit -m "天氣看板：Open-Meteo 抓取與 JSON 解析

current 四項＋逐時 6 格（溫度/降雨率/天氣碼），utc_offset_seconds
驅動多時區。HTTPS 採 setInsecure 權衡（公開資料、單機裝置）。

驗證等級：有硬體（實抓資料並人工核對通過）。"
```

---

### Task 5: UI 渲染骨架（版面＋文字，圖示留空位）

**Files:**
- Create: `src/ui.h`
- Create: `src/ui.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: 建立 `src/ui.h`**

```cpp
#pragma once
#include <Arduino.h>
#include "weather.h"
#include "locations.h"

// GPIO7 上電、SPI/display init（含首次 clean full refresh）。
void uiPowerOnInit();

// 渲染完整看板並 full refresh。
void uiRenderDashboard(const Location& loc, const WeatherData& d);

// 錯誤畫面（保留上次資料由呼叫端另行處理；此處顯示訊息）。
void uiShowOffline(const char* reason);

// hibernate controller（deep sleep 前呼叫）。
void uiHibernate();
```

- [ ] **Step 2: 建立 `src/ui.cpp`**

```cpp
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
    display.drawRect(600, 24, 96, 96, GxEPD_BLACK);

    // 目前狀態：大字溫度
    snprintf(buf, sizeof(buf), "%.1f\xB0" "C", d.temp);  // \xB0 = °(latin-1)
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
      snprintf(buf, sizeof(buf), "%.0f\xB0 %d%%",
               d.hours[i].temp, d.hours[i].precipProb);
      drawText(cx + 12, 236, 3, buf);
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
```

- [ ] **Step 3: 修改 `src/main.cpp`**

include 區加入：

```cpp
#include "ui.h"
```

setup() 改為（fetch 成功後渲染；失敗顯示離線畫面）：

```cpp
void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("weather station boot\n");

  uiPowerOnInit();

  if (!wifiConnect(15000)) {
    uiShowOffline("wifi failed");
    return;  // Task 8 接手 sleep
  }
  syncClock(10000);

  const Location& loc = LOCATIONS[DEFAULT_LOCATION];
  WeatherData data;
  if (fetchWeather(loc.lat, loc.lon, &data)) {
    dumpWeather(data);
    uiRenderDashboard(loc, data);
  } else {
    uiShowOffline("fetch failed");
  }
}
```

原 `dumpWeather` 呼叫前的 NTP 區塊移除（時間顯示已併入 ui 與 fetch 流程）。

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

Run: `/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor`
Expected：畫面出現 Banqiao、當地時間、大字溫度、RH/Wind 列、分隔線與 6 格文字版預報；右上有一個空圖示框。

- [ ] **Step 6: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "天氣看板：UI 渲染骨架

版面文字全到位，圖示區留框待 Task 6；full refresh 更新。

驗證等級：有硬體（版面渲染通過）。"
```

---

### Task 6: 天氣圖示（幾何繪製＋WMO 映射）

**Files:**
- Create: `src/icons.h`
- Create: `src/icons.cpp`
- Modify: `src/ui.cpp`

設計修訂落實：以幾何繪製取代 PROGMEM 點陣（見計畫開頭注意事項）。

- [ ] **Step 1: 建立 `src/icons.h`**

```cpp
#pragma once
#include <Arduino.h>

enum class IconId : uint8_t {
  SUN, PARTLY, CLOUDY, FOG, DRIZZLE, RAIN, SNOW, SHOWERS, SLEET, THUNDER
};

// WMO weather_code -> IconId（涵蓋 spec 列出的全部碼段）
IconId iconForCode(int wmo);

// 在 (x,y) 為左上角、邊長 size 的方形內繪製圖示（黑白）。
void drawIcon(int x, int y, int size, IconId id);
```

- [ ] **Step 2: 建立 `src/icons.cpp`**

所有繪製使用 Adafruit GFX 呼叫（drawCircle/drawLine/fillCircle/
fillTriangle 等），全部參數化於 size，黑白兩色：

```cpp
#include "icons.h"
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

namespace {

void sun(int x, int y, int s) {
  int cx = x + s / 2, cy = y + s / 2, r = s / 5;
  display.drawCircle(cx, cy, r, GxEPD_BLACK);
  for (int a = 0; a < 8; a++) {
    float ang = a * PI_4;
    int x1 = cx + cos(ang) * (r + s / 12), y1 = cy + sin(ang) * (r + s / 12);
    int x2 = cx + cos(ang) * (r + s / 4),  y2 = cy + sin(ang) * (r + s / 4);
    display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
  }
}

void cloudShape(int x, int y, int s) {  // 共用雲輪廓：三弧＋底線
  int cy = y + s * 0.62, r = s * 0.22;
  display.drawCircle(x + s * 0.35, cy, r, GxEPD_BLACK);
  display.drawCircle(x + s * 0.58, cy - s * 0.08, s * 0.28, GxEPD_BLACK);
  display.drawCircle(x + s * 0.78, cy, r, GxEPD_BLACK);
  display.drawLine(x + s * 0.14, cy + r, x + s * 0.98, cy + r, GxEPD_BLACK);
  // 清除雲內部線條使其成封閉外觀
  display.fillRect(x + s * 0.36, cy - s * 0.05, s * 0.42, r, GxEPD_WHITE);
  display.drawLine(x + s * 0.14, cy + r, x + s * 0.98, cy + r, GxEPD_BLACK);
}

void drops(int x, int y, int s, int count) {
  for (int i = 0; i < count; i++) {
    int dx = x + s * (0.30 + i * 0.20);
    display.drawLine(dx, y + s * 0.72, dx - s * 0.06, y + s * 0.88, GxEPD_BLACK);
  }
}

}  // namespace

void drawIcon(int x, int y, int size, IconId id) {
  switch (id) {
    case IconId::SUN:
      sun(x, y, size);
      break;
    case IconId::PARTLY:
      sun(x + size * 0.15, y, size * 0.55);
      cloudShape(x, y + size * 0.25, size * 0.85);
      break;
    case IconId::CLOUDY:
      cloudShape(x, y + size * 0.15, size);
      break;
    case IconId::FOG:
      cloudShape(x, y, size * 0.9);
      for (int i = 0; i < 3; i++) {
        display.drawLine(x + size * 0.15, y + size * (0.75 + i * 0.09),
                         x + size * 0.85, y + size * (0.75 + i * 0.09),
                         GxEPD_BLACK);
      }
      break;
    case IconId::DRIZZLE:
      cloudShape(x, y, size * 0.9);
      drops(x, y, size * 0.9, 2);
      break;
    case IconId::RAIN:
      cloudShape(x, y, size * 0.9);
      drops(x, y, size * 0.9, 3);
      break;
    case IconId::SHOWERS:
      cloudShape(x, y, size * 0.9);
      drops(x, y, size * 0.9, 4);
      break;
    case IconId::SNOW:
    case IconId::SLEET:
      cloudShape(x, y, size * 0.9);
      for (int i = 0; i < 3; i++) {
        int dx = x + size * (0.32 + i * 0.18);
        display.drawCircle(dx, y + size * 0.82, size * 0.04, GxEPD_BLACK);
      }
      break;
    case IconId::THUNDER:
      cloudShape(x, y, size * 0.85);
      display.fillTriangle(x + size * 0.52, y + size * 0.60,
                           x + size * 0.38, y + size * 0.82,
                           x + size * 0.56, y + size * 0.82, GxEPD_BLACK);
      display.fillTriangle(x + size * 0.56, y + size * 0.82,
                           x + size * 0.44, y + size * 1.00,
                           x + size * 0.66, y + size * 0.86, GxEPD_BLACK);
      break;
  }
}
```

- [ ] **Step 3: 修改 `src/ui.cpp` 接入圖示**

include 區加入：

```cpp
#include "icons.h"
```

`uiRenderDashboard()` 中：

- 把 `display.drawRect(600, 24, 96, 96, GxEPD_BLACK);` 替換為：

```cpp
    drawIcon(600, 24, 96, iconForCode(d.code));
```

- 6 格迴圈內，`drawText(cx + 12, 208, 2, buf);` 之後加入：

```cpp
      drawIcon(cx + cellW - 44, 204, 32, iconForCode(d.hours[i].code));
```

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

Run: `/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor`
Expected：右上出現對應目前天氣的圖示；下方 6 格各有小圖示；圖形無越界、無破圖。serial 可比對 `code=` 值與畫面圖示是否符合映射表。

- [ ] **Step 6: Commit**

```bash
git add src/icons.h src/icons.cpp src/ui.cpp
git commit -m "天氣看板：幾何繪製天氣圖示與 WMO 映射

10 種圖示參數化繪製（sun/cloud/rain/snow/thunder 等），
取代原 spec 的 PROGMEM 點陣方案（設計修訂已註明）。

驗證等級：有硬體（圖示渲染通過）。"
```

---

### Task 7: NVS 地點記憶與 awake 模式互動

**Files:**
- Modify: `src/ui.h`
- Modify: `src/ui.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: `ui.h` 加入 awake 相關介面**

在 `void uiHibernate();` 之後加入：

```cpp
#include <Preferences.h>

// awake 模式提示條：partial refresh 顯示所選地點名與操作說明。
void uiAwakeHint(const char* name);

// 清除 awake 提示（partial refresh 恢復原狀：重繪一次 dashboard 底層）。
void uiClearHint();
```

- [ ] **Step 2: `ui.cpp` 加入實作**

include 區加入：

```cpp
#include "log.h"
```

檔尾加入：

```cpp
void uiAwakeHint(const char* name) {
  char buf[64];
  snprintf(buf, sizeof(buf), "[awake] UP/DOWN: %s  PRESS: refresh", name);
  display.setPartialWindow(0, display.height() - 24, display.width(), 24);
  display.firstPage();
  do {
    display.fillRect(0, display.height() - 24, display.width(), 24, GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setTextSize(1);
    display.setCursor(8, display.height() - 12);
    display.print(buf);
  } while (display.nextPage());
}

void uiClearHint() {
  // 重繪主畫面底層覆蓋提示條（partial window 同尺寸）
  display.setPartialWindow(0, display.height() - 24, display.width(), 24);
  display.firstPage();
  do {
    display.fillRect(0, display.height() - 24, display.width(), 24, GxEPD_WHITE);
    display.drawFastHLine(0, display.height() - 24, display.width(), GxEPD_BLACK);
  } while (display.nextPage());
}
```

- [ ] **Step 3: 修改 `src/main.cpp`**

include 區加入：

```cpp
#include <Preferences.h>
#include "log.h"

// 按鍵（active-low）
#define BTN_UP    6
#define BTN_DOWN  4
#define BTN_PRESS 5
```

檔案層級加入（在 setup 之前）：

```cpp
static int loadLocationIdx() {
  Preferences prefs;
  prefs.begin("weather", true);
  int idx = prefs.getInt("loc", DEFAULT_LOCATION);
  prefs.end();
  if (idx < 0 || idx >= LOCATION_COUNT) idx = DEFAULT_LOCATION;
  return idx;
}

static void saveLocationIdx(int idx) {
  Preferences prefs;
  prefs.begin("weather", false);
  prefs.putInt("loc", idx);
  prefs.end();
}

// awake 模式：撥桿上下選地點、下壓刷新；idleMs 無操作即返回。
// 回傳使用者最終確認的地點索引。
static int awakeLoop(int curIdx, uint32_t idleTimeoutMs) {
  static BtnDef awButtons[] = {
    {"UP", BTN_UP}, {"DOWN", BTN_DOWN}, {"PRESS", BTN_PRESS},
  };
  bool last[3] = {true, true, true};
  uint32_t lastAct = millis();
  uiAwakeHint(LOCATIONS[curIdx].name);
  while (true) {
    if (millis() - lastAct > idleTimeoutMs) {
      LOGF("awake idle timeout\n");
      uiClearHint();
      return curIdx;
    }
    for (int i = 0; i < 3; i++) {
      bool released = digitalRead(awButtons[i].pin);
      if (released != last[i]) {
        last[i] = released;
        if (!released) {
          lastAct = millis();
          if (i == 0) curIdx = (curIdx + 1) % LOCATION_COUNT;
          else if (i == 1) curIdx = (curIdx + LOCATION_COUNT - 1) % LOCATION_COUNT;
          else { uiClearHint(); return curIdx; }
          uiAwakeHint(LOCATIONS[curIdx].name);
          LOGF("select -> %s\n", LOCATIONS[curIdx].name);
        }
      }
    }
    delay(10);
    yield();
  }
}
```

同檔案需有共用按鍵型別（放在 BTN define 之後）：

```cpp
struct BtnDef { const char* name; uint8_t pin; };
```

setup() 改造為可重入的更新流程（抽出 `runUpdateCycle()`，供首次開機與
awake 確認後共用）：

```cpp
static void runUpdateCycle(int locIdx) {
  const Location& loc = LOCATIONS[locIdx];
  if (!wifiConnect(15000)) {
    uiShowOffline("wifi failed");
    return;
  }
  syncClock(10000);
  WeatherData data;
  if (fetchWeather(loc.lat, loc.lon, &data)) {
    uiRenderDashboard(loc, data);
  } else {
    uiShowOffline("fetch failed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("weather station boot\n");

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_PRESS, INPUT_PULLUP);

  uiPowerOnInit();
  int idx = loadLocationIdx();
  runUpdateCycle(idx);
  // Task 8 在此接上 sleep/wake 分流
}
```

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

Run: `/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor`

測試方式（此階段尚未接 sleep，awake 由 reset 後手動觸發不便，
採間接驗證）：在 setup() 末尾**暫時**加一行 `awakeLoop(idx, 20000);`
上傳後：
- 撥桿上／下：底部黑底提示條循環顯示四個地點名
- 下壓：立即抓該地點資料並全刷（畫面地點名變更）
- 驗完移除暫時行並再次編譯

另驗 NVS：確認某地點後按 RESET 重開，`loadLocationIdx` 回傳上次值
（可在 setup 加一行 `LOGF("saved loc idx=%d", idx);` 觀察，驗完移除）。

- [ ] **Step 6: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "天氣看板：NVS 地點記憶與 awake 模式互動

撥桿上下循環選地點（partial refresh 提示條）、下壓立即刷新；
選擇存 NVS 斷電不忘。等待迴圈 delay+yield 讓出執行權。

驗證等級：有硬體（地點切換、立即刷新、NVS 重開記憶通過）。"
```

---

### Task 8: Sleep/wake 整合與錯誤路徑

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 加入 sleep/wake 基礎設施**

include 區加入：

```cpp
#include <esp_sleep.h>
#include "driver/rtc_io.h"

#define SLEEP_US_NORMAL (30ULL * 60ULL * 1000000ULL)
#define SLEEP_US_RETRY  (5ULL  * 60ULL * 1000000ULL)
```

（`BTN_UP`/`BTN_DOWN`/`BTN_PRESS` 已於 Task 7 定義，勿重複。）

setup() 之前加入：

```cpp
static bool wokeByButton() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return false;
  return (esp_sleep_get_ext1_wakeup_status() >> BTN_PRESS) & 1ULL;
}

static void goToDeepSleep(bool retryShort) {
  LOGF("sleeping %s\n", retryShort ? "5 min (retry)" : "30 min");
  uiHibernate();            // controller hibernate + GPIO7 低
  wifiOff();                // 射頻停用
  Serial.flush();
  delay(500);

  // 撥桿下壓（active-low）作為 EXT1 喚醒源；睡眠期間保持內部拉高
  esp_sleep_enable_timer_wakeup(retryShort ? SLEEP_US_RETRY : SLEEP_US_NORMAL);
  esp_sleep_enable_ext1_wakeup(1ULL << BTN_PRESS, ESP_EXT1_WAKEUP_ANY_LOW);
  rtc_gpio_init((gpio_num_t)BTN_PRESS);
  rtc_gpio_set_direction((gpio_num_t)BTN_PRESS, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)BTN_PRESS);
  rtc_gpio_pulldown_dis((gpio_num_t)BTN_PRESS);

  esp_deep_sleep_start();
}
```

- [ ] **Step 2: setup() 改為完整分流**

```cpp
void setup() {
  Serial.begin(115200);
  delay(500);
  bool btnWake = wokeByButton();
  LOGF("boot (%s)\n", btnWake ? "button wake" : "timer/power-on");

  for (uint8_t p : {(uint8_t)BTN_UP, (uint8_t)BTN_DOWN, (uint8_t)BTN_PRESS}) {
    pinMode(p, INPUT_PULLUP);
  }

  uiPowerOnInit();
  int idx = loadLocationIdx();

  const Location* loc = &LOCATIONS[idx];
  WeatherData data;
  bool ok = false;
  if (wifiConnect(15000)) {
    syncClock(10000);
    ok = fetchWeather(loc->lat, loc->lon, &data);
  }

  if (ok) {
    uiRenderDashboard(*loc, data);
  } else {
    const char* why = (WiFi.status() == WL_CONNECTED) ? "fetch failed" : "wifi failed";
    uiShowOffline(why);
    goToDeepSleep(true);  // 失敗：5 分鐘後重試，不進 awake
    return;               // 不會到達，防禦性
  }

  if (btnWake) {
    int picked = awakeLoop(idx, 20000);
    if (picked != idx) {
      saveLocationIdx(picked);
      runUpdateCycle(picked);   // Task 7 已建立；確認後立即刷新
    }
  }
  saveLocationIdx(idx);
  goToDeepSleep(false);
}
```

loop() 保持空白（`void loop() {}`）；一切在 setup 內完成後睡去。

- [ ] **Step 3: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 4: 硬體檢查點（正常週期）**

Run: `/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor`
Expected：
- 開機刷新後印 `sleeping 30 min`，隨後靜默
- 按**撥桿下撥桿下壓**喚醒：印 `boot (button wake)`，重新刷新，底部出現 awake 提示
- 上下切換、下壓確認後刷新新地點並再度入睡
- 20 秒不動作：印 `awake idle timeout` 自行入睡

- [ ] **Step 5: 硬體檢查點（失敗路徑）**

把 `src/secrets.h` 密碼暫時改錯，重新上傳：
Expected：`[fail] wifi connect timeout` → OFFLINE 畫面 → `sleeping 5 min (retry)`。
驗完**恢復正確密碼**並再次上傳確認正常。

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "天氣看板：sleep/wake 整合與錯誤路徑

timer 30 分＋EXT1 撥桿下壓雙喚醒源（rtc_gpio 維持睡眠期間拉高）；
失敗走 OFFLINE 畫面＋5 分短睡眠重試。實測兩種喚醒分流與
錯誤密碼離线路徑均通過。

驗證等級：有硬體（正常週期＋按鍵喚醒＋失敗路徑通過）。"
```

---

### Task 9: 收尾記錄與最終驗證

**Files:**
- Modify: `docs/device-research.md`
- Verify: 全 repo

- [ ] **Step 1: 彙整 serial log 數據**

從各檢查點收集：Wi-Fi 連線耗時、NTP 耗時、HTTP payload 大小與耗時、
dashboard full refresh 耗時、awake 提示 partial 耗時。

- [ ] **Step 2: `docs/device-research.md` 末尾加入小節**

```markdown
## 天氣看板應用量測（<填日期>）

量測方法：天氣看板韌體 serial log（見
`docs/superpowers/specs/2026-08-25-weather-station-design.md`）。

| 項目 | 結果 |
| --- | --- |
| Wi-Fi 連線 | <xxx ms> |
| NTP 同步 | <xxx ms> |
| Open-Meteo GET | <payload bytes / xxx ms> |
| Dashboard full refresh | <xxx ms> |
| Awake 提示 partial refresh | <xxx ms> |
| 30 分週期運轉 | 通過 |
| 撥桿喚醒 → awake → 確認刷新 | 通過 |

異常事件：<無／條列>
```

- [ ] **Step 3: 最終一致性檢查**

Run: `rg -n 'TBD|TODO|PLACEHOLDER|待補' src/ docs/superpowers/specs/2026-08-25-weather-station-design.md docs/superpowers/plans/2026-08-25-weather-station.md`
Expected: exit 1（無匹配；計畫文件自身指令文字除外）

Run: `rg -n 'WIFI_PASS' src/ --glob '!secrets*'`
Expected: 只命中 `secrets.h.example` 與程式中引用常數名，不含任何實際值。

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 4: Commit**

```bash
git add docs/device-research.md
git commit -m "記錄天氣看板應用量測數據

Wi-Fi/NTP/HTTP/刷新耗時與週期行為，附方法說明。

驗證等級：有硬體（完整應用週期驗證完成）。"
```
