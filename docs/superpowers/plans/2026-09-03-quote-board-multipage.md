# 報價看板雙頁八檔 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 將 Quote Board v1 擴充為兩頁共 8 檔股票，支援 UP／DOWN 快取翻頁、目前頁跨 deep sleep 保留，以及 9 列單次抓取與個股逐列容錯。

**Architecture:** `watchlist.h` 是清單與分頁常數的唯一來源；`quote_logic.h` 負責 9 列收集／驗證、NVS v2、頁面索引及 RTC 純邏輯。`main.cpp` 只編排喚醒、網路、渲染和睡眠，UI 每次只接收一頁 5 列。所有翻頁仍做 full refresh，但不開 Wi-Fi、不做 NTP、不寫 NVS。

**Tech Stack:** PlatformIO、Arduino framework、ESP32-S3、GxEPD2 1.6.9、ArduinoJson 7.4.3、U8g2_for_Adafruit_GFX 1.8.0、C++17 host tests、Python 3／Pillow 12.3.0、u8g2 `bdfconv` 2.37.1。

---

## 實作前提與檔案配置

執行前先讀：

- `README.md`
- `docs/device-research.md`
- `docs/superpowers/specs/2026-09-03-quote-board-multipage-design.md`
- `docs/superpowers/specs/2026-08-29-quote-board-design.md`

執行時使用 `using-git-worktrees` 建立隔離工作樹，不直接在 `master` 上開發。建議
分支名：`feature/quote-board-multipage`。

本計畫的檔案邊界：

| 檔案 | 責任 |
| --- | --- |
| `src/watchlist.h` | 純 C++ 固定清單、分頁常數、安全 `ex_ch` 組裝 |
| `src/quote_logic.h` | 收集／容錯驗證、NVS v2、分頁與 RTC 純邏輯 |
| `tests/host/test_quote_logic.cpp` | C++ 邏輯及 layout 契約測試 |
| `tools/gen_fonts.py` | 固定工具版本、glyph manifest、環境無關 metadata |
| `tests/host/test_gen_fonts.py` | 字型環境拒絕條件及重產一致性測試 |
| `src/fonts_quote.c/.h` | 已產生的字型陣列；正常 firmware build 不跑 generator |
| `src/ui.h/.cpp` | 單頁 5 列、頁碼、有效旗標與狀態列 |
| `src/main.cpp` | EXT1 喚醒分流、RTC page／target、快取翻頁、目前頁渲染 |
| `src/quote_store.cpp/.h` | 安全 URL 呼叫、HTTPS、NVS I/O |
| `README.md` | 操作、清單、按鍵與驗證指令 |

固定命令：

```sh
PIO=/tmp/opencode/pio-venv/bin/pio
HOST_CXX="g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src"
```

---

### Task 1: 建立純 C++ watchlist 與安全 URL builder

**Files:**
- Modify: `src/watchlist.h`
- Modify: `tests/host/test_quote_logic.cpp`

- [ ] **Step 1: 先新增會失敗的常數與 URL 邊界測試**

在 `tests/host/test_quote_logic.cpp` 引入 `watchlist.h`，新增：

```cpp
#include "watchlist.h"

static void testWatchlistAndExCh() {
  static const char EXPECTED[] =
      "tse_t00.tw|tse_2330.tw|tse_2317.tw|tse_0050.tw|tse_006208.tw|"
      "tse_1513.tw|tse_2412.tw|tse_2881.tw|tse_2002.tw";
  static_assert(QUOTE_TOTAL == 9);
  static_assert(STOCK_TOTAL == 8);
  static_assert(PAGE_COUNT == 2);
  static_assert(STOCKS_PER_PAGE == 4);
  static_assert(PAGE_ROWS == 5);
  assert(strcmp(WATCHLIST[5].code, "1513") == 0);
  assert(strcmp(WATCHLIST[5].name, "中興電") == 0);
  assert(strcmp(WATCHLIST[8].code, "2002") == 0);

  const size_t len = strlen(EXPECTED);
  char exact[sizeof EXPECTED];
  memset(exact, 'x', sizeof exact);
  assert(buildQuoteExCh(exact, len + 1));
  assert(strcmp(exact, EXPECTED) == 0);

  char shortBuf[sizeof EXPECTED];
  memset(shortBuf, 'x', sizeof shortBuf);
  assert(!buildQuoteExCh(shortBuf, len));
  assert(shortBuf[0] == '\0');
  assert(!buildQuoteExCh(nullptr, len + 1));
  char zeroCap = 'x';
  assert(!buildQuoteExCh(&zeroCap, 0));
  assert(zeroCap == 'x');

  char again[sizeof EXPECTED];
  assert(buildQuoteExCh(again, sizeof again));
  assert(strcmp(again, exact) == 0);
  printf("watchlist/ex_ch ok\n");
}
```

並在 `main()` 呼叫 `testWatchlistAndExCh()`。

- [ ] **Step 2: 執行 host test，確認先紅**

Run:

```sh
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
  tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic && \
  /tmp/opencode/test_quote_logic
```

Expected: 編譯失敗，指出 `QUOTE_TOTAL`、新 watchlist 項目或
`buildQuoteExCh()` 尚不存在。

- [ ] **Step 3: 將 `watchlist.h` 改為純 C++ 單一資料源**

用下列結構取代 Arduino／UI 相依及 static buffer：

```cpp
#pragma once
#include <cstddef>
#include <cstring>

struct WatchItem {
  const char* exchange;
  const char* code;
  const char* name;
};

static constexpr int QUOTE_TOTAL = 9;
static constexpr int STOCK_TOTAL = 8;
static constexpr int PAGE_COUNT = 2;
static constexpr int STOCKS_PER_PAGE = 4;
static constexpr int PAGE_ROWS = 5;

static constexpr WatchItem WATCHLIST[QUOTE_TOTAL] = {
    {"tse", "t00", "加權指數"},
    {"tse", "2330", "台積電"},
    {"tse", "2317", "鴻海"},
    {"tse", "0050", "元大台灣50"},
    {"tse", "006208", "富邦台50"},
    {"tse", "1513", "中興電"},
    {"tse", "2412", "中華電"},
    {"tse", "2881", "富邦金"},
    {"tse", "2002", "中鋼"},
};

static_assert(QUOTE_TOTAL == 1 + PAGE_COUNT * STOCKS_PER_PAGE,
              "watchlist/page count mismatch");
static_assert(PAGE_ROWS == 1 + STOCKS_PER_PAGE,
              "page row count mismatch");

inline bool buildQuoteExChPrefix(char* out, size_t cap, int count) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  if (count < 0 || count > QUOTE_TOTAL) return false;
  size_t used = 0;
  for (int i = 0; i < count; ++i) {
    const char* pieces[] = {i == 0 ? "" : "|", WATCHLIST[i].exchange,
                            "_", WATCHLIST[i].code, ".tw"};
    for (const char* piece : pieces) {
      const size_t n = strlen(piece);
      if (n >= cap - used) {
        out[0] = '\0';
        return false;
      }
      memcpy(out + used, piece, n);
      used += n;
      out[used] = '\0';
    }
  }
  return true;
}

inline bool buildQuoteExCh(char* out, size_t cap) {
  return buildQuoteExChPrefix(out, cap, QUOTE_TOTAL);
}

// 過渡 shim：Task 6 刪除。
static constexpr int WATCH_N = PAGE_ROWS;
inline const char* quoteExCh() {
  static char out[96];
  static bool built = buildQuoteExChPrefix(out, sizeof out, PAGE_ROWS);
  return built ? out : "";
}
```

為保持中間 commit 可編譯，在同檔暫留只組前 `PAGE_ROWS` 項的 v1
`quoteExCh()` shim，以及 `WATCH_N=PAGE_ROWS`。shim 必須呼叫同一個 bounded
append helper，不能複製清單。它只供尚未遷移的 `quote_store.cpp` 與舊單頁
view 使用，Task 6 必須刪除；最終產物不得保留這兩個名稱。

- [ ] **Step 4: 執行 host test，確認 URL 測試轉綠**

Run: 與 Step 2 相同。

Expected: 新 `watchlist/ex_ch ok` 與既有測試通過。

- [ ] **Step 5: 確認 transitional shim 仍可建置 v1 路徑**

Run:

```sh
/tmp/opencode/pio-venv/bin/pio run
```

Expected: `[SUCCESS]`。這一步只證明中間 commit 沒破壞既有 firmware build；9 檔
正式 URL 要到 Task 6 移除 shim 後才生效。

