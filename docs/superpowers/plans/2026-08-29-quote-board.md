# 報價看板（Quote Board v1）實作計畫

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 直立安裝的 e-ink 報價看板：盤中 5 分邊界即時更新、收盤定格、假日跳過、MENU 立即更新。

**Architecture:** 快照式分支（`feature/quote-board`）。純邏輯（驗證/排程/blob）抽成 `src/quote_logic.h` 於 host 以 g++ 做單元測試；Arduino 端 `quote_store`（Wi-Fi/NTP/HTTPS/JSON/NVS）＋`ui`（`display.setRotation(1)` 統一旋轉直式版面）＋`main.cpp` 狀態機。TLS 採**釘選 TWCA Global Root CA**（查證：Arduino bundle API 需 `esp_crt_bundle_gen.py` 自製 blob 且無預設可取，故改 reviewer 選項 2）。

**Tech Stack:** PlatformIO + Arduino core ESP32 2.0.17（不動）、GxEPD2@1.6.9、U8g2_for_Adafruit_GFX@1.8.0、**重新引入 ArduinoJson@7.4.3**、Pillow+u8g2 bdfconv（字型子集工具）、g++（host 測試）。

**規格：** `docs/superpowers/specs/2026-08-29-quote-board-design.md`（修訂六版）
**平台工具：** `/tmp/opencode/pio-venv/bin/pio`；host 測試用 `g++ -std=c++17`。

**分支與合併慣例**（同相框）：從 master 開 `feature/quote-board`；每 Task 硬體檢查點由**使用者上機**回報後才 commit；commit 訊息 zh-TW，末行「驗證等級：…」。完成後合併回 master、標籤 `quote-v1`。

**驗證矩陣對應**（spec 22+2 項）：T2→#1；T3→#2,3,4,5；T4→#6；T6→#7,8(部分),9,12(數值來源)；T7→#10,11,13；T8→#12,14,15,16,17,18,19,20,22；T9→#21 全掃＋量測記錄。

---

### Task 1: 分支建立與工程重整

**Files:**
- Create: `feature/quote-board`（branch）
- Delete: `src/photo_store.h`, `src/photo_store.cpp`, `tools/raw_convert.py`
- Create: `src/watchlist.h`, `src/secrets.h.example`
- Modify: `platformio.ini`, `src/ui.h`, `src/ui.cpp`, `src/main.cpp`（改為報價版 stub）
- Verify: `.gitignore` 含 `src/secrets.h`

- [ ] **Step 1: 開分支並刪除相框檔案**

```bash
git checkout master && git pull origin master
git checkout -b feature/quote-board
git rm src/photo_store.h src/photo_store.cpp tools/raw_convert.py
```

- [ ] **Step 2: platformio.ini 重新引入 ArduinoJson**

在 `[env:esp32eink]` 的 `lib_deps` 加回（其餘版本不動）：

```ini
lib_deps =
    zinggjm/GxEPD2@1.6.9
    bblanchon/ArduinoJson@7.4.3
    olikraus/U8g2_for_Adafruit_GFX@1.8.0
```

- [ ] **Step 3: 建立 `src/watchlist.h`**

```cpp
#pragma once
#include <Arduino.h>

struct WatchItem {
  const char* code;   // API c 欄位
  const char* name;   // 直式版面顯示名
};

// 順序＝顯示順序（加權指數第一列）；v1 程式內固定（spec）
static const WatchItem WATCHLIST[] = {
  {"t00",    "加權指數"},
  {"2330",   "台積電"},
  {"2317",   "鴻海"},
  {"0050",   "元大台灣50"},
  {"006208", "富邦台50"},
};
static const int WATCH_N = 5;

// ex_ch 參數（與 WATCHLIST 同集合）
static const char* QUOTE_EX_CH =
  "tse_t00.tw|tse_2330.tw|tse_2317.tw|tse_0050.tw|tse_006208.tw";
```

- [ ] **Step 4: 建立 `src/secrets.h.example` 並確認 .gitignore**

`src/secrets.h.example`：

```cpp
#pragma once
// 複製為 src/secrets.h 並填入（secrets.h 已被 .gitignore 排除，不得 commit）
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-pass"
```

確認 `.gitignore` 含 `src/secrets.h`（缺則補）。

- [ ] **Step 5: 改寫 `src/ui.h`／`src/ui.cpp`／`src/main.cpp` 為可編譯 stub**

`src/ui.h`：

```cpp
#pragma once
#include <cstdint>

// 報價看板 UI（直式 272x792，display.setRotation(1) 統一旋轉）
struct QuoteView {
  const char* names[5];
  double z[5];
  double chg[5];
  double pct[5];
  char dateStr[20];    // "08-28 週五"
  char timeStr[8];     // "13:33"
  const char* status;  // nullptr=正常；"更新失敗"/"時間未同步"
};

void uiInit();
void uiShowQuotes(const QuoteView& v);
void uiShowMessage(const char* line1, const char* line2);
void uiHibernate();
void uiSleepHoldPins();
```

`src/ui.cpp`（stub，Task 7 補版面；先可編譯）：

```cpp
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
```

`src/main.cpp`（stub：編譯通過即可，Task 8 補狀態機）：

```cpp
#include <Arduino.h>
#include "log.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("quote-board stub\n");
  uiInit();
}

void loop() { delay(1000); }
```

- [ ] **Step 6: 編譯驗證**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS（stub 尚未引用 secrets/watchlist，可暫不建 `src/secrets.h`）

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "報價看板：分支建立與工程重整

刪除相框專屬檔案（photo_store/raw_convert）；重新引入
ArduinoJson@7.4.3；新增 watchlist 與 secrets 範本；ui/main 改
報價版 stub，display.setRotation(1) 契約先行落地。

驗證等級：編譯通過（無硬體）。"
```

---

### Task 2: 字型子集工具 `tools/gen_fonts.py`

**Files:**
- Create: `tools/gen_fonts.py`
- Create（產出、入庫）: `src/fonts_quote.c`, `src/fonts_quote.h`
- Toolchain: u8g2 `bdfconv`（clone 至 `/tmp/opencode/u8g2` 並編譯）

- [ ] **Step 1: 取得並編譯 bdfconv（固定版本 2.37.1，可重現性契約）**

```bash
git clone --depth 1 --branch 2.37.1 https://github.com/olikraus/u8g2 /tmp/opencode/u8g2
make -C /tmp/opencode/u8g2/tools/bdfconv
ls -la /tmp/opencode/u8g2/tools/bdfconv/bdfconv   # Expected: 執行檔存在
```

- [ ] **Step 2: 撰寫 `tools/gen_fonts.py`**

```python
#!/usr/bin/env python3
"""產生報價看板 U8g2 中文子集字型（PIL 渲染 → BDF → bdfconv → .c）。

可重現性契約（spec R5）：
- 字型來源：限 OFL 授權之 Noto Sans CJK Bold（禁用 PingFang 等系統字）
  探測路徑見 CANDIDATES；--font 可覆寫；產出檔頭記錄路徑＋sha256＋PIL/bdfconv 版本
- bdfconv 來源：u8g2 tag 2.37.1（clone --branch 2.37.1）
- glyph manifest 明列於下方常數（禁止隱式掃描）
- 產出 .c 提交 repo；僅 manifest 變更時重跑本工具
- 自檢：每 glyph 的 hex row 數必等於 BBX h；bdfconv 非零回傳即中止
"""
import argparse
import hashlib
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont
import PIL

CANDIDATES = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",       # Linux
    "/opt/homebrew/share/fonts/noto/NotoSansCJK-Bold.ttc",       # macOS Homebrew
    "/usr/local/share/fonts/noto/NotoSansCJK-Bold.ttc",          # macOS 手動安裝
]
U8G2_DIR = "/tmp/opencode/u8g2"
BDFCONV = os.path.join(U8G2_DIR, "tools/bdfconv/bdfconv")
OUT_C = "src/fonts_quote.c"
OUT_H = "src/fonts_quote.h"

# glyph manifest（明列；新增字元 = 修改這裡後重跑）
G16 = "週日一二三四五六更新失敗時間未同步0123456789:- "
G20 = "台積電鴻海元大灣富邦05"
G28 = "加權指數"
MANIFEST = {16: G16, 20: G20, 28: G28}


def resolve_font(cli_font: str | None) -> str:
    if cli_font:
        if not os.path.exists(cli_font):
            sys.exit(f"--font 指定路徑不存在：{cli_font}")
        return cli_font
    for p in CANDIDATES:
        if os.path.exists(p):
            return p
    sys.exit("找不到 Noto Sans CJK Bold；請以 --font 指定 OFL 授權之字型路徑")


def build_bdf(size: int, chars: str, path: str, font_path: str) -> None:
    font = ImageFont.truetype(font_path, size)
    ascent, _descent = font.getmetrics()
    canvas_h = size + 8
    lines = [
        "STARTFONT 2.1",
        f"FONT u8g2_font_quote{size}",
        f"SIZE {size} 72 72",
        f"FONTBOUNDINGBOX {size + 8} {canvas_h} 0 -{descent}",
        f"CHARS {len(chars)}",
    ]
    for ch in chars:
        cp = ord(ch)
        img = Image.new("1", (size + 8, canvas_h), 0)
        d = ImageDraw.Draw(img)
        d.text((2, 2 + ascent), ch, font=font, fill=1, anchor="ls")
        bbox = img.getbbox()
        dw = max(1, round(font.getlength(ch)))
        if bbox is None:
            # 空白類 glyph（如空格）：BBX 0 0、BITMAP 後無 hex row
            lines += [
                f"STARTCHAR u{cp:04X}",
                f"ENCODING {cp}",
                "SWIDTH 0 0",
                f"DWIDTH {dw} 0",
                "BBX 0 0 0 0",
                "BITMAP",
                "ENDCHAR",
            ]
            continue
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        stride = (w + 7) // 8
        pix = img.crop((x0, y0, x1, y1)).load()
        hexrows = []
        for ry in range(h):
            bits = "".join("1" if pix[rx, ry] else "0" for rx in range(w))
            bits = bits.ljust(stride * 8, "0")
            hexrows.append("".join(f"{int(bits[i*8:(i+1)*8], 2):02X}" for i in range(stride)))
        assert len(hexrows) == h, f"{ch!r}: hex rows {len(hexrows)} != BBX h {h}"
        yoff = (2 + ascent) - y1  # BDF y 向上為正：bitmap 底緣距 baseline
        lines += [
            f"STARTCHAR u{cp:04X}",
            f"ENCODING {cp}",
            "SWIDTH 0 0",
            f"DWIDTH {dw} 0",
            f"BBX {w} {h} {x0 - 2} {yoff}",
            "BITMAP",
            *hexrows,
            "ENDCHAR",
        ]
    lines.append("ENDFONT")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def run_bdfconv(bdf: str, out_c: str, name: str) -> None:
    r = subprocess.run([BDFCONV, bdf, "-o", out_c, "-n", name],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"bdfconv failed (rc={r.returncode}): {r.stderr}")