- [ ] **Step 6: Commit**

```sh
git add src/watchlist.h tests/host/test_quote_logic.cpp
git commit -m "報價看板：集中八檔清單與安全 URL 組裝

驗證等級：無硬體（watchlist/ex_ch host 測試通過＋v1 過渡路徑 pio run SUCCESS）。"
```

---

### Task 2: 將 JSON 收集與驗證擴充為 9 列容錯模型

**Files:**
- Modify: `src/quote_logic.h`
- Modify: `src/main.cpp`
- Modify: `tests/host/test_quote_logic.cpp`

- [ ] **Step 1: 建立 9 列 fixture helper**

把舊固定 5 列 fixture 改成 helper，所有測試都從同一份有效資料複製：

```cpp
static qlogic::RawBatch validRawBatch() {
  qlogic::RawBatch raw = {};
  const char* z[QUOTE_TOTAL] = {
      "46331.45", "2420", "253", "106.95", "245.10",
      "180.5", "132.0", "95.6", "21.4"};
  const char* y[QUOTE_TOTAL] = {
      "45975.22", "2410", "252", "106.05", "243.15",
      "179.0", "131.5", "96.0", "21.1"};
  for (int i = 0; i < QUOTE_TOTAL; ++i) {
    raw.count[i] = 1;
    strcpy(raw.rows[i].code, WATCHLIST[i].code);
    strcpy(raw.rows[i].name, WATCHLIST[i].name);
    strcpy(raw.rows[i].z, z[i]);
    strcpy(raw.rows[i].y, y[i]);
    strcpy(raw.rows[i].t, i == 0 ? "13:33:00" : "13:30:00");
    strcpy(raw.rows[i].d, "20260903");
  }
  return raw;
}
```

- [ ] **Step 2: 新增失敗中的 9 列 parsing／容錯測試**

測試檔加入 `#include <string>`。

新增案例並逐項 assert：

```cpp
static void testNineRowValidation() {
  qlogic::MarketBatch out;
  auto raw = validRawBatch();
  assert(qlogic::validateBatch(raw, &out) == qlogic::V_OK);
  assert(out.rows[0].valid && out.rows[8].valid);
  assert(strcmp(out.date, "20260903") == 0);
  assert(strcmp(out.quoteTime, "13:33:00") == 0);

  auto missingIndex = raw;
  missingIndex.count[0] = 0;
  assert(qlogic::validateBatch(missingIndex, &out) == qlogic::V_STRUCT);

  auto duplicateIndex = raw;
  duplicateIndex.count[0] = 2;
  assert(qlogic::validateBatch(duplicateIndex, &out) == qlogic::V_STRUCT);

  auto badStock = raw;
  strcpy(badStock.rows[6].y, "-");
  assert(qlogic::validateBatch(badStock, &out) == qlogic::V_OK);
  assert(!out.rows[6].valid);
  assert(out.rows[6].z == 0 && out.rows[6].y == 0);
  assert(out.rows[6].t[0] == '\0');

  auto stockDash = raw;
  strcpy(stockDash.rows[5].z, "-");
  assert(qlogic::validateBatch(stockDash, &out) == qlogic::V_OK);
  assert(out.rows[5].valid && out.rows[5].z == 0);

  auto badDate = raw;
  strcpy(badDate.rows[7].d, "20260902");
  assert(qlogic::validateBatch(badDate, &out) == qlogic::V_OK);
  assert(!out.rows[7].valid);

  auto newerInvalid = raw;
  strcpy(newerInvalid.rows[8].t, "13:34:00");
  strcpy(newerInvalid.rows[8].name, "");
  assert(qlogic::validateBatch(newerInvalid, &out) == qlogic::V_OK);
  assert(strcmp(out.quoteTime, "13:33:00") == 0);
}
```

對個股錯誤加入共用 assertion，確認只有指定 slot 失效且一定是 canonical invalid：

```cpp
auto assertOnlyStockInvalid = [&](qlogic::RawBatch candidate, int bad) {
  qlogic::MarketBatch checked;
  assert(qlogic::validateBatch(candidate, &checked) == qlogic::V_OK);
  for (int i = 0; i < QUOTE_TOTAL; ++i) assert(checked.rows[i].valid == (i != bad));
  assert(strcmp(checked.rows[bad].code, WATCHLIST[bad].code) == 0);
  assert(checked.rows[bad].z == 0 && checked.rows[bad].y == 0);
  assert(checked.rows[bad].t[0] == '\0');
};

auto missing = raw; missing.count[5] = 0; assertOnlyStockInvalid(missing, 5);
auto duplicate = raw; duplicate.count[5] = 2; assertOnlyStockInvalid(duplicate, 5);
auto noName = raw; noName.rows[5].name[0] = '\0'; assertOnlyStockInvalid(noName, 5);
auto badZ = raw; strcpy(badZ.rows[5].z, "bad"); assertOnlyStockInvalid(badZ, 5);
auto badTime = raw; strcpy(badTime.rows[5].t, "9:30"); assertOnlyStockInvalid(badTime, 5);
auto otherDate = raw; strcpy(otherDate.rows[5].d, "20260902");
assertOnlyStockInvalid(otherDate, 5);

auto indexNumeric = raw; strcpy(indexNumeric.rows[0].y, "-");
assert(qlogic::validateBatch(indexNumeric, &out) == qlogic::V_NUMERIC);
auto indexFormat = raw; indexFormat.rows[0].name[0] = '\0';
assert(qlogic::validateBatch(indexFormat, &out) == qlogic::V_FORMAT);
auto indexMissing = raw; indexMissing.count[0] = 0;
assert(qlogic::validateBatch(indexMissing, &out) == qlogic::V_STRUCT);
auto indexDuplicate = raw; indexDuplicate.count[0] = 2;
assert(qlogic::validateBatch(indexDuplicate, &out) == qlogic::V_STRUCT);
```

JSON collection 使用具體 helper 與 assertions：

```cpp
static std::string jsonRows(std::initializer_list<const char*> codes) {
  std::string body = "{\"msgArray\":[";
  bool first = true;
  for (const char* code : codes) {
    if (!first) body += ',';
    first = false;
    body += "{\"c\":\"" + std::string(code) +
            "\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\"," 
            "\"t\":\"13:30:00\",\"d\":\"20260903\"}";
  }
  return body + "]}";
}

const char* all[] = {"t00","2330","2317","0050","006208",
                     "1513","2412","2881","2002"};
std::string complete = jsonRows({all[0],all[1],all[2],all[3],all[4],
                                 all[5],all[6],all[7],all[8]});
qlogic::RawBatch collected = {};
assert(qlogic::parseJsonToRaw(complete.c_str(), complete.size(), &collected) == qlogic::V_OK);
for (int i = 0; i < QUOTE_TOTAL; ++i) assert(collected.count[i] == 1);

std::string unknown = jsonRows({all[0],all[1],all[2],all[3],all[4],
                                all[5],all[6],all[7],all[8],"9999"});
assert(qlogic::parseJsonToRaw(unknown.c_str(), unknown.size(), &collected) == qlogic::V_OK);
for (int i = 0; i < QUOTE_TOTAL; ++i) assert(collected.count[i] == 1);

std::string missingStock = jsonRows({all[0],all[1],all[2],all[3],all[4],
                                     all[5],all[7],all[8]});
assert(qlogic::parseJsonToRaw(missingStock.c_str(), missingStock.size(), &collected) == qlogic::V_OK);
assert(collected.count[6] == 0);

std::string duplicateStock = jsonRows({all[0],all[1],all[2],all[3],all[4],
                                       all[5],all[6],all[6],all[7],all[8]});
assert(qlogic::parseJsonToRaw(duplicateStock.c_str(), duplicateStock.size(), &collected) == qlogic::V_OK);
assert(collected.count[6] == 2);

std::string missingIndex = jsonRows({all[1],all[2],all[3],all[4],all[5],all[6],all[7],all[8]});
assert(qlogic::parseJsonToRaw(missingIndex.c_str(), missingIndex.size(), &collected) == qlogic::V_OK);
assert(qlogic::validateBatch(collected, &out) == qlogic::V_STRUCT);
std::string duplicateIndex = jsonRows({all[0],all[0],all[1],all[2],all[3],all[4],
                                       all[5],all[6],all[7],all[8]});
assert(qlogic::parseJsonToRaw(duplicateIndex.c_str(), duplicateIndex.size(), &collected) == qlogic::V_OK);
assert(qlogic::validateBatch(collected, &out) == qlogic::V_STRUCT);
```

測試檔加入 `<initializer_list>`。不要再要求 `arr.size()==9`。

- [ ] **Step 3: 執行測試，確認因資料結構與語意未實作而失敗**

Run:

```sh
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
  tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic
```

Expected: 編譯失敗於 `RawBatch::count`／`QuoteRow::valid`，或新容錯 assert 失敗。

- [ ] **Step 4: 實作 slot-based JSON 收集資料結構**

在 `quote_logic.h` include `watchlist.h`，移除 `EXPECT_CODES[5]`，改用：

```cpp
struct QuoteRow {
  char code[12];
  bool valid;
  double z;
  double y;
  char t[9];
};

struct RawBatch {
  RawQuote rows[QUOTE_TOTAL];
  uint8_t count[QUOTE_TOTAL];
};

struct MarketBatch {
  QuoteRow rows[QUOTE_TOTAL];
  char date[9];
  char quoteTime[9];
};

inline int expectedIndex(const char* code) {
  for (int i = 0; i < QUOTE_TOTAL; ++i)
    if (strcmp(code, WATCHLIST[i].code) == 0) return i;
  return -1;
}
```

`parseJsonToRaw()` 只拒絕壞 JSON 或不存在的 `msgArray`。遍歷 array 時，未知代碼
直接 `continue`；預期代碼使用飽和計數，只需區分 0、1、至少 2：

```cpp
if (out->count[idx] < 2) ++out->count[idx];
if (out->count[idx] != 1) continue;
```

只有第一次出現時 bounded copy 欄位。加入由程式產生 300 個同代碼 row 的 JSON
測試，確認 `count[idx]==2` 且 index／個股仍按重複規則處理，不得因 `uint8_t`
回繞誤判為 1。

```cpp
std::string many = "{\"msgArray\":[";
for (int i = 0; i < 300; ++i) {
  if (i) many += ',';
  many += "{\"c\":\"t00\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",";
  many += "\"t\":\"13:30:00\",\"d\":\"20260903\"}";
}
many += "]}";
qlogic::RawBatch repeated = {};
assert(qlogic::parseJsonToRaw(many.c_str(), many.size(), &repeated) == qlogic::V_OK);
assert(repeated.count[0] == 2);
assert(qlogic::validateBatch(repeated, &out) == qlogic::V_STRUCT);
```

- [ ] **Step 5: 實作 index 嚴格、股票逐列降級的 `validateBatch()`**

先清空輸出並填入所有預期 code。建立單列 parser：

```cpp
inline bool convertRow(const RawQuote& raw, QuoteRow* out) {
  if (raw.name[0] == '\0' || !validTime(raw.t) || !validDate(raw.d)) return false;
  double z = 0, y = 0;
  if (!parseNum(raw.z, &z) && strcmp(raw.z, "-") != 0) return false;
  if (!parseNum(raw.y, &y) || y == 0.0) return false;
  out->valid = true;
  out->z = z;
  out->y = y;
  strcpy(out->t, raw.t);
  return true;
}

inline void invalidateRow(QuoteRow* row, const char* code) {
  *row = QuoteRow{};
  strncpy(row->code, code, sizeof(row->code) - 1);
}
```

`validateBatch()` 的順序固定：

1. `count[0] != 1` 回 `V_STRUCT`。
2. index 數值錯誤（非法 `z`／`y` 或 `y==0`）回 `V_NUMERIC`；空 `name`、非法
   `d`／`t` 回 `V_FORMAT`。用 `validateRequiredRow()` 明確回傳這兩類錯誤，
   不可把 index 錯誤壓成單一 bool。
3. `out->date = raw.rows[0].d`，index 寫入 `out->rows[0]`。
4. 對 1..8：`count != 1`、`convertRow` false、或 `d != out->date` 時呼叫
   `invalidateRow()`；否則保留有效列。
5. `quoteTime` 只在有效列中取最大值。

- [ ] **Step 6: 保持 v1 persistence 邊界，避免越界**

本任務不得把 `main.cpp` 的 batch→record loop 改成 `QUOTE_TOTAL`；`QuoteRecord`
在 Task 3 前仍只有 5 列。過渡 `WATCH_N=PAGE_ROWS` 繼續限制複製前 5 列，第二頁
資料只存在 `MarketBatch`，不寫入舊 blob。

- [ ] **Step 7: 執行完整 C++ host test 與 firmware build**

Run:

```sh
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
  tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic && \
  /tmp/opencode/test_quote_logic
/tmp/opencode/pio-venv/bin/pio run
```

Expected: `ALL PASS`，包括 `watchlist/ex_ch ok` 與 `nine-row validation ok`；PIO
`[SUCCESS]`。過渡 firmware 仍向 API 請求前 5 列，但後四個 slot 會安全降級，
不允許燒錄作硬體驗證。

- [ ] **Step 8: Commit**

```sh
git add src/quote_logic.h tests/host/test_quote_logic.cpp
git commit -m "報價看板：擴充九列解析與個股容錯驗證

驗證等級：無硬體（quote logic host 測試 ALL PASS＋過渡 firmware pio run SUCCESS）。"
```

---

### Task 3: 升級 NVS v2，加入分頁與 RTC 純邏輯

**Files:**
- Modify: `src/quote_logic.h`
- Modify: `tests/host/test_quote_logic.cpp`

- [ ] **Step 1: 先寫 NVS v2 layout／sanity 測試**

更新 `testBlob()` 建立 9 列，並加入：

```cpp
static_assert(qlogic::BLOB_VERSION == 2);
static_assert(std::is_standard_layout_v<qlogic::QuoteRow>);
static_assert(std::is_standard_layout_v<qlogic::QuoteRecord>);
static_assert(sizeof(qlogic::QuoteRow) == 48);
static_assert(alignof(qlogic::QuoteRow) == 8);
static_assert(offsetof(qlogic::QuoteRow, code) == 0);
static_assert(offsetof(qlogic::QuoteRow, valid) == 12);
static_assert(offsetof(qlogic::QuoteRow, z) == 16);
static_assert(offsetof(qlogic::QuoteRow, y) == 24);
static_assert(offsetof(qlogic::QuoteRow, t) == 32);
static_assert(sizeof(qlogic::QuoteRecord) == 472);
static_assert(offsetof(qlogic::QuoteRecord, rows) == 8);
static_assert(offsetof(qlogic::QuoteRecord, quoteDate) == 440);
static_assert(offsetof(qlogic::QuoteRecord, quoteTime) == 449);
static_assert(offsetof(qlogic::QuoteRecord, lastCloseDate) == 458);
static_assert(offsetof(qlogic::QuoteRecord, savedEpoch) == 468);
```

有效列要求 `valid=true`、`y!=0`、合法 `t`。無效個股只接受 `valid=false`、
`z=0`、`y=0`、`t=""`，但 code 仍須是該 slot 的預期 code。index 0 不接受
`valid=false`。`recordDiffers()` 必須比較 `valid`。

- [ ] **Step 2: 先寫頁面、按鍵與 RTC 測試**

在純邏輯層使用以下介面：

```cpp
enum class WakeAction { None, Menu, PrevPage, NextPage };

struct QuoteRtcState {
  uint8_t pageIndex;
  uint32_t targetEpoch;
};

int quoteIndexForPageRow(uint8_t pageIndex, int rowIndex);
uint8_t changedPage(uint8_t pageIndex, WakeAction action);
WakeAction chooseWakeAction(bool menu, bool up, bool down);
void normalizeRtcState(QuoteRtcState* state, bool deepSleepWake);
uint32_t resumeTarget(uint32_t now, uint32_t target);
uint32_t noCacheRetryTarget(uint32_t now, uint32_t target);
```

測試：