def extract_array(c_path: str) -> str:
    with open(c_path) as f:
        text = f.read()
    i = text.index("const uint8_t")
    return text[i:]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", help="覆寫字型路徑（限 OFL 授權之 Noto CJK）")
    args = ap.parse_args()
    if not os.path.exists(BDFCONV):
        sys.exit("bdfconv 不存在，先執行 Step 1")
    font_path = resolve_font(args.font)
    os.makedirs("src", exist_ok=True)
    sha = hashlib.sha256(open(font_path, "rb").read()).hexdigest()
    try:
        rev = subprocess.run(["git", "-C", U8G2_DIR, "describe", "--tags"],
                             capture_output=True, text=True).stdout.strip()
    except Exception:
        rev = "unknown"
    parts = []
    for size, chars in MANIFEST.items():
        bdf = f"/tmp/opencode/quote{size}.bdf"
        tmp_c = f"/tmp/opencode/quote{size}.c"
        build_bdf(size, chars, bdf, font_path)
        # 自檢：BDF 含全部 manifest glyph
        body = open(bdf).read()
        missing = [c for c in chars if f"ENCODING {ord(c)}\n" not in body]
        if missing:
            sys.exit(f"size {size} 缺 glyph: {missing}")
        run_bdfconv(bdf, tmp_c, f"u8g2_font_quote{size}")
        parts.append(extract_array(tmp_c))
        print(f"size {size}: {len(chars)} glyphs ok (bdfconv rc=0)")
    banner = (
        "// 自動產生：tools/gen_fonts.py（勿手改）\n"
        f"// font: {font_path} sha256={sha[:16]}... (SIL OFL 1.0)\n"
        f"// PIL {PIL.__version__}, bdfconv u8g2@{rev}\n"
        "#include <stdint.h>\n"
        "#ifndef U8G2_FONT_SECTION\n"
        "#define U8G2_FONT_SECTION(s)\n"
        "#endif\n"
    )
    with open(OUT_C, "w") as f:
        f.write(banner + "\n".join(parts))
    with open(OUT_H, "w") as f:
        f.write(
            "// 自動產生對應宣告：tools/gen_fonts.py（勿手改）\n#pragma once\n#include <stdint.h>\n"
            "extern const uint8_t u8g2_font_quote16[];\n"
            "extern const uint8_t u8g2_font_quote20[];\n"
            "extern const uint8_t u8g2_font_quote28[];\n"
        )
    print(f"ok -> {OUT_C}, {OUT_H}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: 執行並驗證**

```bash
/tmp/opencode/pio-venv/bin/python tools/gen_fonts.py
```

Expected: 三組 size 各印 `glyphs ok`，`src/fonts_quote.c`／`src/fonts_quote.h` 產生。
再檢查：

```bash
rtk rg -c 'extern const uint8_t' src/fonts_quote.h    # Expected: 3（含 extern 行）
rtk rg -n 'u8g2_font_quote28' src/fonts_quote.c | head -2   # Expected: 陣列定義存在
```

- [ ] **Step 4: Commit**

```bash
git add tools/gen_fonts.py src/fonts_quote.c src/fonts_quote.h
git commit -m "報價看板：中文子集字型工具與產出

PIL 渲染 Noto CJK Bold → BDF → bdfconv → .c；manifest 明列
（16px header/狀態字＋ASCII、20px 個股名、28px 加權指數）；
字型 sha256/PIL/bdfconv 版本記錄於檔頭；產出入庫。

驗證等級：無硬體（host 自檢 manifest 覆蓋）。"
```

---

### Task 3: `src/quote_logic.h`（一）驗證/計算/時間 ＋ host 測試

**Files:**
- Create: `src/quote_logic.h`
- Create: `tests/host/test_quote_logic.cpp`, `tests/host/test_rotation.cpp`
- Host 編譯需 ArduinoJson include：`-I .pio/libdeps/esp32eink/ArduinoJson/src`

- [ ] **Step 1: 寫失敗測試 `tests/host/test_quote_logic.cpp`**

```cpp
// host 測試：g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src tests/host/test_quote_logic.cpp
#include "quote_logic.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void testParseNum() {
  double v = 0;
  assert(qlogic::parseNum("2420.0000", &v) && v == 2420.0);
  assert(qlogic::parseNum("0.00", &v) && v == 0.0);
  assert(!qlogic::parseNum("-", &v));
  assert(!qlogic::parseNum("", &v));
  assert(!qlogic::parseNum("abc", &v));
  assert(!qlogic::parseNum("24 20", &v));
  printf("parseNum ok\n");
}

static void testValidateBatch() {
  qlogic::RawBatch raw = {};
  const char* codes[5] = {"t00", "2330", "2317", "0050", "006208"};
  const char* z[5] = {"46331.45", "2420.0000", "253.0000", "106.9500", "245.1000"};
  const char* y[5] = {"45975.22", "2410.0000", "252.0000", "106.0500", "243.1500"};
  const char* t[5] = {"13:33:00", "13:30:00", "13:30:00", "13:30:00", "13:30:00"};
  for (int i = 0; i < 5; i++) {
    strcpy(raw.rows[i].code, codes[i]);
    strcpy(raw.rows[i].name, "x");
    strcpy(raw.rows[i].z, z[i]);
    strcpy(raw.rows[i].y, y[i]);
    strcpy(raw.rows[i].t, t[i]);
    strcpy(raw.rows[i].d, "20260828");
  }
  qlogic::MarketBatch out;
  assert(qlogic::validateBatch(raw, &out) == qlogic::V_OK);
  assert(strcmp(out.date, "20260828") == 0);
  assert(strcmp(out.quoteTime, "13:33:00") == 0);
  assert(out.rows[0].z == 46331.45 && out.rows[0].y == 45975.22);

  // 缺列（漏掉 006208）
  qlogic::RawBatch miss = raw;
  miss.rows[4].code[0] = '\0';
  assert(qlogic::validateBatch(miss, &out) == qlogic::V_STRUCT);

  // 重複代碼
  qlogic::RawBatch dup = raw;
  strcpy(dup.rows[4].code, "2330");
  assert(qlogic::validateBatch(dup, &out) == qlogic::V_STRUCT);

  // z 為 "-"
  qlogic::RawBatch dash = raw;
  strcpy(dash.rows[2].z, "-");
  assert(qlogic::validateBatch(dash, &out) == qlogic::V_NUMERIC);

  // y == 0
  qlogic::RawBatch zero = raw;
  strcpy(zero.rows[1].y, "0.0000");
  assert(qlogic::validateBatch(zero, &out) == qlogic::V_NUMERIC);

  // d 格式錯誤
  qlogic::RawBatch badd = raw;
  strcpy(badd.rows[3].d, "2026-8-2");
  assert(qlogic::validateBatch(badd, &out) == qlogic::V_FORMAT);

  // t 格式錯誤
  qlogic::RawBatch badt = raw;
  strcpy(badt.rows[3].t, "9:30:00");
  assert(qlogic::validateBatch(badt, &out) == qlogic::V_FORMAT);

  // 五列 d 不一致
  qlogic::RawBatch mixd = raw;
  strcpy(mixd.rows[2].d, "20260827");
  assert(qlogic::validateBatch(mixd, &out) == qlogic::V_DATE_DIFF);

  printf("validateBatch ok\n");
}

static void testCalc() {
  qlogic::QuoteCalc c = qlogic::calcQuote(2420.0, 2410.0);
  assert(c.chg == 10.0);
  assert(fabs(c.pct - 0.4149378) < 1e-6);
  c = qlogic::calcQuote(100.0, 110.0);
  assert(c.chg == -10.0 && c.pct < 0);
  c = qlogic::calcQuote(50.0, 50.0);
  assert(c.chg == 0.0 && c.pct == 0.0);
  printf("calc ok\n");
}

static void testFormatPrice() {
  char buf[24];
  qlogic::formatPrice(46331.45, buf, sizeof buf);
  assert(strcmp(buf, "46,331.45") == 0);
  qlogic::formatPrice(2420.0, buf, sizeof buf);
  assert(strcmp(buf, "2,420.00") == 0);
  qlogic::formatPrice(253.0, buf, sizeof buf);
  assert(strcmp(buf, "253.00") == 0);
  printf("formatPrice ok\n");
}

static void testCivil() {
  // 2026-08-28 00:00 台北 = 1787846400 UTC；週五
  qlogic::Civil c = qlogic::civilFromEpoch(1787846400u, qlogic::TZ_TW);
  assert(c.y == 2026 && c.m == 8 && c.d == 28 && c.wday == 5);
  char buf[24];
  qlogic::formatDateTW("20260828", buf, sizeof buf);
  assert(strcmp(buf, "08-28 週五") == 0);
  qlogic::formatDateTW("20260830", buf, sizeof buf);
  assert(strcmp(buf, "08-30 週日") == 0);
  char d[16];
  qlogic::dateOfEpoch(1787846400u, qlogic::TZ_TW, d, sizeof d);
  assert(strcmp(d, "20260828") == 0);
  printf("civil ok\n");
}

static void testParseJson() {
  static const char* FIX_OK =
      "{\"msgArray\":["
      "{\"c\":\"t00\",\"n\":\"發行量加權股價指數\",\"z\":\"46331.45\",\"y\":\"45975.22\",\"t\":\"13:33:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2330\",\"n\":\"台積電\",\"z\":\"2420.0000\",\"y\":\"2410.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2317\",\"n\":\"鴻海\",\"z\":\"253.0000\",\"y\":\"252.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"0050\",\"n\":\"元大台灣50\",\"z\":\"106.9500\",\"y\":\"106.0500\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"006208\",\"n\":\"富邦台50\",\"z\":\"245.1000\",\"y\":\"243.1500\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}";
  qlogic::RawBatch raw;
  assert(qlogic::parseJsonToRaw(FIX_OK, strlen(FIX_OK), &raw) == qlogic::V_OK);
  qlogic::MarketBatch out;
  assert(qlogic::validateBatch(raw, &out) == qlogic::V_OK);
  assert(out.rows[0].z == 46331.45);

  // 壞 JSON
  assert(qlogic::parseJsonToRaw("{bad", 5, &raw) == qlogic::V_JSON);
  // 無 msgArray
  static const char* FIX_NOARR = "{\"rtcode\":\"0000\"}";
  assert(qlogic::parseJsonToRaw(FIX_NOARR, strlen(FIX_NOARR), &raw) == qlogic::V_JSON);
  // 列數不等於 5
  static const char* FIX_4ROWS =
      "{\"msgArray\":[{\"c\":\"t00\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2330\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2317\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"0050\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}";
  assert(qlogic::parseJsonToRaw(FIX_4ROWS, strlen(FIX_4ROWS), &raw) == qlogic::V_JSON);
  // 未知代碼
  static const char* FIX_UNK =
      "{\"msgArray\":[{\"c\":\"t00\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2330\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2317\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"0050\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"9999\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}";
  assert(qlogic::parseJsonToRaw(FIX_UNK, strlen(FIX_UNK), &raw) == qlogic::V_STRUCT);
  printf("parseJson ok\n");
}

static void testValidDate() {
  assert(qlogic::validDate("20260828"));
  assert(qlogic::validDate("20240229"));   // 閏年
  assert(!qlogic::validDate("20260231"));  // 不存在的日期
  assert(!qlogic::validDate("20230229"));  // 非閏年
  assert(!qlogic::validDate("20261301"));  // 月 13
  assert(!qlogic::validDate("20260010"));  // 月 0
  assert(!qlogic::validDate("2026-828"));
  assert(!qlogic::validDate("2026082"));
  printf("validDate ok\n");
}

int main() {
  testParseNum();
  testValidDate();
  testParseJson();
  testValidateBatch();
  testCalc();
  testFormatPrice();
  testCivil();
  printf("ALL PASS\n");
  return 0;
}
```

- [ ] **Step 2: 執行測試確認失敗**

Run: `g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic`
Expected: **FAIL**（`src/quote_logic.h` 不存在）

- [ ] **Step 3: 寫失敗測試 `tests/host/test_rotation.cpp`（golden test，矩陣 #4）**

```cpp
// 旋轉 golden test：鎖定 display.setRotation(1) 之座標映射契約
// 映射抄自 Adafruit_GFX.cpp drawPixel case 1（WIDTH/HEIGHT 為 raw 成員變數，
// 查證：Adafruit_GFX.cpp:2104-2107；setRotation 為 _width=HEIGHT、_height=WIDTH）
// rotation 1：(x, y) → (RAW_W - 1 - y, x)；邏輯 272x792 → 實體 792x272
#include <cassert>
#include <cstdio>

static const int RAW_W = 792, RAW_H = 272;
static const int LOG_W = RAW_H, LOG_H = RAW_W;   // 272, 792

static void mapRot1(int x, int y, int* px, int* py) {
  *px = RAW_W - 1 - y;
  *py = x;
}

int main() {
  // 四角落
  int px, py;
  mapRot1(0, 0, &px, &py);      assert(px == 791 && py == 0);
  mapRot1(LOG_W - 1, 0, &px, &py); assert(px == 791 && py == 271);
  mapRot1(0, LOG_H - 1, &px, &py); assert(px == 0 && py == 0);
  mapRot1(LOG_W - 1, LOG_H - 1, &px, &py); assert(px == 0 && py == 271);

  // 全域在界內（掃邊框 + 對角線取樣足以鎖契約）
  for (int x = 0; x < LOG_W; x += 17) {
    for (int y = 0; y < LOG_H; y += 17) {
      mapRot1(x, y, &px, &py);
      assert(px >= 0 && px < RAW_W && py >= 0 && py < RAW_H);
    }
  }

  // 双射性：旋轉不重疊（取樣 ax+by 線性組合驗證）
  // 上漲三角形（apex 在上）經旋轉後仍為三角形且面積不變（剛性變換）
  // 頂點：logical (100,200),(130,200),(115,180) → 面積
  auto area2 = [](int x1, int y1, int x2, int y2, int x3, int y3) {
    return (x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1);
  };
  int a0 = area2(100, 200, 130, 200, 115, 180);
  int x1p, y1p, x2p, y2p, x3p, y3p;
  mapRot1(100, 200, &x1p, &y1p);
  mapRot1(130, 200, &x2p, &y2p);
  mapRot1(115, 180, &x3p, &y3p);
  int a1 = area2(x1p, y1p, x2p, y2p, x3p, y3p);
  assert(abs(a0) == abs(a1));   // 剛性旋轉：面積不變、無翻轉比例失真
  printf("rotation golden ok\n");
  return 0;
}
```

- [ ] **Step 4: 執行確認失敗後，實作 `src/quote_logic.h`（一）**

```cpp
#pragma once
// 純邏輯（無 Arduino 依賴）：欄位驗證、漲跌計算、時間/星期、價格格式、JSON 解析
// host 測試：tests/host/test_quote_logic.cpp
//   g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src ...
#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace qlogic {

static const int TZ_TW = 8 * 3600;
static const char* EXPECT_CODES[5] = {"t00", "2330", "2317", "0050", "006208"};

enum {
  V_OK = 0,
  V_STRUCT = -1,    // 缺列/重複/代碼不符
  V_NUMERIC = -2,   // z/y 非有限數字或 y==0
  V_FORMAT = -3,    // d/t/n 格式錯誤
  V_DATE_DIFF = -4, // 五列 d 不一致
  V_JSON = -5,      // JSON 結構無效
};

struct QuoteRow {
  char code[12];
  double z;
  double y;
  char t[9];
};

struct RawQuote {
  char code[12];
  char name[32];
  char z[16];
  char y[16];
  char t[9];
  char d[9];
};

struct RawBatch {
  RawQuote rows[5];
};

struct MarketBatch {
  QuoteRow rows[5];
  char date[9];
  char quoteTime[9];
};

// Howard Hinnant civil_from_days / days_from_civil（公有領域）
inline void civilFromDays(int64_t z, int* y, unsigned* m, unsigned* d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = static_cast<unsigned>(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t yy = static_cast<int64_t>(yoe) + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned dd = doy - (153 * mp + 2) / 5 + 1;
  unsigned mm = mp < 10 ? mp + 3 : mp - 9;
  *y = static_cast<int>(yy + (mm <= 2));
  *m = mm;
  *d = dd;
}

inline int64_t daysFromCivil(int y, int m, int d) {
  y -= (m <= 2);
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = static_cast<unsigned>(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// 真日曆驗證（round-trip）：拒絕 20260231 這類不存在的日期
inline bool validDate(const char* s) {
  if (strlen(s) != 8) return false;
  for (int i = 0; i < 8; i++)
    if (s[i] < '0' || s[i] > '9') return false;
  int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
  int mo = (s[4] - '0') * 10 + (s[5] - '0');
  int da = (s[6] - '0') * 10 + (s[7] - '0');
  if (mo < 1 || mo > 12 || da < 1) return false;
  int y2;
  unsigned m2, d2;
  civilFromDays(daysFromCivil(y, mo, da), &y2, &m2, &d2);
  return y2 == y && m2 == static_cast<unsigned>(mo) && d2 == static_cast<unsigned>(da);
}

inline bool validTime(const char* s) {
  if (strlen(s) != 8) return false;
  for (int i = 0; i < 8; i++) {
    if (i == 2 || i == 5) {
      if (s[i] != ':') return false;
    } else if (s[i] < '0' || s[i] > '9') {
      return false;
    }
  }
  int hh = (s[0] - '0') * 10 + (s[1] - '0');
  int mm = (s[3] - '0') * 10 + (s[4] - '0');
  int ss = (s[6] - '0') * 10 + (s[7] - '0');
  return hh < 24 && mm < 60 && ss < 60;
}

inline bool parseNum(const char* s, double* out) {
  if (s == nullptr || *s == '\0') return false;
  if (strcmp(s, "-") == 0) return false;
  char* end = nullptr;
  double v = strtod(s, &end);
  if (end == s || *end != '\0') return false;
  if (!std::isfinite(v)) return false;
  *out = v;
  return true;
}

inline int validateBatch(const RawBatch& in, MarketBatch* out) {
  bool seen[5] = {false, false, false, false, false};
  char date[9] = {0};
  char latestT[9] = {0};
  for (int i = 0; i < 5; i++) {
    const RawQuote& r = in.rows[i];
    int idx = -1;
    for (int j = 0; j < 5; j++) {
      if (strcmp(r.code, EXPECT_CODES[j]) == 0) {
        idx = j;
        break;
      }
    }
    if (idx < 0 || seen[idx]) return V_STRUCT;
    seen[idx] = true;
    if (r.name[0] == '\0') return V_FORMAT;
    double z, y;
    if (!parseNum(r.z, &z)) return V_NUMERIC;
    if (!parseNum(r.y, &y)) return V_NUMERIC;
    if (y == 0.0) return V_NUMERIC;
    if (!validTime(r.t)) return V_FORMAT;
    if (!validDate(r.d)) return V_FORMAT;
    if (date[0] == '\0') {
      strncpy(date, r.d, 8);
      date[8] = '\0';
    } else if (strncmp(date, r.d, 8) != 0) {
      return V_DATE_DIFF;
    }
    if (strcmp(r.t, latestT) > 0) {
      strncpy(latestT, r.t, 8);
      latestT[8] = '\0';
    }
    strncpy(out->rows[idx].code, EXPECT_CODES[idx], 11);
    out->rows[idx].code[11] = '\0';
    out->rows[idx].z = z;
    out->rows[idx].y = y;
    strncpy(out->rows[idx].t, r.t, 8);
    out->rows[idx].t[8] = '\0';
  }
  for (int j = 0; j < 5; j++)
    if (!seen[j]) return V_STRUCT;
  strncpy(out->date, date, 8);
  out->date[8] = '\0';
  strncpy(out->quoteTime, latestT, 8);
  out->quoteTime[8] = '\0';
  return V_OK;
}

// API JSON → RawBatch（ArduinoJson v7；host 可測）。
// 契約：msgArray 恰 5 列、每列 c 必為預期代碼（未知代碼→V_STRUCT）。
// 回傳 V_OK（填入完成，順序＝API 回傳順序）或 V_JSON/V_STRUCT。
inline int parseJsonToRaw(const char* body, size_t len, RawBatch* out) {
  JsonDocument doc;
  if (deserializeJson(doc, body, len)) return V_JSON;
  JsonArray arr = doc["msgArray"];
  if (arr.isNull() || arr.size() != 5) return V_JSON;
  *out = RawBatch{};
  int k = 0;
  for (JsonObject row : arr) {
    const char* c = row["c"] | "";
    int j = -1;
    for (int i = 0; i < 5; i++) {
      if (strcmp(c, EXPECT_CODES[i]) == 0) {
        j = i;
        break;
      }
    }
    if (j < 0) return V_STRUCT;
    strncpy(out->rows[k].code, c, sizeof(out->rows[k].code) - 1);
    strncpy(out->rows[k].name, row["n"] | "", sizeof(out->rows[k].name) - 1);
    strncpy(out->rows[k].z, row["z"] | "-", sizeof(out->rows[k].z) - 1);
    strncpy(out->rows[k].y, row["y"] | "-", sizeof(out->rows[k].y) - 1);
    strncpy(out->rows[k].t, row["t"] | "", sizeof(out->rows[k].t) - 1);
    strncpy(out->rows[k].d, row["d"] | "", sizeof(out->rows[k].d) - 1);
    k++;
  }
  return V_OK;
}

struct QuoteCalc {
  double chg;
  double pct;
};

inline QuoteCalc calcQuote(double z, double y) {
  QuoteCalc c;
  c.chg = z - y;
  c.pct = (y != 0.0) ? (c.chg / y * 100.0) : 0.0;
  return c;
}

// 千分位價格：46331.45 → "46,331.45"
inline void formatPrice(double v, char* buf, int cap) {
  char raw[32];
  snprintf(raw, sizeof raw, "%.2f", v);
  const char* dot = strchr(raw, '.');
  int intLen = (int)(dot - raw);
  int p = 0;
  for (int i = 0; i < intLen && p < cap - 1; i++) {
    if (i > 0 && (intLen - i) % 3 == 0) buf[p++] = ',';
    if (p >= cap - 1) break;
    buf[p++] = raw[i];
  }
  buf[p] = '\0';
  strncat(buf, dot, cap - 1 - p);
}

struct Civil {
  int y, m, d, hh, mm2, ss, wday;  // wday 0=Sun
};

inline Civil civilFromEpoch(uint32_t utc, int tzOffsetSec) {
  int64_t local = static_cast<int64_t>(utc) + tzOffsetSec;
  int64_t days = local / 86400;
  int64_t rem = local % 86400;
  Civil c;
  civilFromDays(days, &c.y, reinterpret_cast<unsigned*>(&c.m),
                reinterpret_cast<unsigned*>(&c.d));
  c.hh = static_cast<int>(rem / 3600);
  c.mm2 = static_cast<int>((rem % 3600) / 60);
  c.ss = static_cast<int>(rem % 60);
  c.wday = static_cast<int>((days + 4) % 7);
  return c;
}

inline const char* weekdayHan(int wday) {
  static const char* W[7] = {"週日", "週一", "週二", "週三", "週四", "週五", "週六"};
  return W[wday % 7];
}

inline void formatDateTW(const char* date, char* buf, int cap) {
  if (!validDate(date)) {
    snprintf(buf, cap, "\?\?-\?\?");   // \?\? 避免 trigraph
    return;
  }
  int y = (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + (date[3] - '0');
  int mo = (date[4] - '0') * 10 + (date[5] - '0');
  int da = (date[6] - '0') * 10 + (date[7] - '0');
  int64_t days = daysFromCivil(y, mo, da);
  int wday = static_cast<int>((days + 4) % 7);
  snprintf(buf, cap, "%c%c-%c%c %s", date[4], date[5], date[6], date[7], weekdayHan(wday));
}

inline void dateOfEpoch(uint32_t utc, int tz, char* buf, int cap) {
  Civil c = civilFromEpoch(utc, tz);
  snprintf(buf, cap, "%04d%02d%02d", c.y, c.m, c.d);
}

}  // namespace qlogic
```

- [ ] **Step 5: 執行兩個測試確認通過**

```bash
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic && /tmp/opencode/test_quote_logic
g++ -std=c++17 tests/host/test_rotation.cpp -o /tmp/opencode/test_rotation && /tmp/opencode/test_rotation
```

Expected: 兩者 `ALL PASS`／`rotation golden ok`

- [ ] **Step 6: Commit**

```bash
git add src/quote_logic.h tests/host/
git commit -m "報價看板：純邏輯驗證/計算/時間與旋轉 golden test

欄位驗證 all-or-nothing（含五列 d 一致）、漲跌計算、千分位價格、
civil 曆法與週別；rotation golden test 鎖定 setRotation(1)
映射契約（Adafruit_GFX.cpp case 1 查證）。g++ host 測試全過。

驗證等級：無硬體（host 單元測試）。"
```

---

### Task 4: `src/quote_logic.h`（二）排程與 blob ＋ host 測試

**Files:**
- Modify: `src/quote_logic.h`（附加排程與 blob 段落）
- Modify: `tests/host/test_quote_logic.cpp`（附加測試）

- [ ] **Step 1: 附加失敗測試（在 `main()` 前加入，並於 `main()` 呼叫）**

```cpp
static void testSchedule() {
  using qlogic::marketState;
  // 2026-08-28 = 週五；00:00 台北 = 1787846400 UTC
  uint32_t fri00 = 1787846400u;
  assert(marketState(fri00 + 7 * 3600) == qlogic::MarketState::PreMarket);
  assert(marketState(fri00 + 9 * 3600) == qlogic::MarketState::Trading);
  assert(marketState(fri00 + 13 * 3600 + 29 * 60) == qlogic::MarketState::Trading);
  assert(marketState(fri00 + 13 * 3600 + 30 * 60) == qlogic::MarketState::PostClose);
  assert(marketState(fri00 + 2 * 86400) == qlogic::MarketState::Weekend);  // 週日
  assert(marketState(fri00 + 86400) == qlogic::MarketState::Weekend);      // 週六

  // PRE_MARKET → 當日 09:00
  assert(qlogic::todayAt9(fri00 + 7 * 3600) == fri00 + 9 * 3600);

  // POST_CLOSE 週五 14:00 → 次交易日（週一）09:00
  uint32_t fri14 = fri00 + 14 * 3600;
  assert(qlogic::nextWeekdayAt9(fri14) == fri00 + 3 * 86400 + 9 * 3600);

  // 週六 10:00 → 週一 09:00
  assert(qlogic::nextWeekdayAt9(fri00 + 86400 + 10 * 3600) == fri00 + 3 * 86400 + 9 * 3600);

  // 週三 14:00 → 週四 09:00
  uint32_t wed14 = fri00 - 2 * 86400 + 14 * 3600;
  assert(qlogic::nextWeekdayAt9(wed14) == fri00 - 86400 + 9 * 3600);

  // 假日（TRADING 中 d!=today）→ 隔日 09:00（含跨週末場景由呼叫端決定）
  assert(qlogic::nextDayAt9(fri00 + 10 * 3600) == fri00 + 86400 + 9 * 3600);

  // 5 分邊界：09:02:10 → 09:05:00（170s）；09:04:50 → 跳 09:10（310s，最小 30s）
  uint32_t t1 = fri00 + 9 * 3600 + 2 * 60 + 10;
  assert(qlogic::nextTradingBoundary(t1) - t1 == 170);
  uint32_t t2 = fri00 + 9 * 3600 + 4 * 60 + 50;
  assert(qlogic::nextTradingBoundary(t2) - t2 == 310);

  // 24h cap
  assert(qlogic::capSleep(fri00, fri00 + 3 * 86400 + 9 * 3600) == 86400);
  assert(qlogic::capSleep(fri00, fri00 + 100) == 100);
  printf("schedule ok\n");
}

static void testBlob() {
  qlogic::QuoteRecord a = {};
  a.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < 5; i++) {
    strcpy(a.rows[i].code, qlogic::EXPECT_CODES[i]);
    a.rows[i].z = 10.0 + i;
    a.rows[i].y = 9.0 + i;
    strcpy(a.rows[i].t, "13:30:00");
  }
  strcpy(a.quoteDate, "20260828");
  strcpy(a.quoteTime, "13:30:00");
  a.savedEpoch = 1787894400u;

  qlogic::QuoteRecord b = a;
  assert(!qlogic::recordDiffers(a, b));
  b.rows[1].z = 2420.0;
  assert(qlogic::recordDiffers(a, b));       // 價格變更
  b = a; strcpy(b.quoteDate, "20260831");
  assert(qlogic::recordDiffers(a, b));       // 交易日變更
  b = a; strcpy(b.lastCloseDate, "20260828");
  assert(qlogic::recordDiffers(a, b));       // 定格旗標變更
  b = a; strcpy(b.quoteTime, "13:31:00");
  assert(!qlogic::recordDiffers(a, b));      // 僅時間變更不寫（spec 修訂四）
  b = a; b.savedEpoch++;
  assert(!qlogic::recordDiffers(a, b));      // 僅 savedEpoch 不寫

  assert(qlogic::recordSane(a));
  b = a; b.version = 2;
  assert(!qlogic::recordSane(b));
  b = a; b.rows[3].y = 0.0;
  assert(!qlogic::recordSane(b));
  b = a; strcpy(b.quoteDate, "2026-8-28");
  assert(!qlogic::recordSane(b));

  // 序列化 roundtrip（putBytes 語意）
  char bytes[sizeof(qlogic::QuoteRecord)];
  memcpy(bytes, &a, sizeof a);
  qlogic::QuoteRecord c;
  memcpy(&c, bytes, sizeof c);
  assert(memcmp(&a, &c, sizeof a) == 0);
  printf("blob ok\n");
}
```

（`main()` 內加 `testSchedule(); testBlob();`）

- [ ] **Step 2: 執行確認失敗**

Run: `g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic && /tmp/opencode/test_quote_logic`
Expected: FAIL（`marketState`/`recordDiffers` 未定義）

- [ ] **Step 3: 實作（附加至 `qlogic` namespace 末尾）**

```cpp
// ---- 排程（spec 修訂三版狀態機）----
enum class MarketState { PreMarket, Trading, PostClose, Weekend };

inline MarketState marketState(uint32_t utc) {
  Civil c = civilFromEpoch(utc, TZ_TW);
  int secs = c.hh * 3600 + c.mm2 * 60 + c.ss;
  if (c.wday == 0 || c.wday == 6) return MarketState::Weekend;
  if (secs >= 9 * 3600 && secs < 13 * 3600 + 30 * 60) return MarketState::Trading;
  if (secs < 9 * 3600) return MarketState::PreMarket;
  return MarketState::PostClose;
}

inline uint32_t atLocalTime(uint32_t utcNow, int dayOffset, int hh, int minute) {
  Civil c = civilFromEpoch(utcNow, TZ_TW);
  int64_t days = daysFromCivil(c.y, c.m, c.d) + dayOffset;
  return static_cast<uint32_t>(days * 86400 + hh * 3600 + minute * 60 - TZ_TW);
}

inline uint32_t todayAt9(uint32_t utcNow) { return atLocalTime(utcNow, 0, 9, 0); }
inline uint32_t nextDayAt9(uint32_t utcNow) { return atLocalTime(utcNow, 1, 9, 0); }

inline uint32_t nextWeekdayAt9(uint32_t utcNow) {
  for (int off = 1; off <= 7; off++) {
    uint32_t t = atLocalTime(utcNow, off, 9, 0);
    Civil c = civilFromEpoch(t, TZ_TW);
    if (c.wday >= 1 && c.wday <= 5) return t;
  }
  return nextDayAt9(utcNow);  // 不可達
}

// 5 分邊界對齊＋最小安全等待 30s（spec R1）
inline uint32_t nextTradingBoundary(uint32_t utcNow) {
  uint32_t aligned = utcNow - (utcNow % 300) + 300;
  if (aligned - utcNow < 30) aligned += 300;
  return aligned;
}

// 單段睡眠上限 24h（超過由呼叫端喚醒後重算，分段）
inline uint32_t capSleep(uint32_t now, uint32_t target) {
  if (target <= now) return 1;
  uint32_t delta = target - now;
  return delta > 86400 ? 86400 : delta;
}

// ---- NVS blob（單一 versioned record，spec 修訂四版）----
static const uint32_t BLOB_VERSION = 1;

struct QuoteRecord {
  uint32_t version;
  QuoteRow rows[5];
  char quoteDate[9];      // 快取交易日（YYYYMMDD）
  char quoteTime[9];      // 5 列最新有效 t
  char lastCloseDate[9];  // 收盤定格旗標（""=未定格）
  uint32_t savedEpoch;    // 最後持久化時間（UTC）
};

inline bool recordSane(const QuoteRecord& r) {
  if (r.version != BLOB_VERSION) return false;
  if (!validDate(r.quoteDate) || !validTime(r.quoteTime)) return false;
  if (r.lastCloseDate[0] != '\0' && !validDate(r.lastCloseDate)) return false;
  for (int i = 0; i < 5; i++) {
    if (strcmp(r.rows[i].code, EXPECT_CODES[i]) != 0) return false;
    if (!std::isfinite(r.rows[i].z) || !std::isfinite(r.rows[i].y)) return false;
    if (r.rows[i].y == 0.0) return false;
    if (!validTime(r.rows[i].t)) return false;
  }
  return true;
}

// write-on-change：僅 rows/quoteDate/lastCloseDate 參與比較
// （quoteTime/savedEpoch 單獨變更不觸發寫入——spec 修訂四版）
// 逐欄位比較——QuoteRow 含 padding，禁止以 memcmp 做語意比較
// （兩側 doubles 皆由同一來源字串解析，== 比較成立）
// putBytes 二進位儲存保留；layout 變動時 BLOB_VERSION 必須遞增
inline bool recordDiffers(const QuoteRecord& a, const QuoteRecord& b) {
  for (int i = 0; i < 5; i++) {
    if (strcmp(a.rows[i].code, b.rows[i].code) != 0) return true;
    if (a.rows[i].z != b.rows[i].z) return true;
    if (a.rows[i].y != b.rows[i].y) return true;
    if (strcmp(a.rows[i].t, b.rows[i].t) != 0) return true;
  }
  if (strncmp(a.quoteDate, b.quoteDate, 9) != 0) return true;
  if (strncmp(a.lastCloseDate, b.lastCloseDate, 9) != 0) return true;
  return false;
}
```

（`QuoteRecord`/`BLOB_VERSION` 放在排程段**之前**——直接把上述整段插在 namespace 內 `formatDateTW` 之後即可，無先後依賴問題。）

- [ ] **Step 4: 執行測試確認通過**

Run: `g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic && /tmp/opencode/test_quote_logic`
Expected: `ALL PASS`（含新群組 `schedule ok`／`blob ok`）

- [ ] **Step 5: Commit**

```bash
git add src/quote_logic.h tests/host/test_quote_logic.cpp
git commit -m "報價看板：排程計算與 NVS blob 邏輯

市場四狀態判定、5 分邊界對齊（最小 30s）、todayAt9/nextDayAt9/
nextWeekdayAt9、24h cap；QuoteRecord 版本 blob（quoteDate/
lastCloseDate/savedEpoch）、sane 驗證與 write-on-change
（時間單獨變更不寫）。host 測試全過。

驗證等級：無硬體（host 單元測試）。"
```

---

### Task 5: `src/quote_store.h/.cpp` ＋ 抓取驗證 harness

**Files:**
- Create: `src/quote_store.h`, `src/quote_store.cpp`, `src/twse_root_ca.h`
- Modify: `src/main.cpp`（抓取驗證 harness，Task 8 起替換）
- 本機建立: `src/secrets.h`（使用者填 Wi-Fi；不入庫）

- [ ] **Step 1: 萃取 TWCA 根憑證 → `src/twse_root_ca.h`**

先驗證指紋（比對下方檔頭註解）：

```bash
echo | openssl s_client -connect mis.twse.com.tw:443 -servername mis.twse.com.tw -showcerts 2>/dev/null \
  | awk '/BEGIN CERT/{i++} /END CERT/{last=i} {buf[i]=buf[i]"\n"$0} END{print buf[last]}' \
  | openssl x509 -noout -fingerprint -sha256 -subject -dates
# Expected: Fingerprint=59:76:90:07:F7:68:5D:0F:CD:50:87:2F:9F:95:D5:75:5A:5B:2B:45:7D:81:F3:69:2B:61:0A:98:67:2F:0E:1B
#           CN = TWCA Global Root CA, notAfter Dec 31 15:59:59 2030 GMT
```

建立 `src/twse_root_ca.h`（完整內容如下）：

```cpp
#pragma once
// 釘選 TWCA Global Root CA（spec 修訂五版：TLS 主要信任錨，原 bundle 方案降為備註）
// SHA256 Fingerprint=59:76:90:07:F7:68:5D:0F:CD:50:87:2F:9F:95:D5:75:5A:5B:2B:45:7D:81:F3:69:2B:61:0A:98:67:2F:0E:1B
// 效期至 2030-12-31——到期前必須更新本檔（驗證矩陣 TLS 項）。鏈：葉(mis.twse.com.tw)→TWCA SSL Sub-CA→本根。
#define TWSE_ROOT_CA_PEM \
  "-----BEGIN CERTIFICATE-----\n" \
  "MIIFQTCCAymgAwIBAgICDL4wDQYJKoZIhvcNAQELBQAwUTELMAkGA1UEBhMCVFcx\n" \
  "EjAQBgNVBAoTCVRBSVdBTi1DQTEQMA4GA1UECxMHUm9vdCBDQTEcMBoGA1UEAxMT\n" \
  "VFdDQSBHbG9iYWwgUm9vdCBDQTAeFw0xMjA2MjcwNjI4MzNaFw0zMDEyMzExNTU5\n" \
  "NTlaMFExCzAJBgNVBAYTAlRXMRIwEAYDVQQKEwlUQUlXQU4tQ0ExEDAOBgNVBAsT\n" \
  "B1Jvb3QgQ0ExHDAaBgNVBAMTE1RXQ0EgR2xvYmFsIFJvb3QgQ0EwggIiMA0GCSqG\n" \
  "SIb3DQEBAQUAA4ICDwAwggIKAoICAQCwBdvI64zEbooh745NnHEKH1Jw7W2CnJfF\n" \
  "10xORUnLQEK1EjRsGcJ0pDFfhQKX7EMzClPSnIyOt7h52yvVavKOZsTuKwEHktSz\n" \
  "0ALfUPZVr2YOy+BHYC8rMjk1Ujoog/h7FsYYuGLWRyWRzvAZEk2tY/XTP3VfKfCh\n" \
  "MBwqoJimFb3u/Rk28OKRQ4/6ytYQJ0lM793B8YVwm8rqqFpD/G2Gb3PpN0Wp8DbH\n" \
  "zIh1HrtsBv+baz4X7GGqcXzGHaL3SekVtTzWoWH1EfcFbx39Eb7QMAfCKbAJTibc\n" \
  "46KokWofwpFFiFzlmLhxpRUZyXx1EcxwdE8tmx2RRP1WKKD+u4ZqyPpcC1jcxkt2\n" \
  "yKsi2XMPpfRaAok/T54igu6idFMqPVMnaR1sjjIsZAAmY2E2TqNGtz99sy2sbZCi\n" \
  "laLOz9qC5wc0GZbpuCGqKX6mOL6OKUohZnkfs8O1CWfe1tQHRvMq2uYiN2DLgbYP\n" \
  "oA/pyJV/v1WRBXrPPRXAb94JlAGD1zQbzECl8LibZ9WYkTunhHiVJqRaCPgrdLQA\n" \
  "BDzfuBSO6N+pjWxnkjMdwLfS7JLIvgm/LCkFbwJrnu+8vyq8W8BQj0FwcYeyTbcE\n" \
  "qYSjMq+u7msXi7Kx/mzhkIyIqJdIzshNy/MGz19qCkKxHh53L46g5pIOBvwFItIm\n" \
  "4TFRfTLcDwIDAQABoyMwITAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0TAQH/BAUwAwEB\n" \
  "/zANBgkqhkiG9w0BAQsFAAOCAgEAXzSBdu+WHdXltdkCY4QWwa6gcFGn90xHNcgL\n" \
  "1yg9iXHZqjNB6hQbbCEAwGxCGX6faVsgQt+i0trEfJdLjbDorMjupWkEmQqSpqsn\n" \
  "LhpNgb+E1HAerUf+/UqdM+DyucRFCCEK2mlpc3INvjT+lIutwx4116KD7+U4x6WF\n" \
  "H6vPNOw/KP4M8VeGTslV9xzU2KV9Bnpv1d8Q34FOIWWxtuEXeZVFBs5fzNxGiWNo\n" \
  "RI2T9GRwoD2dKAXDOXC4Ynsg/eTb6QihuJ49CcdP+yz4k3ZB3lLg4VfSnQO8d57+\n" \
  "nile98FRYB/e2guyLXW3Q0iT5/Z5xoRdgFlglPx4mI88k1HtQJAH32RjJMtOcQWh\n" \
  "15QaiDLxInQirqWm2BJpTGCjAu4r7NRjkgtevi92a6O2JryPA9gK8kxkRr05YuWW\n" \
  "6zRjESjMlfGt7+/cgFhI6Uu46mWs6fyAtbXIRfmswZ/ZuepiiI7E8UuDEq3mi4TW\n" \
  "nsLrgxifarsbJGAzcMzs9zLzXNl5fe+epP7JI8Mk7hWSsT2RTyaGvWZzJBPqpK5j\n" \
  "wa19hAM8EHiGG3njxPPyBJUgriOCxLM6AGK/5jYk4Ve6xx6QddVfP5VhK8E7zeWz\n" \
  "aGHQRiapIVJpLesux+t3zqY6tQMzT3bR51xUAV3LePTJDL/PEo4XLSNolOer/qmy\n" \
  "KwbQBM0=\n" \
  "-----END CERTIFICATE-----\n"
```

- [ ] **Step 2: 建立 `src/secrets.h`**

```bash
cp src/secrets.h.example src/secrets.h   # 使用者編輯填入 Wi-Fi
```

- [ ] **Step 3: 實作 `src/quote_store.h`**

```cpp
#pragma once
#include <cstdint>
#include "quote_logic.h"

// Wi-Fi 連線（spec：timeout 內未連上回 false）
bool quoteWifiBegin(uint32_t timeoutMs);
// NTP 對時（configTime + getLocalTime）；成功後 time(nullptr) 可用
bool quoteNtpSync(uint32_t timeoutMs);
// HTTPS 抓取＋解析＋欄位驗證（all-or-nothing）
// 回 0=成功（out 填入）；<0 transport 區間（不與 V_* 重疊）：
//   -10 begin 失敗、-11 非 200、-12 body 空/超 32KB 上限
//   或 quote_logic 之 V_STRUCT(-1)/V_NUMERIC(-2)/V_FORMAT(-3)/V_DATE_DIFF(-4)/V_JSON(-5)
int quoteFetch(qlogic::MarketBatch* out);
// NVS blob（單一 key quote:rec；putBytes）
bool quoteRecordLoad(qlogic::QuoteRecord* rec);   // false=無有效快取
bool quoteRecordSave(qlogic::QuoteRecord* rec, uint32_t nowUtc);
```

- [ ] **Step 4: 實作 `src/quote_store.cpp`**

```cpp
#include "quote_store.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "log.h"
#include "watchlist.h"
#include "secrets.h"
#include "twse_root_ca.h"

static Preferences g_prefs;

bool quoteWifiBegin(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(100);
    yield();
  }
  LOGF("wifi %s (%lu ms)\n", WiFi.status() == WL_CONNECTED ? "connected" : "fail",
       (unsigned long)(millis() - t0));
  return WiFi.status() == WL_CONNECTED;
}

bool quoteNtpSync(uint32_t timeoutMs) {
  configTime(qlogic::TZ_TW, 0, "pool.ntp.org", "time.nist.gov");
  struct tm tmInfo;
  bool ok = getLocalTime(&tmInfo, timeoutMs);
  LOGF("ntp %s\n", ok ? "synced" : "fail");
  return ok;
}

int quoteFetch(qlogic::MarketBatch* out) {
  WiFiClientSecure client;
  client.setCACert(TWSE_ROOT_CA_PEM);      // 釘選根憑證（禁 setInsecure）
  client.setConnectTimeout(10000);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  String url = String("https://mis.twse.com.tw/stock/api/getStockInfo.jsp?ex_ch=") +
               QUOTE_EX_CH + "&json=1";
  if (!http.begin(client, url)) return -10;
  http.useHTTP10(true);                    // HTTP/1.0：無 chunked，原始位元組流
  http.addHeader("User-Agent", "esp32-eink-quote/1.0");
  int code = http.GET();
  if (code != 200) {
    LOGF("[fail] http %d\n", code);
    http.end();
    return -11;
  }
  int len = http.getSize();
  if (len > 32768) {
    LOGF("[fail] content-length %d over cap\n", len);
    http.end();
    return -12;
  }
  // 有界讀取（spec P0：上限必須在資料進入 String/JsonDocument 前生效）。
  // 上限語意＝payload ≤ 32768 bytes（含）：以第 32769 byte 作 probe——
  // 讀滿 32769 即確定超限→失敗；恰好 32768 合法。len==-1（無 Content-Length）亦受保護。
  static char body[32770];                 // 32769 probe 緩衝 + NUL（BSS，不佔 stack）
  int total = 0;
  WiFiClient* stream = http.getStreamPtr();
  uint32_t t0 = millis();
  while ((http.connected() || stream->available()) && total < 32769 &&
         millis() - t0 < 15000) {
    int avail = stream->available();
    if (avail > 0) {
      int want = (32769 - total) < avail ? (32769 - total) : avail;
      int n = stream->read((uint8_t*)(body + total), (size_t)want);
      if (n <= 0) break;
      total += n;
    } else {
      delay(2);
      yield();
    }
  }
  http.end();
  if (total <= 0) {
    LOGF("[fail] empty body\n");
    return -12;
  }
  if (total > 32768) {
    LOGF("[fail] body over 32KB cap\n");
    return -12;
  }
  body[total] = '\0';

  qlogic::RawBatch raw;
  int pr = qlogic::parseJsonToRaw(body, total, &raw);
  if (pr != qlogic::V_OK) {
    LOGF("[fail] parse/collect %d\n", pr);
    return pr;
  }
  int vr = qlogic::validateBatch(raw, out);
  if (vr != qlogic::V_OK) {
    LOGF("[fail] validate %d\n", vr);
    return vr;
  }
  return 0;
}

bool quoteRecordLoad(qlogic::QuoteRecord* rec) {
  g_prefs.begin("quote", true);
  size_t got = g_prefs.getBytes("rec", rec, sizeof(qlogic::QuoteRecord));
  g_prefs.end();
  if (got != sizeof(qlogic::QuoteRecord)) return false;
  if (!qlogic::recordSane(rec)) return false;
  return true;
}

bool quoteRecordSave(qlogic::QuoteRecord* rec, uint32_t nowUtc) {
  rec->savedEpoch = nowUtc;
  g_prefs.begin("quote", false);
  size_t put = g_prefs.putBytes("rec", rec, sizeof(qlogic::QuoteRecord));
  g_prefs.end();
  return put == sizeof(qlogic::QuoteRecord);
}
```

- [ ] **Step 5: `src/main.cpp` 改為抓取驗證 harness**

```cpp
#include <Arduino.h>
#include <time.h>
#include "log.h"
#include "ui.h"
#include "quote_store.h"
#include "watchlist.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("quote fetch harness\n");
  uiInit();
  if (!quoteWifiBegin(15000)) {
    LOGF("wifi fail, halt\n");
    return;
  }
  if (!quoteNtpSync(10000)) {
    LOGF("ntp fail\n");
  }
  qlogic::MarketBatch mb;
  int r = quoteFetch(&mb);
  LOGF("fetch=%d\n", r);
  if (r == 0) {
    for (int i = 0; i < WATCH_N; i++) {
      qlogic::QuoteCalc c = qlogic::calcQuote(mb.rows[i].z, mb.rows[i].y);
      LOGF("  %s z=%.2f y=%.2f chg=%+.2f pct=%+.2f%% t=%s\n",
           mb.rows[i].code, mb.rows[i].z, mb.rows[i].y, c.chg, c.pct, mb.rows[i].t);
    }
    LOGF("date=%s quoteTime=%s\n", mb.date, mb.quoteTime);
    qlogic::QuoteRecord rec = {};
    rec.version = qlogic::BLOB_VERSION;
    for (int i = 0; i < WATCH_N; i++) rec.rows[i] = mb.rows[i];
    strcpy(rec.quoteDate, mb.date);
    strcpy(rec.quoteTime, mb.quoteTime);
    LOGF("save=%d\n", quoteRecordSave(&rec, (uint32_t)time(nullptr)));
    qlogic::QuoteRecord back;
    LOGF("load=%d\n", quoteRecordLoad(&back));
  }
}

void loop() { delay(1000); }
```

- [ ] **Step 6: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 7: 硬體檢查點（矩陣 #7、#8 部分、#9、#12 數值來源）**

請使用者上傳後回報 serial：

```bash
/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor -b 115200
```

檢查：`wifi connected`、`ntp synced`、`fetch=0`、五列數值與證交所網頁一致
（盤中為即時值；假日/夜間為最後交易日值）、`save=1`、`load=1`。

- [ ] **Step 8: Commit**

```bash
git add src/quote_store.h src/quote_store.cpp src/twse_root_ca.h src/main.cpp
git commit -m "報價看板：quote_store（Wi-Fi/NTP/HTTPS/JSON/NVS）

釘選 TWCA Global Root CA（fingerprint 59:76:...:0E:1B，2030-12-31
到期）；HTTP 契約（timeout 10s/15s、UA、32KB 上限、非 200 失敗）；
ArduinoJson v7 解析＋quote_logic all-or-nothing 驗證；NVS 單一
versioned blob（quote:rec putBytes）。

驗證等級：有硬體（fetch/save/load 實測，數值與證交所一致）。"
```

---

### Task 6: `src/ui.h/.cpp` 直式版面

**Files:**
- Modify: `src/ui.h`（QuoteView 已於 Task 1 定型，不變）
- Modify: `src/ui.cpp`（實作 `uiShowQuotes`／`uiShowMessage`）

- [ ] **Step 1: 實作版面（取代 Task 1 的 stub 函式）**

在 `src/ui.cpp` 加入 include 與兩個實作：

```cpp
#include "fonts_quote.h"

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
  // 全部圖元經已 setRotation(1) 之 display（邏輯 272x792）——spec 修訂三版
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    // header
    u8g2.setFont(u8g2_font_quote16);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(16, 26);
    u8g2.print(v.dateStr);
    int tw = u8g2.getUTF8Width(v.timeStr);
    u8g2.setCursor(272 - 16 - tw, 26);
    u8g2.print(v.timeStr);
    display.drawLine(16, 40, 256, 40, GxEPD_BLACK);
    // 5 列（row0=加權指數：名稱 28px）
    for (int i = 0; i < 5; i++) {
      int y0 = 56 + i * 138;
      u8g2.setFont(i == 0 ? u8g2_font_quote28 : u8g2_font_quote20);
      u8g2.setCursor(16, y0 + (i == 0 ? 26 : 20));
      u8g2.print(v.names[i]);
      char buf[24];
      qlogic::formatPrice(v.z[i], buf, sizeof buf);
      u8g2.setFont(u8g2_font_logisoso38_tr);
      u8g2.setCursor(16, y0 + 72);
      u8g2.print(buf);
      u8g2.setFont(u8g2_font_logisoso22_tr);
      if (v.chg[i] > 0.0001)      drawArrowUp(16, y0 + 112);
      else if (v.chg[i] < -0.0001) drawArrowDown(16, y0 + 112);
      else                         drawFlatDash(16, y0 + 112);
      snprintf(buf, sizeof buf, "%+.2f  %+.2f%%", v.chg[i], v.pct[i]);
      u8g2.setCursor(42, y0 + 112);
      u8g2.print(buf);
      if (i < 4) display.drawLine(16, y0 + 130, 256, y0 + 130, GxEPD_BLACK);
    }
    if (v.status) {
      u8g2.setFont(u8g2_font_quote16);
      u8g2.setCursor(16, 780);
      u8g2.print(v.status);
    }
  } while (display.nextPage());
}

void uiShowMessage(const char* l1, const char* l2) {
  if (!initialized) return;
  // 英文訊息用 helv 字型（相框已驗證含完整 ASCII；logisoso 字集不保證全字母）
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setFont(u8g2_font_helvB24_tf);
    u8g2.setCursor(16, 120);
    u8g2.print(l1);
    u8g2.setFont(u8g2_font_helvR14_tf);
    u8g2.setCursor(16, 160);
    u8g2.print(l2);
  } while (display.nextPage());
}
```

並在檔案頂部補 `#include "quote_logic.h"`（formatPrice）。

- [ ] **Step 2: harness 補渲染呼叫（`src/main.cpp` 的 `fetch==0` 分支後）**

```cpp
  QuoteView v = {};
  for (int i = 0; i < WATCH_N; i++) {
    v.names[i] = WATCHLIST[i].name;
    qlogic::QuoteCalc c = qlogic::calcQuote(mb.rows[i].z, mb.rows[i].y);
    v.z[i] = mb.rows[i].z;
    v.chg[i] = c.chg;
    v.pct[i] = c.pct;
  }
  qlogic::formatDateTW(mb.date, v.dateStr, sizeof v.dateStr);
  strcpy(v.timeStr, "13:33");   // harness 固定值；正式路徑於 Task 8
  uiShowQuotes(v);
```

- [ ] **Step 3: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS（若 logisoso38/22 字型名錯誤，以 `rtk rg 'u8g2_font_logisoso' ~/.platformio/.../U8g2` 核對實際名稱並修正——優先 `logisoso38_tr`／`logisoso22_tr`）

- [ ] **Step 4: 硬體檢查點（矩陣 #10、#11、#13、#4 實體對應）**

請使用者上傳回報：直立安裝方向（header 在頂、文字正立；若顛倒改 `setRotation(3)`）、
中文名稱無缺字、▲方向與正負一致、日期/時間位置正確、底線完整。

- [ ] **Step 5: Commit**

```bash
git add src/ui.cpp src/ui.h src/main.cpp
git commit -m "報價看板：直式版面渲染

display.setRotation(1) 統一旋轉（僅此一處；圖元全經 GFX 邏輯
座標）；A3 版面（header 16px/名稱 28+20px/價格 logisoso38/
漲跌 logisoso22+手繪▲▼）；狀態字於底部。

驗證等級：有硬體（直立方向/缺字/符號實測）。"
```

---

### Task 7: `src/main.cpp` 完整狀態機

**Files:**
- Modify: `src/main.cpp`（整檔替換）

- [ ] **Step 1: 實作完整狀態機（整檔）**

```cpp
#include <Arduino.h>
#include <esp_sleep.h>
#include <time.h>
#include "driver/rtc_io.h"
#include "log.h"
#include "quote_logic.h"
#include "quote_store.h"
#include "ui.h"
#include "watchlist.h"

// 按鍵（active-low，docs/device-research.md）；v1 僅 MENU 有功能
#define BTN_MENU 2
#define BTN_UP   6
#define BTN_DOWN 4
#define BTN_PRESS 5

#define WAIT_RELEASE_MS 2000

static esp_sleep_wakeup_cause_t g_wake;
static uint32_t g_wakeMask = 0;
static bool stuckGuard = false;

// ---------- NVS ----------（快取載入於 quote_store；此處僅讀取輔助）
static bool loadCache(qlogic::QuoteRecord* rec) {
  return quoteRecordLoad(rec);
}

// ---------- 視圖組裝 ----------
static void viewFromRecord(QuoteView* v, const qlogic::QuoteRecord& rec,
                           const char* timeStr, const char* status) {
  for (int i = 0; i < WATCH_N; i++) {
    v->names[i] = WATCHLIST[i].name;
    v->z[i] = rec.rows[i].z;
    qlogic::QuoteCalc c = qlogic::calcQuote(rec.rows[i].z, rec.rows[i].y);
    v->chg[i] = c.chg;
    v->pct[i] = c.pct;
  }
  qlogic::formatDateTW(rec.quoteDate, v->dateStr, sizeof v->dateStr);
  strncpy(v->timeStr, timeStr, sizeof v->timeStr - 1);
  v->timeStr[sizeof v->timeStr - 1] = '\0';
  v->status = status;
}

static void viewFromBatch(QuoteView* v, const qlogic::MarketBatch& mb,
                          const char* timeStr, const char* status) {
  qlogic::QuoteRecord rec = {};
  rec.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < WATCH_N; i++) rec.rows[i] = mb.rows[i];
  strcpy(rec.quoteDate, mb.date);
  strcpy(rec.quoteTime, mb.quoteTime);
  viewFromRecord(v, rec, timeStr, status);
}

static void localHHMM(uint32_t utc, char* buf, int cap) {
  qlogic::Civil c = qlogic::civilFromEpoch(utc, qlogic::TZ_TW);
  snprintf(buf, cap, "%02d:%02d", c.hh, c.mm2);
}

// "HH:MM:SS" → "HH:MM"（header 時間用）
static void hhmm(const char* t8, char* out) {
  memcpy(out, t8, 5);
  out[5] = '\0';
}

// ---------- 抓取＋持久化 ----------
struct FetchResult {
  bool ok;         // transport/JSON/驗證成功
  bool isToday;    // d == 今日（僅 ok 時有意義）
  qlogic::MarketBatch mb;
};

static FetchResult fetchUpdate(uint32_t nowUtc, const char* todayStr) {
  FetchResult fr;
  fr.ok = false;
  fr.isToday = false;
  int r = quoteFetch(&fr.mb);
  if (r != 0) return fr;
  fr.ok = true;
  fr.isToday = (strcmp(fr.mb.date, todayStr) == 0);
  // write-on-change：先載入舊 record、補回可保留旗標，再單次比較
  // （quoteTime/savedEpoch 不參與比較——spec 修訂四版）
  qlogic::QuoteRecord old;
  bool have = quoteRecordLoad(&old);
  qlogic::QuoteRecord rec = {};
  rec.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < WATCH_N; i++) rec.rows[i] = fr.mb.rows[i];
  strcpy(rec.quoteDate, fr.mb.date);
  strcpy(rec.quoteTime, fr.mb.quoteTime);
  if (have && strcmp(old.lastCloseDate, rec.quoteDate) == 0) {
    strcpy(rec.lastCloseDate, old.lastCloseDate);   // 定格日與本次交易日相同才保留
  }
  if (!have || qlogic::recordDiffers(old, rec)) {
    quoteRecordSave(&rec, nowUtc);
    LOGF("nvs save\n");
  }
  return fr;
}

// 收盤定格寫入（PostClose 與 MENU 於收盤後成功共用）——write-on-change：
// 候選 record（含新定格旗標）與現存快取真正不同才寫入；savedEpoch
// 語意保持「最後持久化時間」
static void finalizeClose(const qlogic::MarketBatch& mb, const char* today,
                          uint32_t nowUtc) {
  qlogic::QuoteRecord old;
  bool have = quoteRecordLoad(&old);
  qlogic::QuoteRecord rec = {};
  rec.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < WATCH_N; i++) rec.rows[i] = mb.rows[i];
  strcpy(rec.quoteDate, mb.date);
  strcpy(rec.quoteTime, mb.quoteTime);
  strcpy(rec.lastCloseDate, today);
  if (!have || qlogic::recordDiffers(old, rec)) {
    quoteRecordSave(&rec, nowUtc);
    LOGF("nvs save (close)\n");
  }
}

// ---------- 睡眠 ----------
static void goToDeepSleep(uint32_t targetUtc, bool enableExt1) {
  uint32_t now = (uint32_t)time(nullptr);
  uint32_t delta;
  if (stuckGuard) {
    // 卡鍵（spec R4）：一律 5 分鐘 timer-only——停用 EXT1 且覆寫狀態路徑
    // 目標，避免 Weekend/PostClose 長睡讓板子長時間不可達
    delta = 300;
    enableExt1 = false;
    LOGF("[warn] stuck: 5 min timer-only retry\n");
  } else {
    delta = qlogic::capSleep(now, targetUtc);
  }
  uint64_t us = (uint64_t)delta * 1000000ULL;
  uiHibernate();
  uiSleepHoldPins();
  Serial.flush();
  delay(500);
  // s_config 開機歸零；零遮罩覆寫清除 RTC EXT1（相框已驗證做法）
  esp_sleep_enable_ext1_wakeup(0, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup(us);
  if (enableExt1) {
    esp_sleep_enable_ext1_wakeup((1ULL << BTN_MENU), ESP_EXT1_WAKEUP_ANY_LOW);
    rtc_gpio_init((gpio_num_t)BTN_MENU);
    rtc_gpio_set_direction((gpio_num_t)BTN_MENU, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)BTN_MENU);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_MENU);
  }
  LOGF("sleep %lus ext1=%d\n", (unsigned long)delta, enableExt1 ? 1 : 0);
  esp_deep_sleep_start();
}

// 依市場狀態取得長睡目標（PostClose/Weekend → 次平日 09:00；PreMarket → 當日 09:00）
static uint32_t longSleepTarget(qlogic::MarketState st, uint32_t now) {
  switch (st) {
    case qlogic::MarketState::PreMarket: return qlogic::todayAt9(now);
    case qlogic::MarketState::Trading:   return qlogic::nextTradingBoundary(now);
    default:                             return qlogic::nextWeekdayAt9(now);
  }
}

// ---------- 主流程 ----------
static bool waitButtonsReleased(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (digitalRead(BTN_MENU) && digitalRead(BTN_UP) &&
        digitalRead(BTN_DOWN) && digitalRead(BTN_PRESS)) {
      return true;
    }
    delay(10);
    yield();
  }
  LOGF("[warn] buttons still held after %lu ms\n", (unsigned long)timeoutMs);
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  g_wake = esp_sleep_get_wakeup_cause();
  g_wakeMask = (g_wake == ESP_SLEEP_WAKEUP_EXT1)
                   ? (uint32_t)esp_sleep_get_ext1_wakeup_status()
                   : 0;
  bool menuWake = (g_wakeMask & (1ULL << BTN_MENU)) != 0;
  LOGF("boot wake=%d mask=%u\n", (int)g_wake, g_wakeMask);

  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_PRESS, INPUT_PULLUP);

  // 卡鍵防護（R4）：等待釋放；stuck → 該輪 timer-only 5 分
  bool released = waitButtonsReleased(WAIT_RELEASE_MS);
  stuckGuard = !released;
  if (stuckGuard) {
    LOGF("[warn] stuck button\n");
    menuWake = false;
  }

  uiInit();

  // Wi-Fi／NTP 失敗路徑（spec 修訂三版）
  if (!quoteWifiBegin(15000)) {
    qlogic::QuoteRecord cache;
    if (loadCache(&cache)) {
      char ts[8];
      localHHMM(cache.savedEpoch, ts, sizeof ts);
      QuoteView v;
      viewFromRecord(&v, cache, ts, "更新失敗");
      uiShowQuotes(v);
    } else {
      uiShowMessage("NO WIFI", "check network");
    }
    goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
    return;
  }
  if (!quoteNtpSync(10000)) {
    qlogic::QuoteRecord cache;
    if (loadCache(&cache)) {
      char ts[8];
      localHHMM(cache.savedEpoch, ts, sizeof ts);
      QuoteView v;
      viewFromRecord(&v, cache, ts, "時間未同步");
      uiShowQuotes(v);
    } else {
      uiShowMessage("TIME NOT SYNC", "retry in 5 min");
    }
    goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
    return;
  }

  uint32_t now = (uint32_t)time(nullptr);
  char today[16];
  qlogic::dateOfEpoch(now, qlogic::TZ_TW, today, sizeof today);
  qlogic::MarketState st = qlogic::marketState(now);
  LOGF("state=%d today=%s\n", (int)st, today);

  // MENU 立即更新（任何狀態；spec 修訂六版：休市日/定格結果與一般路徑同規則）
  if (menuWake) {
    FetchResult fr = fetchUpdate(now, today);
    QuoteView v;
    if (fr.ok) {
      char qt[8];
      hhmm(fr.mb.quoteTime, qt);
      viewFromBatch(&v, fr.mb, qt, nullptr);
      uiShowQuotes(v);
      if (!fr.isToday &&
          (st == qlogic::MarketState::Trading || st == qlogic::MarketState::PostClose)) {
        // 休市日手動更新 → 與一般路徑同規則：睡至隔日 09:00（避免短週期）
        goToDeepSleep(qlogic::nextDayAt9((uint32_t)time(nullptr)), !stuckGuard);
        return;
      }
      if (st == qlogic::MarketState::PostClose && fr.isToday) {
        // 順帶完成收盤定格，避免次輪重抓
        finalizeClose(fr.mb, today, (uint32_t)time(nullptr));
      }
      goToDeepSleep(longSleepTarget(st, (uint32_t)time(nullptr)), !stuckGuard);
      return;
    }
    qlogic::QuoteRecord cache;
    if (loadCache(&cache)) {
      char ts[8];
      localHHMM(cache.savedEpoch, ts, sizeof ts);
      viewFromRecord(&v, cache, ts, "更新失敗");
      uiShowQuotes(v);
    } else {
      uiShowMessage("FETCH FAIL", "retry in 5 min");
    }
    if (st == qlogic::MarketState::PreMarket || st == qlogic::MarketState::Weekend) {
      goToDeepSleep(longSleepTarget(st, (uint32_t)time(nullptr)), !stuckGuard);
    } else {
      goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
    }
    return;
  }

  switch (st) {
    case qlogic::MarketState::PreMarket: {
      goToDeepSleep(qlogic::todayAt9(now), !stuckGuard);
      return;
    }
    case qlogic::MarketState::Trading: {
      FetchResult fr = fetchUpdate(now, today);
      QuoteView v;
      if (fr.ok) {
        if (!fr.isToday) {
          // 假日：渲染本次回應（舊交易日資料），睡到隔日 09:00
          char qt[8];
          hhmm(fr.mb.quoteTime, qt);
          viewFromBatch(&v, fr.mb, qt, nullptr);
          uiShowQuotes(v);
          goToDeepSleep(qlogic::nextDayAt9((uint32_t)time(nullptr)), !stuckGuard);
          return;
        }
        char qt[8];
        hhmm(fr.mb.quoteTime, qt);
        viewFromBatch(&v, fr.mb, qt, nullptr);
        uiShowQuotes(v);
        goToDeepSleep(qlogic::nextTradingBoundary((uint32_t)time(nullptr)), !stuckGuard);
        return;
      }
      // 失敗：快取＋更新失敗 → 5 分重試
      qlogic::QuoteRecord cache;
      if (loadCache(&cache)) {
        char ts[8];
        localHHMM(cache.savedEpoch, ts, sizeof ts);
        viewFromRecord(&v, cache, ts, "更新失敗");
        uiShowQuotes(v);
      } else {
        uiShowMessage("FETCH FAIL", "retry in 5 min");
      }
      goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
      return;
    }
    case qlogic::MarketState::PostClose: {
      qlogic::QuoteRecord cache;
      bool have = loadCache(&cache);
      if (have && strcmp(cache.lastCloseDate, today) == 0) {
        // 已定格
        goToDeepSleep(qlogic::nextWeekdayAt9(now), !stuckGuard);
        return;
      }
      FetchResult fr = fetchUpdate(now, today);
      QuoteView v;
      if (fr.ok && fr.isToday) {
        // 收盤定格：寫 lastCloseDate（共用 helper）
        finalizeClose(fr.mb, today, (uint32_t)time(nullptr));
        char ts[8];
        localHHMM((uint32_t)time(nullptr), ts, sizeof ts);
        viewFromBatch(&v, fr.mb, ts, nullptr);
        uiShowQuotes(v);
        goToDeepSleep(qlogic::nextWeekdayAt9((uint32_t)time(nullptr)), !stuckGuard);
        return;
      }
      if (fr.ok && !fr.isToday) {
        // 休市日（d != today）：顯示後睡至隔日 09:00（不得 5 分重試迴圈）
        if (have) {
          char ts[8];
          localHHMM(cache.savedEpoch, ts, sizeof ts);
          viewFromRecord(&v, cache, ts, nullptr);
          uiShowQuotes(v);
        } else {
          char qt[8];
          hhmm(fr.mb.quoteTime, qt);
          viewFromBatch(&v, fr.mb, qt, nullptr);
          uiShowQuotes(v);
        }
        goToDeepSleep(qlogic::nextDayAt9(now), !stuckGuard);
        return;
      }
      // 失敗 → 5 分重試
      if (have) {
        char ts[8];
        localHHMM(cache.savedEpoch, ts, sizeof ts);
        viewFromRecord(&v, cache, ts, "更新失敗");
        uiShowQuotes(v);
      } else {
        uiShowMessage("FETCH FAIL", "retry in 5 min");
      }
      goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
      return;
    }
    case qlogic::MarketState::Weekend: {
      goToDeepSleep(qlogic::nextWeekdayAt9(now), !stuckGuard);
      return;
    }
  }
}

void loop() { delay(100); }
```

- [ ] **Step 2: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 3: 硬體檢查點（矩陣 #14–#20、#22，分次回報）**

請使用者依序上機（改系統時間/牆鐘模擬非交易時段）：
1. 盤中：5 分邊界自動更新 2 個週期（serial 檢查 `sleep 300s` 前後 timestamp 對齊 :05/:00）
2. MENU 立即更新（盤中與盤後各一次）
3. 卡鍵：按住 MENU 開機 → stuck → timer-only → 釋放後恢復
4. 收盤定格：13:30 後啟動 → `lastCloseDate` 寫入 → 再醒不重抓（`sleep 300s` 應為長睡）
5. 休市日模擬（牆鐘調至假日 14:00）：成功但 d≠today → 長睡無重試迴圈
6. NTP 失敗模擬（擋 NTP）：快取＋「時間未同步」＋5 分短睡
7. 假日 TRADING 模擬（假日牆鐘調至 10:00）：d≠today → 睡至隔日 09:00

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "報價看板：完整狀態機

四狀態（PRE_MARKET/TRADING/POST_CLOSE/WEEKEND）＋假日與休市日
分支、NTP 失敗路徑、MENU 例外路徑、5 分邊界排程、卡鍵 timer-only
防護、write-on-change NVS；睡眠雙源（stuck 時 timer-only）與
零遮罩 EXT1 清除沿用相框修補。

驗證等級：有硬體（狀態分支/排程/防護實測）。"
```

---

### Task 8: 收尾——矩陣全掃、量測記錄與文件

**Files:**
- Modify: `docs/device-research.md`（量測小節）
- Modify: `README.md`（狀態/目錄/操作）
- Verify: spec 驗證矩陣全數通過

- [ ] **Step 1: 矩陣全掃（含 #21 20+ 次連續 sleep/wake）**

逐項執行 spec「驗證矩陣」host＋硬體全部項目並記錄（含時間戳、狀態轉換 serial log）。

- [ ] **Step 2: `docs/device-research.md` 附加量測小節**

```markdown
## 報價看板量測（2026-08-29）

量測方法：serial log（quote board 狀態機），時間戳取自 millis()/NTP。

| 項目 | 結果 |
| --- | --- |
| Wi-Fi 連線 | （實測值）ms |
| NTP 對時 | （實測值）ms |
| HTTPS 抓取＋解析＋驗證 | （實測值）ms |
| full refresh（直式） | （實測值）ms |
| 喚醒→畫面完成（total） | （實測值）ms |
| 5 分邊界對齊誤差 | （實測值） |
| 20+ 次連續 sleep/wake | 通過/異常 |
| TLS handshake（釘選 CA） | 通過/失敗→備案 |

異常事件：
- （記錄實作中發現之異常與修法）
```

（量測值於實測後填入，禁止未測先值。）

- [ ] **Step 3: README 同步**

- 專案狀態表加「報價看板 v1（**現行韌體**）」、SD 相框列標籤 `photo-frame-v1`
- 目錄結構：`quote_store.*`、`quote_logic.h`、`watchlist.h`、`fonts_quote.*`、`tools/gen_fonts.py`、`src/secrets.h.example`
- 操作：Wi-Fi 憑證放置說明（`cp src/secrets.h.example src/secrets.h`）、按鍵行為（MENU 立即更新）
- 文件連結補報價看板 spec/plan

- [ ] **Step 4: 一致性檢查＋編譯**

```bash
rtk rg -n 'TBD|TODO|PLACEHOLDER|待補' src/ tools/ docs/superpowers/specs/2026-08-29-quote-board-design.md; echo "tbd-exit=$?"   # Expected: exit 1
/tmp/opencode/pio-venv/bin/pio run   # Expected: SUCCESS
```

- [ ] **Step 5: Commit**

```bash
git add docs/device-research.md README.md
git commit -m "報價看板收尾：量測記錄與文件同步

驗證等級：有硬體（矩陣全掃記錄）。"
```

- [ ] **Step 6: 最終 code review → 合併決策**

合併回 master（fast-forward）＋標籤 `quote-v1`（`photo-frame-v1` 已保存現行相框），push 由使用者確認後執行。