```cpp
assert(qlogic::quoteIndexForPageRow(0, 0) == 0);
assert(qlogic::quoteIndexForPageRow(0, 4) == 4);
assert(qlogic::quoteIndexForPageRow(1, 0) == 0);
assert(qlogic::quoteIndexForPageRow(1, 1) == 5);
assert(qlogic::quoteIndexForPageRow(1, 4) == 8);
assert(qlogic::quoteIndexForPageRow(2, 1) == -1);
assert(qlogic::quoteIndexForPageRow(0, 5) == -1);
assert(qlogic::changedPage(0, qlogic::WakeAction::PrevPage) == 1);
assert(qlogic::changedPage(1, qlogic::WakeAction::NextPage) == 0);
assert(qlogic::chooseWakeAction(true, true, false) == qlogic::WakeAction::Menu);
assert(qlogic::chooseWakeAction(false, true, true) == qlogic::WakeAction::None);

qlogic::QuoteRtcState rtc{1, 1234};
qlogic::normalizeRtcState(&rtc, false);
assert(rtc.pageIndex == 0 && rtc.targetEpoch == 0);
rtc = {9, 1234};
qlogic::normalizeRtcState(&rtc, true);
assert(rtc.pageIndex == 0 && rtc.targetEpoch == 1234);

const uint32_t now = 1000000;
assert(qlogic::resumeTarget(now, now + 20) == now + 20);
assert(qlogic::resumeTarget(now, now) == now + 1);
assert(qlogic::resumeTarget(now, now - 1) == now + 1);
assert(qlogic::resumeTarget(now, 0) == now + 300);
assert(qlogic::resumeTarget(now, now + 7 * 86400 + 1) == now + 300);
assert(qlogic::resumeTarget(now, now - 7 * 86400 - 1) == now + 300);
assert(qlogic::noCacheRetryTarget(now, now + 1000) == now + 300);
assert(qlogic::noCacheRetryTarget(now, now + 20) == now + 20);
uint32_t nineOhFive = 1000020;
assert(qlogic::resumeTarget(1000000, nineOhFive) == nineOhFive);
uint32_t retry = qlogic::noCacheRetryTarget(1000000, 1002000);
assert(retry == 1000300);
assert(qlogic::noCacheRetryTarget(1000100, retry) == 1000300);
```

測試名稱與註解須明示 `nineOhFive` 代表 09:04:40→09:05:00 的 20 秒差；最後兩個
assertions 鎖定第二次翻頁不得把 retry deadline 往後滑動。

- [ ] **Step 3: 執行測試，確認 BLOB_VERSION／helper 尚未實作而失敗**

Run: Task 2 Step 7 的 C++ host test 命令。

Expected: compile failure 或新 assertions 失敗。

- [ ] **Step 4: 實作 NVS v2 與 layout 斷言**

在 `quote_logic.h` 加 `<cstddef>`、`<type_traits>`，改為：

```cpp
static constexpr uint32_t BLOB_VERSION = 2;

struct QuoteRecord {
  uint32_t version;
  QuoteRow rows[QUOTE_TOTAL];
  char quoteDate[9];
  char quoteTime[9];
  char lastCloseDate[9];
  uint32_t savedEpoch;
};
```

將 Step 1 的 `sizeof`／`alignof`／`offsetof` assertions 放到 production header。
`recordSane()` 與 `recordDiffers()` 依 Step 1 契約迭代 `QUOTE_TOTAL`。

- [ ] **Step 5: 實作分頁、按鍵與 RTC helper**

helper 不得 include ESP-IDF header。時間有效性用 `int64_t` 差值避免 unsigned
underflow：

```cpp
inline bool rtcTargetValid(uint32_t now, uint32_t target) {
  if (target == 0) return false;
  int64_t diff = static_cast<int64_t>(target) - now;
  return diff >= -7LL * 86400 && diff <= 7LL * 86400;
}

inline uint32_t resumeTarget(uint32_t now, uint32_t target) {
  if (!rtcTargetValid(now, target)) return now + 300;
  return target > now ? target : now + 1;
}

inline uint32_t noCacheRetryTarget(uint32_t now, uint32_t target) {
  if (!rtcTargetValid(now, target)) return now + 300;
  if (target <= now) return now + 1;
  uint32_t retry = now + 300;
  return target < retry ? target : retry;
}
```

其餘 helper 直接依 Step 2 assertions 實作，非法 page／row 回 `-1`。

- [ ] **Step 6: NVS 擴容後才把 persistence loop 改成 9 列**

`main.cpp` 的 `fetchUpdate()`、`finalizeClose()` 及 batch→record copy 改用
`QUOTE_TOTAL`。此步必須晚於 `QuoteRecord::rows[QUOTE_TOTAL]`，禁止對 5 列 blob
寫入 9 列。單頁 view loop 暫時仍用 `WATCH_N=PAGE_ROWS`，Task 5 再改 page slicing。

- [ ] **Step 7: 執行 host test 與 firmware build，確認全部通過**

Run:

```sh
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
  tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic && \
  /tmp/opencode/test_quote_logic
/tmp/opencode/pio-venv/bin/pio run
```

Expected: `ALL PASS`，新增 `blob v2 ok`、`page/rtc ok`；PIO `[SUCCESS]`。

- [ ] **Step 8: Commit**

```sh
git add src/quote_logic.h src/main.cpp tests/host/test_quote_logic.cpp
git commit -m "報價看板：加入 NVS v2 與分頁 RTC 純邏輯

驗證等級：無硬體（NVS layout、頁面與 RTC host 測試 ALL PASS）。"
```

---

### Task 4: 收緊字型工具契約並重產 glyph

**Files:**
- Modify: `tools/gen_fonts.py`
- Create: `tests/host/test_gen_fonts.py`
- Modify: `src/fonts_quote.c`
- Verify: `src/fonts_quote.h`

- [ ] **Step 1: 寫環境驗證與 metadata 的失敗測試**

以 `unittest`／`unittest.mock` 載入 `tools.gen_fonts`，測試獨立 helper：

```python
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

from tools import gen_fonts

EXPECTED_FONT_SHA = "faa5f3656a78b2e2d450d27fe8382c778bc2b6bb5ea29c986664a6a435056ceb"
PINNED_FONT = pathlib.Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc")

class FontContractTest(unittest.TestCase):
    def test_manifest_contains_v2_glyphs(self):
        self.assertTrue(set("部分/").issubset(set(gen_fonts.G16)))
        self.assertTrue(set("中興華金鋼").issubset(set(gen_fonts.G20)))

    def test_validate_environment_accepts_exact_inputs(self):
        self.assertEqual(
            gen_fonts.validate_environment(EXPECTED_FONT_SHA, "12.3.0", "2.37.1"),
            None,
        )

    def test_rejects_wrong_inputs(self):
        with self.assertRaises(SystemExit):
            gen_fonts.validate_environment("bad", "12.3.0", "2.37.1")
        with self.assertRaises(SystemExit):
            gen_fonts.validate_environment(EXPECTED_FONT_SHA, "12.2.0", "2.37.1")
        with self.assertRaises(SystemExit):
            gen_fonts.validate_environment(EXPECTED_FONT_SHA, "12.3.0", "abc123")
        with self.assertRaises(SystemExit):
            gen_fonts.validate_environment(EXPECTED_FONT_SHA, "12.3.0", "2.37.1-dirty")

    def test_banner_is_path_independent(self):
        a = gen_fonts.output_banner(EXPECTED_FONT_SHA)
        b = gen_fonts.output_banner(EXPECTED_FONT_SHA)
        self.assertEqual(a, b)
        self.assertIn("font: NotoSansCJK-Bold.ttc", a)
        self.assertIn(EXPECTED_FONT_SHA, a)
        self.assertNotIn("/usr/share", a)

    @mock.patch("tools.gen_fonts.subprocess.run")
    def test_detect_u8g2_uses_exact_tag_and_dirty_check(self, run):
        run.return_value = subprocess.CompletedProcess([], 0, "2.37.1\n", "")
        self.assertEqual(gen_fonts.detect_u8g2_version(), "2.37.1")
        run.assert_called_once_with(
            ["git", "-C", gen_fonts.U8G2_DIR, "describe", "--tags",
             "--exact-match", "--dirty"],
            capture_output=True, text=True,
        )

    def _assert_generate_rejected_without_overwrite(self, font_path, pillow, rev):
        with tempfile.TemporaryDirectory() as tmp:
            out_c = pathlib.Path(tmp) / "fonts_quote.c"
            out_h = pathlib.Path(tmp) / "fonts_quote.h"
            out_c.write_bytes(b"sentinel-c")
            out_h.write_bytes(b"sentinel-h")
            with mock.patch.object(gen_fonts.PIL, "__version__", pillow), \
                 mock.patch.object(gen_fonts, "detect_u8g2_version", return_value=rev):
                with self.assertRaises(SystemExit):
                    gen_fonts.generate(str(font_path), str(out_c), str(out_h))
            self.assertEqual(out_c.read_bytes(), b"sentinel-c")
            self.assertEqual(out_h.read_bytes(), b"sentinel-h")

    def test_generate_rejects_wrong_sha_without_overwrite(self):
        with tempfile.NamedTemporaryFile() as bad_font:
            bad_font.write(b"not-the-pinned-font")
            bad_font.flush()
            self._assert_generate_rejected_without_overwrite(
                bad_font.name, "12.3.0", "2.37.1")

    def test_generate_rejects_wrong_pillow_without_overwrite(self):
        self._assert_generate_rejected_without_overwrite(
            PINNED_FONT, "12.2.0", "2.37.1")

    def test_generate_rejects_non_exact_tag_without_overwrite(self):
        self._assert_generate_rejected_without_overwrite(
            PINNED_FONT, "12.3.0", "abc123")

    def test_generate_rejects_dirty_tag_without_overwrite(self):
        self._assert_generate_rejected_without_overwrite(
            PINNED_FONT, "12.3.0", "2.37.1-dirty")
```

這些測試涵蓋 SHA 計算、git command、環境驗證到輸出寫入前的完整路徑，不得
用只呼叫 `validate_environment()` 的測試取代。

- [ ] **Step 2: 跑 Python 測試，確認 helper 尚不存在而失敗**

Run:

```sh
/tmp/opencode/pio-venv/bin/python -m unittest -v tests.host.test_gen_fonts
```

Expected: ERROR/FAIL，指出 `validate_environment` 或 `output_banner` 不存在。

- [ ] **Step 3: 實作版本驗證及原子輸出**

在 `gen_fonts.py` 定義：

```python
EXPECTED_FONT_SHA = "faa5f3656a78b2e2d450d27fe8382c778bc2b6bb5ea29c986664a6a435056ceb"
EXPECTED_PILLOW = "12.3.0"
EXPECTED_U8G2 = "2.37.1"

def validate_environment(font_sha: str, pillow: str, u8g2: str) -> None:
    if font_sha != EXPECTED_FONT_SHA:
        sys.exit(f"字型 SHA256 不符：{font_sha}")
    if pillow != EXPECTED_PILLOW:
        sys.exit(f"Pillow 版本不符：{pillow}")
    if u8g2 != EXPECTED_U8G2:
        sys.exit(f"bdfconv u8g2 tag 不符：{u8g2}")

def output_banner(font_sha: str) -> str:
    return (
        "// 自動產生：tools/gen_fonts.py（勿手改）\n"
        f"// font: NotoSansCJK-Bold.ttc sha256={font_sha} (SIL OFL 1.0)\n"
        f"// PIL {EXPECTED_PILLOW}, bdfconv u8g2@{EXPECTED_U8G2}\n"
    )
```

加入 `detect_u8g2_version()`，`generate()` 在產生任何 BDF／輸出前執行：

```python
def detect_u8g2_version() -> str:
    result = subprocess.run(
        ["git", "-C", U8G2_DIR, "describe", "--tags", "--exact-match", "--dirty"],
        capture_output=True, text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else ""

validate_environment(sha, PIL.__version__, detect_u8g2_version())
```

先把 `.c/.h` 寫入同目錄暫存檔，兩份都成功後才用 `os.replace()` 覆寫正式檔。
`main()` 只解析 `--font` 後呼叫 `generate(font_path, OUT_C, OUT_H)`。

- [ ] **Step 4: 更新 manifest**

```python
G16 = "週日一二三四五六更新失敗時間未同步部分0123456789:-/ " + "A1(g"
G20 = "台積電鴻海元大灣富邦中興華金鋼05" + "A1(g"
G28 = "加權指數" + "A1(g"
```

- [ ] **Step 5: 跑 Python contract tests**

Run: Step 2 命令。

Expected: 全部 `OK`。

- [ ] **Step 6: 準備固定 u8g2 工具並重產兩次**

若 `/tmp/opencode/u8g2` 不存在：

```sh
git clone --depth 1 --branch 2.37.1 https://github.com/olikraus/u8g2 /tmp/opencode/u8g2
make -C /tmp/opencode/u8g2/tools/font/bdfconv
```

驗證：

```sh
sha256sum /usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc
/tmp/opencode/pio-venv/bin/python -c 'import PIL; assert PIL.__version__ == "12.3.0"'
```

Expected: `2.37.1`、spec 固定 SHA、Python exit 0。

重產並比對：

```sh
/tmp/opencode/pio-venv/bin/python tools/gen_fonts.py
cp src/fonts_quote.c /tmp/opencode/fonts_quote.first.c
cp src/fonts_quote.h /tmp/opencode/fonts_quote.first.h
/tmp/opencode/pio-venv/bin/python tools/gen_fonts.py
cmp /tmp/opencode/fonts_quote.first.c src/fonts_quote.c
cmp /tmp/opencode/fonts_quote.first.h src/fonts_quote.h
```

Expected: 兩個 `cmp` exit 0；產物 banner 無絕對路徑且含完整 SHA。

再驗證同內容、不同檔名不改變輸出：

```sh
cp /usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc \
  /tmp/opencode/same-font-different-name.ttc
/tmp/opencode/pio-venv/bin/python tools/gen_fonts.py \
  --font /tmp/opencode/same-font-different-name.ttc
cmp /tmp/opencode/fonts_quote.first.c src/fonts_quote.c
cmp /tmp/opencode/fonts_quote.first.h src/fonts_quote.h
```

Expected: 兩個 `cmp` 仍為 exit 0。

- [ ] **Step 7: Commit**

```sh
git add tools/gen_fonts.py tests/host/test_gen_fonts.py src/fonts_quote.c src/fonts_quote.h
git commit -m "報價看板：加入第二頁名稱與可重現字型契約

驗證等級：無硬體（字型 contract tests 通過，固定環境重產兩次 byte-identical）。"
```

---

### Task 5: UI 顯示目前頁、頁碼與部分失敗

**Files:**
- Modify: `src/ui.h`
- Modify: `src/ui.cpp`
- Modify: `src/main.cpp`（僅 view assembly helper）
- Modify: `tests/host/test_quote_logic.cpp`

- [ ] **Step 1: 先寫頁面切片與狀態優先序測試**

先只在測試中引用尚不存在的 `qlogic::ViewStatus` 與 `resolvedStatus()`，加入完整
assertions：

```cpp
using qlogic::ViewStatus;
assert(qlogic::resolvedStatus(ViewStatus::None, false) == ViewStatus::None);
assert(qlogic::resolvedStatus(ViewStatus::None, true) == ViewStatus::PartialFailure);
assert(qlogic::resolvedStatus(ViewStatus::PartialFailure, false) == ViewStatus::None);
assert(qlogic::resolvedStatus(ViewStatus::UpdateFailure, false) == ViewStatus::UpdateFailure);
assert(qlogic::resolvedStatus(ViewStatus::UpdateFailure, true) == ViewStatus::UpdateFailure);
assert(qlogic::resolvedStatus(ViewStatus::TimeUnsynced, false) == ViewStatus::TimeUnsynced);
assert(qlogic::resolvedStatus(ViewStatus::TimeUnsynced, true) == ViewStatus::TimeUnsynced);
```

另建立 row 6 invalid 的 record，逐 row 呼叫 `quoteIndexForPageRow()`：

```cpp
auto pageHasInvalid = [&](uint8_t page) {
  for (int row = 0; row < PAGE_ROWS; ++row) {
    int idx = qlogic::quoteIndexForPageRow(page, row);
    if (!record.rows[idx].valid) return true;
  }
  return false;
};
assert(!pageHasInvalid(0));
assert(pageHasInvalid(1));
```

- [ ] **Step 2: 執行 C++ host test，確認新介面不存在而失敗**

Run: Task 2 Step 7 命令。

Expected: compile failure 於 `ViewStatus`／`resolvedStatus()`。

- [ ] **Step 3: 實作狀態純邏輯與 `QuoteView`**

在 `quote_logic.h` 加入 enum 與下列最小實作：

```cpp
enum class ViewStatus { None, PartialFailure, UpdateFailure, TimeUnsynced };

inline ViewStatus resolvedStatus(ViewStatus requested, bool visibleInvalid) {
  if (requested == ViewStatus::TimeUnsynced ||
      requested == ViewStatus::UpdateFailure) {
    return requested;
  }
  return visibleInvalid ? ViewStatus::PartialFailure : ViewStatus::None;
}
```

`ui.h` 改為單頁固定陣列：

```cpp
static constexpr int QUOTE_ROWS = PAGE_ROWS;

struct QuoteView {
  const char* names[QUOTE_ROWS];
  bool valid[QUOTE_ROWS];
  double z[QUOTE_ROWS];
  double chg[QUOTE_ROWS];
  double pct[QUOTE_ROWS];
  char dateStr[20];
  char timeStr[8];
  uint8_t pageIndex;
  uint8_t pageCount;
  const char* status;
};
```

`viewFromRecord()` 新增 `pageIndex` 參數。每個 UI row 透過
`quoteIndexForPageRow()` 取總資料 index；無效列不得呼叫 `calcQuote(z, y)`，直接
設 `z/chg/pct=0`。外部狀態字先轉 `ViewStatus`，再以目前頁是否含 invalid row
呼叫 `resolvedStatus()`，最後映射成 `nullptr`／`部分失敗`／`更新失敗`／
`時間未同步`。

- [ ] **Step 4: 更新 UI header 與 invalid row 渲染**

在 `uiShowQuotes()` header 組合：

```cpp
char right[32];
snprintf(right, sizeof right, "%u/%u 更新 %s",
         static_cast<unsigned>(v.pageIndex + 1),
         static_cast<unsigned>(v.pageCount), v.timeStr);
int tw = u8g2.getUTF8Width(right);
u8g2.setCursor(272 - 16 - tw, 26);
u8g2.print(right);
```

列渲染條件改為 `if (!v.valid[i] || v.z[i] == 0.0)`，兩者都畫 `--` 且無箭頭。
不得變更 `ROW_Y0`、`ROW_STRIDE`、rotation、full-window 或 `setFontT()` 契約。

- [ ] **Step 5: 跑 host tests 與 transitional firmware build**

Run:

```sh
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
  tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic && \
  /tmp/opencode/test_quote_logic
/tmp/opencode/pio-venv/bin/pio run
```

Expected: host `ALL PASS`、PIO `[SUCCESS]`。此時 Task 1 的 v1 `quoteExCh()` shim
仍提供前 5 列 URL；Task 6 才移除 shim 並切換正式 9 列 URL。

- [ ] **Step 6: Commit**

```sh
git add src/quote_logic.h src/ui.h src/ui.cpp src/main.cpp tests/host/test_quote_logic.cpp
git commit -m "報價看板：渲染雙頁與部分失敗狀態

驗證等級：無硬體（host 測試 ALL PASS＋過渡 firmware pio run SUCCESS）。"
```

---

### Task 6: 串接安全 URL 與 NVS v2

**Files:**
- Modify: `src/quote_store.cpp`
- Modify: `src/quote_store.h`
- Modify: `src/main.cpp`

- [ ] **Step 1: 移除過渡 shim，執行 build 確認整合仍是紅燈**

從 `watchlist.h` 只刪除 Task 1 的 `WATCH_N` 與 static-buffer `quoteExCh()`。
`buildQuoteExChPrefix()` 是 final `buildQuoteExCh()` 共用的 bounded implementation
helper，必須保留。執行：

```sh
/tmp/opencode/pio-venv/bin/pio run
```

Expected: compile failure，`quote_store.cpp` 指出 `quoteExCh` 未宣告。若不是這個
原因，先修正前面任務造成的非預期 build 問題，再重跑到取得預期紅燈。

- [ ] **Step 2: 讓 `quoteFetch()` 使用 bounded builder**

在建立 `String url` 前：

```cpp
char exCh[192];
if (!buildQuoteExCh(exCh, sizeof exCh)) {
  LOGF("[fail] ex_ch overflow\n");
  return -13;
}
String url = String("https://mis.twse.com.tw/stock/api/getStockInfo.jsp?ex_ch=") +
             exCh + "&json=1";
```

更新 `quote_store.h`：`-13` 是 URL 組裝失敗；註解由 all-or-nothing 改成「指數
嚴格、個股逐列容錯」。NVS load/save API 不需改簽名，`sizeof(QuoteRecord)` 會
自動切換 v2；舊 blob 長度／version 會被拒絕。

- [ ] **Step 3: 把 main 的所有總資料 loop 改成 `QUOTE_TOTAL`**

`fetchUpdate()`、`finalizeClose()`、batch→record copy 都必須複製 9 列；所有
`viewFromRecord()`／`viewFromBatch()` 呼叫都傳入 RTC `pageIndex`。不得以
`PAGE_ROWS` 複製 NVS 或 batch。

- [ ] **Step 4: 跑完整無硬體驗證**

Run:

```sh
/tmp/opencode/pio-venv/bin/python -m unittest -v tests.host.test_gen_fonts
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
  tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic && \
  /tmp/opencode/test_quote_logic
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/GxEPD2/src \
  tests/host/test_rotation.cpp -o /tmp/opencode/test_rotation && \
  /tmp/opencode/test_rotation
/tmp/opencode/pio-venv/bin/pio run
```

Expected: Python `OK`、C++ `ALL PASS`、`rotation golden ok`、PlatformIO
`[SUCCESS]`。

- [ ] **Step 5: Commit**

```sh
git add src/watchlist.h src/quote_store.cpp src/quote_store.h src/main.cpp
git commit -m "報價看板：串接九檔 HTTPS 與 NVS v2

驗證等級：無硬體（Python/C++ host 測試通過＋pio run SUCCESS；TLS 實抓待硬體）。"
```

---

### Task 7: 實作三鍵 EXT1、RTC page 與快取翻頁路徑

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/quote_logic.h`
- Modify: `tests/host/test_quote_logic.cpp`

- [ ] **Step 1: 先寫 page-wake routing 與三鍵 release 測試**

只修改 host test，先引用尚不存在的 helper：

```cpp
assert(qlogic::pageWakeRequested(true, false, true, false));
assert(qlogic::pageWakeRequested(true, false, false, true));
assert(qlogic::pageWakeRequested(true, false, true, true));
assert(!qlogic::pageWakeRequested(true, true, true, false));
assert(!qlogic::pageWakeRequested(false, false, true, false));
assert(qlogic::wakeButtonsReleased(true, true, true));
assert(!qlogic::wakeButtonsReleased(false, true, true));
assert(!qlogic::wakeButtonsReleased(true, false, true));
assert(!qlogic::wakeButtonsReleased(true, true, false));
```

PRESS／EXIT 不在 `wakeButtonsReleased()` 簽名，避免主流程重新把它們納入 R4。

- [ ] **Step 2: 跑 host test 確認紅燈**

Run: Task 2 Step 7 的 C++ host test 命令。

Expected: compile failure，指出兩個 helper 不存在。

- [ ] **Step 3: 實作 routing helper**

在 `quote_logic.h` 加入：

```cpp
inline bool pageWakeRequested(bool ext1Wake, bool menu, bool up, bool down) {
  return ext1Wake && !menu && (up || down);
}

inline bool wakeButtonsReleased(bool menuHigh, bool upHigh, bool downHigh) {
  return menuHigh && upHigh && downHigh;
}
```

重跑 host test，Expected: `ALL PASS`。

- [ ] **Step 4: 建立 RTC 狀態及喚醒動作**

在 `main.cpp`：

```cpp
RTC_DATA_ATTR qlogic::QuoteRtcState g_rtc = {0, 0};
static uint64_t g_wakeMask = 0;

static qlogic::WakeAction wakeAction(uint64_t mask) {
  return qlogic::chooseWakeAction(
      (mask & (1ULL << BTN_MENU)) != 0,
      (mask & (1ULL << BTN_UP)) != 0,
      (mask & (1ULL << BTN_DOWN)) != 0);
}
```

讀取 wake cause 後，只有 TIMER／EXT1 令 `deepSleepWake=true`。立即呼叫
`normalizeRtcState(&g_rtc, deepSleepWake)`；非 deep-sleep 啟動 log 應顯示
`page=0 target=0`。

- [ ] **Step 5: 卡鍵只看三個喚醒鍵**

`waitButtonsReleased()` 條件固定為：

```cpp
if (qlogic::wakeButtonsReleased(digitalRead(BTN_MENU), digitalRead(BTN_UP),
                                digitalRead(BTN_DOWN))) {
  return true;
}
```

PRESS／EXIT 不得參與。stuck 時把 action 設 `None`，並由既有 300 秒
timer-only 路徑處理。

- [ ] **Step 6: 擴充 deep-sleep 設定**

`goToDeepSleep(targetUtc, enableExt1)` 在套用 24 小時 cap 前把最終 target 存進
`g_rtc.targetEpoch`；stuck override 時，最終 target 是 `now+300`。EXT1 mask：

```cpp
constexpr uint64_t BUTTON_WAKE_MASK =
    (1ULL << BTN_MENU) | (1ULL << BTN_UP) | (1ULL << BTN_DOWN);
esp_sleep_enable_ext1_wakeup(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
```

對三個 GPIO 逐一做 `rtc_gpio_init`、input-only、pull-up enable、pull-down disable。
保留先寫 zero mask 清除舊設定的 v1 防護。

- [ ] **Step 7: 在 Wi-Fi 前加入 cache-only page wake**

按鍵釋放後，以 `pageWakeRequested()` 判定 cache-only page wake。單鍵依 action
翻頁；UP＋DOWN 同時按時 action 是 None，頁碼不變，但仍只重畫目前快取頁，
不得連 Wi-Fi：

```cpp
g_rtc.pageIndex = qlogic::changedPage(g_rtc.pageIndex, action);  // None 保持原頁
qlogic::QuoteRecord cache;
if (loadCache(&cache)) {
  char ts[8];
  localHHMM(cache.savedEpoch, ts, sizeof ts);
  QuoteView view;
  viewFromRecord(&view, cache, g_rtc.pageIndex, ts, nullptr);
  uiShowQuotes(view);
  goToDeepSleep(qlogic::resumeTarget((uint32_t)time(nullptr), g_rtc.targetEpoch), true);
} else {
  uiShowMessage("NO DATA", "cache unavailable");
  uint32_t retry = qlogic::noCacheRetryTarget((uint32_t)time(nullptr),
                                               g_rtc.targetEpoch);
  goToDeepSleep(retry, true);
}
return;
```

這段必須位於 `quoteWifiBegin()` 前。MENU action 繼續走既有網路路徑；純 timer
wake 不得誤進 cache-only 分支。

- [ ] **Step 8: 加強 serial 可觀察性**

boot log 加 `page`／`target`；睡眠 log 加最終 target；翻頁 log：

```text
page cache 1->2
sleep 123s target=... ext1=1
```

不得把報價 payload、Wi-Fi 密碼或其他憑證寫入 log。

- [ ] **Step 9: 無硬體回歸驗證**

Run: Task 6 Step 4 的四組命令。

Expected: 全部通過；另以 `rg` 確認：

```sh
rg -n 'BTN_PRESS' src/main.cpp
rg -n 'BUTTON_WAKE_MASK|BTN_MENU|BTN_UP|BTN_DOWN' src/main.cpp
```

第一個結果不得出現在 `waitButtonsReleased()`；第二個須顯示三鍵 mask 與 RTC
pull-up 設定。

- [ ] **Step 10: Commit**

```sh
git status --short
git diff -- src/main.cpp src/quote_logic.h tests/host/test_quote_logic.cpp
git add src/main.cpp src/quote_logic.h tests/host/test_quote_logic.cpp
git commit -m "報價看板：加入三鍵喚醒與 RTC 快取翻頁

驗證等級：無硬體（host 測試全部通過＋pio run SUCCESS；按鍵與排程待實機）。"
```

---

### Task 8: 文件、全量檢查與 code review

**Files:**
- Modify: `README.md`
- Verify: all modified source and tests

- [ ] **Step 1: 更新 README 現況與操作**

將專案狀態改為 Quote Board v2 候選，列出兩頁清單及按鍵：MENU 立即更新、
UP／DOWN 快取翻頁、EXIT／PRESS 未使用。說明翻頁約 4.4 秒 full refresh、保留
目前頁、NVS v1 cache 升級後失效一次，以及修改清單須同步字型 manifest。

- [ ] **Step 2: 執行完整驗證並保存原始結果**

Run:

```sh
/tmp/opencode/pio-venv/bin/python -m unittest -v tests.host.test_gen_fonts
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
  tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic && \
  /tmp/opencode/test_quote_logic
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/GxEPD2/src \
  tests/host/test_rotation.cpp -o /tmp/opencode/test_rotation && \
  /tmp/opencode/test_rotation
/tmp/opencode/pio-venv/bin/pio run
git diff --check
git status --short
```

Expected: Python `OK`、quote `ALL PASS`、rotation `ok`、PIO `[SUCCESS]`、diff check
無輸出；status 只列本任務預期文件。

- [ ] **Step 3: 對 spec 做逐項 coverage review**

逐項核對：9 檔清單、單請求、個股容錯、部分失敗只顯示於所在頁、header
`1/2`、RTC page、原 timer target、無快取固定 retry deadline、三鍵 stuck、NVS
layout、字型拒絕條件。缺少任何一項先補測試／實作，不得只在文件註明。

- [ ] **Step 4: 發出 code review**

使用 `requesting-code-review`。Reviewer 必須讀 spec、檢查每個 commit、重跑完整
驗證，並以 blocker／high／medium／low 回報。若有 finding，回到負責該檔案的
Task，以先紅後綠方式修正並用該 Task 的明列檔案另建 commit，再從 Step 2 重跑。

- [ ] **Step 5: Commit 文件與 review 修正**

```sh
git status --short
git diff -- README.md
git add README.md
git commit -m "報價看板：完成雙頁八檔無硬體驗證與文件

驗證等級：無硬體（Python/C++ host 測試通過＋pio run SUCCESS；實機矩陣待驗證）。"
```

本任務不得修改或 stage `docs/device-research.md`；實測資料留到 Task 11。

---

### Task 9: 建立受控硬體 fixture environments

**Files:**
- Create: `src/quote_fixture.h`
- Create: `tests/host/test_quote_fixture.cpp`
- Create: `tests/host/test_fixture_wiring.py`
- Modify: `src/quote_store.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: 先寫 fixture 純邏輯測試**

建立 `tests/host/test_quote_fixture.cpp`，預期介面為：

```cpp
#include "quote_fixture.h"
#include <cassert>
#include <cstring>

int main() {
  qlogic::MarketBatch normal;
  fillQuoteFixture(&normal, "20260903", QuoteFixtureKind::Normal);
  assert(normal.rows[0].valid && normal.rows[6].valid);
  assert(strcmp(normal.date, "20260903") == 0);

  qlogic::MarketBatch untraded;
  fillQuoteFixture(&untraded, "20260903", QuoteFixtureKind::Untraded);
  assert(untraded.rows[5].valid);
  assert(untraded.rows[5].z == 0 && untraded.rows[5].y != 0);

  qlogic::MarketBatch partial;
  fillQuoteFixture(&partial, "20260903", QuoteFixtureKind::PartialFailure);
  assert(partial.rows[0].valid);
  assert(!partial.rows[6].valid);
  assert(partial.rows[6].z == 0 && partial.rows[6].y == 0);
  assert(partial.rows[6].t[0] == '\0');
}
```

Run:

```sh
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
  tests/host/test_quote_fixture.cpp -o /tmp/opencode/test_quote_fixture
```

Expected: compile failure，`quote_fixture.h` 不存在。

- [ ] **Step 2: 實作 checked-in fixture provider**

`quote_fixture.h` 不依賴 Arduino，定義 `QuoteFixtureKind::{Normal, Untraded,
PartialFailure}`。`fillQuoteFixture(out, date, kind)` 填入 9 個預期 code、固定有效
價格與 `13:30:00`；Untraded 把 index 5 設為 `valid=true,z=0,y!=0`；
PartialFailure 把 index 6 改成 canonical invalid row。date 由呼叫端傳入。

重跑 Step 1 compile 並執行 `/tmp/opencode/test_quote_fixture`，Expected: exit 0。

- [ ] **Step 3: 先寫 fixture wiring contract test**

建立 `tests/host/test_fixture_wiring.py`：

```python
import pathlib
import unittest

class FixtureWiringTest(unittest.TestCase):
    def test_quote_store_has_guarded_fixture_routes(self):
        source = pathlib.Path("src/quote_store.cpp").read_text()
        self.assertIn("QUOTE_TEST_PARTIAL_FIXTURE", source)
        self.assertIn("QUOTE_TEST_UNTRADED_FIXTURE", source)
        self.assertIn("QUOTE_TEST_FORCE_NO_CACHE", source)
        self.assertIn("fillQuoteFixture", source)
        self.assertIn("fixture partial", source)
        self.assertIn("fixture untraded", source)
        self.assertIn("fixture no cache", source)
```

Run:

```sh
/tmp/opencode/pio-venv/bin/python -m unittest -v tests.host.test_fixture_wiring
```

Expected: FAIL，現有 `quote_store.cpp` 沒有 fixture routing。

- [ ] **Step 4: 加入三個隔離 PlatformIO env 並串接 fixture**

在 `platformio.ini` 追加：

```ini
[env:esp32eink-fixture-partial]
extends = env:esp32eink
build_flags =
    ${env:esp32eink.build_flags}
    -DQUOTE_TEST_PARTIAL_FIXTURE

[env:esp32eink-fixture-untraded]
extends = env:esp32eink
build_flags =
    ${env:esp32eink.build_flags}
    -DQUOTE_TEST_UNTRADED_FIXTURE

[env:esp32eink-fixture-nocache]
extends = env:esp32eink
build_flags =
    ${env:esp32eink.build_flags}
    -DQUOTE_TEST_PARTIAL_FIXTURE
    -DQUOTE_TEST_FORCE_NO_CACHE
```

`quote_store.cpp` 在 `QUOTE_TEST_PARTIAL_FIXTURE`／`QUOTE_TEST_UNTRADED_FIXTURE`
下略過 HTTPS：以本地日期及對應 enum 呼叫 `fillQuoteFixture()`，分別 log
`fixture partial`／`fixture untraded`。僅在
`QUOTE_TEST_FORCE_NO_CACHE` 下讓 `quoteRecordLoad()` log `fixture no cache` 後回
false；production env 不定義任一 macro，路徑完全不變。

- [ ] **Step 5: 驗證四個 firmware env**

Run:

```sh
/tmp/opencode/pio-venv/bin/pio run -e esp32eink
/tmp/opencode/pio-venv/bin/pio run -e esp32eink-fixture-partial
/tmp/opencode/pio-venv/bin/pio run -e esp32eink-fixture-untraded
/tmp/opencode/pio-venv/bin/pio run -e esp32eink-fixture-nocache
/tmp/opencode/pio-venv/bin/python -m unittest -v \
  tests.host.test_fixture_wiring
```

Expected: 四者 `[SUCCESS]`。Production build log／binary 不得含 fixture 字串；以：

```sh
strings .pio/build/esp32eink/firmware.elf | rg 'fixture (partial|untraded|no cache)'
```

Expected: 無輸出、exit 1。

- [ ] **Step 6: Commit**

```sh
git status --short
git diff -- platformio.ini src/quote_store.cpp src/quote_fixture.h \
  tests/host/test_quote_fixture.cpp tests/host/test_fixture_wiring.py
git add platformio.ini src/quote_store.cpp src/quote_fixture.h \
  tests/host/test_quote_fixture.cpp tests/host/test_fixture_wiring.py
git commit -m "測試：加入報價部分失敗與無快取硬體 fixture

驗證等級：無硬體（fixture host test 與四個 PlatformIO env 編譯通過）。"
```

---

### Task 10: 硬體檢查點 A（畫面、翻頁與按鍵）

**Files:**
- No source changes unless a hardware defect is found
- Evidence: serial log under ignored `logs/`

- [ ] **Step 1: Build、upload、monitor**

```sh
/tmp/opencode/pio-venv/bin/pio run -e esp32eink
/tmp/opencode/pio-venv/bin/pio run -e esp32eink -t upload
/tmp/opencode/pio-venv/bin/pio device monitor -b 115200 -f log2file
```

記錄 commit、日期、USB 供電、bus/rotation 設定。不能宣稱電池結果。

- [ ] **Step 2: 驗證兩頁畫面**

目視確認：第 1 頁四檔原股票、第 2 頁 1513／2412／2881／2002；兩頁加權指數
一致，中文無缺字，header 不重疊且分別顯示 `1/2`、`2/2`。

- [ ] **Step 3: 驗證快取翻頁**

各按 UP／DOWN 至少兩輪，確認迴繞。Serial 在 page wake 期間不得出現
`wifi connected`、`ntp synced`、HTTP 或 `nvs save`；每次只允許 display init、
cache render、full refresh、sleep。

- [ ] **Step 4: 驗證頁碼保持與 MENU**

停在第 2 頁等待下一個 5 分 timer 更新，確認仍是 `2/2`。第 2 頁按 MENU，抓取
後仍是 `2/2`，並回到原市場排程。

- [ ] **Step 5: 驗證三鍵 stuck 與排除鍵**

分別按住 MENU／UP／DOWN 喚醒，確認 `stuck`、`sleep 300s`、`ext1=0`；放開後
恢復三鍵 EXT1。按住 EXIT、PRESS 再 reset，確認不觸發 stuck guard。

- [ ] **Step 6: 停下等待使用者回報**

未取得以上目視與 serial 證據前，不執行 Task 11，也不宣稱硬體通過。

---

### Task 11: 硬體檢查點 B（排程、容錯、收盤與長跑）

**Files:**
- Modify: `docs/device-research.md` only for observed facts
- Modify: `README.md` status after all checks pass

- [ ] **Step 1: 驗證翻頁撞邊界**

在 5 分鐘邊界前約 20 秒完成一次翻頁；serial 須顯示仍睡到即將到來的邊界，
下一輪執行正常 timer fetch，不可跳過 5 分鐘。

- [ ] **Step 2: 驗證未成交與部分失敗**

先燒錄合法未成交 fixture：

```sh
/tmp/opencode/pio-venv/bin/pio run -e esp32eink-fixture-untraded -t upload
/tmp/opencode/pio-venv/bin/pio device monitor -b 115200 -f log2file
```

Expected: log 有 `fixture untraded`；第 2 頁 1513 顯示 `--`，但兩頁底部都沒有
「部分失敗」。如果開機時段不會自動 fetch，monitor 連上後按 MENU；所有 fixture
畫面驗證都以看到對應 `fixture ...` log 為開始條件。再燒錄個股無效 fixture：

```sh
/tmp/opencode/pio-venv/bin/pio run -e esp32eink-fixture-partial -t upload
/tmp/opencode/pio-venv/bin/pio device monitor -b 115200 -f log2file
```

Expected: log 有 `fixture partial`；第 1 頁無「部分失敗」，第 2 頁 2412 顯示
`--`＋「部分失敗」，其他列仍有數字。不得以 `setInsecure()` 或修改正式 API
達成此測試。

- [ ] **Step 3: 驗證無快取 retry deadline**

NVS v1 blob 拒絕已由 Task 3 host layout/version 測試涵蓋；硬體不重建舊 blob。
本步只驗證無快取 retry deadline。燒錄 no-cache env：

```sh
/tmp/opencode/pio-venv/bin/pio run -e esp32eink-fixture-nocache -t upload
/tmp/opencode/pio-venv/bin/pio device monitor -b 115200 -f log2file
```

monitor 連上後先按 MENU，確認 `fixture partial` 並完成首次 render／睡眠；再按
UP，記錄 `fixture no cache` 與 retry target；60 秒
內再按 DOWN。第二次 log 的 target 必須與第一次完全相同，不得變成第二次按鍵
時間加 300 秒。

完成 fixture 測試後立即恢復 production firmware：

```sh
/tmp/opencode/pio-venv/bin/pio run -e esp32eink -t upload
/tmp/opencode/pio-venv/bin/pio device monitor -b 115200 -f log2file
```

確認 production monitor 沒有任何 `fixture` log，再繼續收盤與長跑驗證。

- [ ] **Step 4: 驗證 13:30／13:35 與定格翻頁**

13:30 不抓取並睡到 13:35；13:35 抓 9 列、寫 `nvs save (close)` 後長睡。收盤
定格後用 UP／DOWN 翻頁，兩頁日期／時間屬同一批資料，之後睡回次交易日 09:00。

- [ ] **Step 5: 執行 20+ 混合循環**

保存單一 monitor log，包含 timer、UP／DOWN、MENU 共 20 次以上 sleep-wake。
確認無 Busy Timeout、喚醒迴圈、頁碼越界、NVS sanity failure 或排程延後。

- [ ] **Step 6: 記錄實測並 final review**

把新的實機刷新時間、例外或硬體事實追加到 `docs/device-research.md`，附 commit、
量測方法、USB 供電與時間戳。README 狀態改為現行 v2。重跑 Task 8 Step 2，使用
下列 fixture tests，並使用 `requesting-code-review` 做 merge 前審查：

```sh
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
  tests/host/test_quote_fixture.cpp -o /tmp/opencode/test_quote_fixture && \
  /tmp/opencode/test_quote_fixture
/tmp/opencode/pio-venv/bin/python -m unittest -v tests.host.test_fixture_wiring
```

Expected: fixture binary exit 0、Python `OK`。

- [ ] **Step 7: Commit 硬體驗證記錄**

```sh
git add README.md docs/device-research.md
git commit -m "文件：記錄雙頁八檔報價看板實機驗證

驗證等級：有硬體（列出本次實際通過項目；未測項不得省略）。"
```

- [ ] **Step 8: 由使用者確認整合**

所有測試與 review 通過後，使用 `finishing-a-development-branch` 提供選項。只有
使用者明確同意，才 fast-forward 合併 `master`、建立 `quote-v2`、push branch／
tag；不可自行執行遠端操作。
