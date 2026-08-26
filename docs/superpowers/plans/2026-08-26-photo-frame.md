# SD 相框／圖片播放器實作計畫

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `feature/photo-frame` 分支上建立 SD 相框韌體與 host 轉檔工具，瀏覽 `/raw_photos/*.raw`，支援手動翻頁、設定選單與自動輪播，完成後合併回 master 取代天氣看板（`weather-v1` 標籤保存）。

**Architecture:** 快照式分支（方案 2）。刪除天氣專屬檔案，保留 `log.h`；新 `photo_store` 負責 SD 掛載、掃描過濾、排序檔名清單與讀 RAW 入 static logical bitmap；`ui` 保留 U8g2 字型核心並改為相框版面（全幅圖＋選單＋提示畫面）；`main.cpp` 為狀態機（三鍵 EXT1 喚醒分流、卡鍵防護、輪播 timer、NVS）。Host 端 `tools/raw_convert.py`（Pillow）產出符合 12-byte 檔頭契約的 `.raw`。

**Tech Stack:** PlatformIO（沿用）、GxEPD2 1.6.9、U8g2_for_Adafruit_GFX 1.8.0（移除 ArduinoJson）、SD library、Pillow。

**規格來源:** `docs/superpowers/specs/2026-08-26-photo-frame-design.md`（修訂二版，已核准）

**執行注意事項:**
- pio 二進位：`/tmp/opencode/pio-venv/bin/pio`
- 使用者偏好逐步驗證：標「硬體檢查點」的步驟需停下請使用者操作與回報
- 本分支為快照式：天氣看板由 `weather-v1` 標籤保存，勿在本分支保留天氣程式
- 依賴變更：移除 ArduinoJson（天氣專屬）；不新增任何函式庫

---

### Task 1: 分支建立與工程重整

**Files:**
- Modify: `platformio.ini`（移除 ArduinoJson）
- Modify: `src/main.cpp`（換成相框骨架）
- Delete: `src/weather.h`、`src/weather.cpp`、`src/icons.h`、`src/icons.cpp`、`src/icons_bitmaps.h`、`src/locations.h`、`src/secrets.h.example`、`tools/gen_icons.py`

- [ ] **Step 1: 建立分支**

```bash
git checkout -b feature/photo-frame
```

- [ ] **Step 2: 刪除天氣專屬檔案**

```bash
git rm src/weather.h src/weather.cpp src/icons.h src/icons.cpp src/icons_bitmaps.h src/locations.h src/secrets.h.example tools/gen_icons.py
```

（`secrets.h` 本機檔案保留但不使用、不受影響；`weather-v1` 標籤有完整天氣版可考。）

- [ ] **Step 3: `platformio.ini` 移除 ArduinoJson**

`lib_deps` 區段改為：

```ini
lib_deps =
    zinggjm/GxEPD2@1.6.9
    olikraus/U8g2_for_Adafruit_GFX@1.8.0
```

- [ ] **Step 4: 建立相框骨架 `src/main.cpp`**

```cpp
#include <Arduino.h>
#include "log.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("photo frame skeleton\n");
}

void loop() {
  delay(100);
}
```

- [ ] **Step 5: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS（ArduinoJson 移除後 LDF 不再拉入）

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "SD 相框：分支建立與工程重整

移除天氣專屬檔案（weather/icons/locations/secrets 範本/圖示產生器）
與 ArduinoJson 依賴；保留 log.h 與既有 pinned 版本。weather-v1
標籤保存天氣看板。

驗證等級：無硬體（僅編譯）。"
```

---

### Task 2: Host 轉檔工具 `tools/raw_convert.py`

**Files:**
- Create: `tools/raw_convert.py`

- [ ] **Step 1: 建立完整工具**

```python
#!/usr/bin/env python3
"""RAW 轉檔工具：JPG/PNG -> EPFR .raw（792x272 1bpp）。

契約見 docs/superpowers/specs/2026-08-26-photo-frame-design.md：
檔頭 12B = magic EPFR + version(1) + flags(0) + w u16LE + h u16LE +
reserved(0)；payload 26928B，bit1=黑 bit0=白，99B/列 MSB first。

用法：
  raw_convert.py 圖片... --out DIR [--mode contain|cover] [--force]
  raw_convert.py --selftest --out DIR   # 產生 8 張測試樣本
"""
import argparse
import os
import struct
import sys

from PIL import Image

W, H = 792, 272
PAYLOAD = W * H // 8
TOTAL = 12 + PAYLOAD
MAGIC = b"EPFR"
VERSION = 1
FLAGS = 0
MODES = ("contain", "cover")
IN_EXT = (".jpg", ".jpeg", ".png")


def _normalize(img: Image.Image) -> Image.Image:
    img = ImageOps.exif_transpose(img)          # EXIF orientation
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
        img = Image.alpha_composite(bg, img)    # 透明合成白底
    return img.convert("L")


def _resize(img: Image.Image, mode: str) -> Image.Image:
    scale_w, scale_h = W / img.width, H / img.height
    if mode == "contain":
        scale = min(scale_w, scale_h)
        tw, th = round(img.width * scale), round(img.height * scale)
        img = img.resize((tw, th), Image.Resampling.LANCZOS)
        canvas = Image.new("L", (W, H), 0)      # 0 = 白（bit0）
        canvas.paste(img, ((W - tw) // 2, (H - th) // 2))
        return canvas
    scale = max(scale_w, scale_h)
    tw, th = round(img.width * scale), round(img.height * scale)
    img = img.resize((tw, th), Image.Resampling.LANCZOS)
    left, top = (tw - W) // 2, (th - H) // 2
    return img.crop((left, top, left + W, top + H))


def _dither(img: Image.Image) -> Image.Image:
    return img.convert("1", dither=Image.Dither.FLOYDSTEINBERG)


def _pack(img: Image.Image) -> bytes:
    """1bpp：暗(<128) 設 bit=1(黑)；列優先、MSB first。輸入已為 WxH。"""
    px = img.load()
    out = bytearray()
    for y in range(H):
        for xb in range(W // 8):
            b = 0
            for bit in range(8):
                x = xb * 8 + bit
                if px[x, y] < 128:
                    b |= 0x80 >> bit
            out.append(b)
    return bytes(out)


def convert(src: str, dst: str, mode: str, force: bool) -> None:
    if os.path.exists(dst) and not force:
        raise FileExistsError(f"輸出已存在（需 --force）：{dst}")
    img = Image.open(src)
    img = _normalize(img)
    img = _resize(img, mode)
    img = _dither(img)
    payload = _pack(img)
    assert len(payload) == PAYLOAD
    header = MAGIC + bytes([VERSION, FLAGS]) + struct.pack("<HH", W, H) \
        + struct.pack("<H", 0)
    with open(dst, "wb") as f:
        f.write(header)
        f.write(payload)
    print(f"wrote {dst} ({len(header) + len(payload)} bytes, mode={mode})")


def selftest(out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    cases = [
        ("white",  Image.new("L", (1200, 800), 255)),
        ("black",  Image.new("L", (1200, 800), 0)),
        ("grad",   _gradient(1200, 800)),
        ("grid",   _grid(1200, 800)),
        ("label",  _label(1200, 800)),
    ]
    for name, img in cases:
        for mode in MODES:
            p = os.path.join(out_dir, f"{name}_{mode}.png")
            img.save(p)
            convert(p, p[:-4] + ".raw", mode, force=True)
    print(f"selftest wrote {len(cases) * len(MODES)} 組（PNG＋RAW）至 {out_dir}")


def _gradient(w: int, h: int) -> Image.Image:
    import math
    img = Image.new("L", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = (x * 255) // w if x < w // 2 else 255 - ((x - w // 2) * 255) // w
    return img


def _grid(w: int, h: int) -> Image.Image:
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    for x in range(0, w, 80):
        d.line([(x, 0), (x, h)], fill=0, width=2)
    for y in range(0, h, 40):
        d.line([(0, y), (w, y)], fill=0, width=2)
    return img


def _label(w: int, h: int) -> Image.Image:
    from PIL import ImageFont
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 90)
    except Exception:
        font = ImageFont.load_default()
    d.text((40, 60), "EPFR TEST 792x272", fill=0, font=font)
    d.rectangle([10, 10, w - 10, h - 10], outline=0, width=4)
    return img


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("images", nargs="*", help="輸入圖片（批次不遞迴）")
    ap.add_argument("--out", required=True)
    ap.add_argument("--mode", choices=MODES, default="contain")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        selftest(args.out)
        return
    if not args.images:
        ap.error("需要輸入圖片或 --selftest")
    os.makedirs(args.out, exist_ok=True)
    for src in args.images:
        if not os.path.isfile(src):
            print(f"[skip] 非一般檔案：{src}", file=sys.stderr)
            continue
        if os.path.splitext(src)[1].lower() not in IN_EXT:
            print(f"[skip] 副檔名不接受：{src}", file=sys.stderr)
            continue
        convert(src, os.path.join(args.out, os.path.splitext(os.path.basename(src))[0] + ".raw"),
                args.mode, args.force)


if __name__ == "__main__":
    from PIL import ImageDraw, ImageOps
    main()
```

- [ ] **Step 2: 自我測試驗證輸出契約**

```bash
mkdir -p /tmp/opencode/photos && /tmp/opencode/pio-venv/bin/python tools/raw_convert.py --selftest --out /tmp/opencode/photos
```

Run: `/tmp/opencode/pio-venv/bin/python -c "
import struct,glob
for p in sorted(glob.glob('/tmp/opencode/photos/*.raw')):
    b=open(p,'rb').read()
    magic,ver,flg,w,h,rsv=struct.unpack('<4sBBHHH',b[:12])
    ok = magic==b'EPFR' and ver==1 and flg==0 and w==792 and h==272 and rsv==0 and len(b)==26940
    print(p.split('/')[-1], len(b), 'OK' if ok else 'FAIL', magic, ver, flg, w, h, rsv)
"`

Expected: 全部 10 個 RAW 皆 `26940 OK`。

- [ ] **Step 3: 反相檢查（純白/純黑）**

Run: `/tmp/opencode/pio-venv/bin/python -c "
b=open('/tmp/opencode/photos/white_contain.raw','rb').read()[12:]
assert all(x==0 for x in b), 'white 必須全 0'
b=open('/tmp/opencode/photos/black_contain.raw','rb').read()[12:]
assert all(x==0xFF for x in b), 'black 必須全 0xFF'
print('polarity OK: 1=黑 0=白')
"`

Expected: `polarity OK: 1=黑 0=白`

- [ ] **Step 4: EXIF／透明合成／覆寫碰撞驗證（host 端）**

```bash
/tmp/opencode/pio-venv/bin/python - <<'EOF'
from PIL import Image, ImageOps
# 1) EXIF 旋轉 JPEG：90° 標記，轉檔後應為橫向
img = Image.new("L", (400, 1600), 200)
d = ImageDraw.Draw(img); d.rectangle([0,0,399,1599], outline=0, width=8)
img.save("/tmp/opencode/photos/exif_src.jpg", exif=Image.Exif().get_ifd(0) if False else None)
EOF
```

若上述簡易 EXIF 產生不可行，改以 Pillow 明確方式：

```bash
/tmp/opencode/pio-venv/bin/python - <<'EOF'
from PIL import Image
img = Image.new("L", (400, 1600), 200)
ex = Image.Exif(); ex[274] = 6          # Orientation=Rotate 90 CW
img.save("/tmp/opencode/photos/exif_src.jpg", exif=ex)
EOF
/tmp/opencode/pio-venv/bin/python tools/raw_convert.py /tmp/opencode/photos/exif_src.jpg --out /tmp/opencode/photos --force
/tmp/opencode/pio-venv/bin/python - <<'EOF'
from PIL import Image
# 2) 透明 PNG：中央全透明，轉檔後該區應為白（0）
img = Image.new("RGBA", (1200, 800), (0, 0, 0, 255))
for y in range(200, 600):
    for x in range(300, 900):
        img.putpixel((x, y), (0, 0, 0, 0))
img.save("/tmp/opencode/photos/alpha_src.png")
EOF
/tmp/opencode/pio-venv/bin/python tools/raw_convert.py /tmp/opencode/photos/alpha_src.png --out /tmp/opencode/photos --force
# 3) 覆寫拒絕：再轉一次不加 --force 應失敗
/tmp/opencode/pio-venv/bin/python tools/raw_convert.py /tmp/opencode/photos/exif_src.jpg --out /tmp/opencode/photos; echo "exit=$?"
```

Expected：
- EXIF JPEG 轉檔成功；以 PIL 讀回 RAW 判斷長邊為水平（
  `-c "from PIL import Image; im=Image.open('/tmp/opencode/photos/exif_src.raw'); print(im.size)"` 由工具契約保證 792x272，改以視覺樣本於 Task 4 硬體檢查確認方向）
- 透明 PNG 轉檔成功；RAW payload 中透明區（映射座標）皆為 0
  （可用 `python -c` 比對座標 bit）
- 第三次轉檔 exit 非 0（拒絕覆寫，需 `--force`）

- [ ] **Step 5: Commit**

```bash
git add tools/raw_convert.py
git commit -m "SD 相框：RAW 轉檔工具

Pillow 實作：EXIF transpose、RGBA 合成白底、contain/cover 雙模式、
Floyd-Steinberg 抖動、12B 檔頭契約、預設拒覆寫(--force)、批次不遞迴。
含 --selftest 產生測試樣本；契約、polarity、EXIF/透明/覆寫拒絕
皆以 host 指令驗證通過。

驗證等級：無硬體（host 端驗證：檔頭/長度/polarity/EXIF/透明/覆寫
全數通過）。"
```

---

### Task 3: `photo_store` 掃描與排序

**Files:**
- Create: `src/photo_store.h`
- Create: `src/photo_store.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: 建立 `src/photo_store.h`**

```cpp
#pragma once
#include <Arduino.h>

#define MAX_FILES 128
#define MAX_NAME_LEN 64
#define BITMAP_BYTES 26928   // 792*272/8

// SD 上電＋掛載。回傳是否成功。
bool photoBegin();

// 掃描 /raw_photos/ 並排序（契約見 spec）：
//   >=0 張數；-1 = SD 層級錯誤；-2 = 超過 128 張；-3 = 資料夾不存在。
int photoScan();

// 排序後第 i 個檔名；越界回傳 nullptr。
const char* photoName(int i);

// 讀取第 i 檔：驗頭（magic/version/flags/w/h/reserved/檔案大小）後
// 讀入 g_bitmap。回傳 0 成功；-1 開檔失敗；-2 驗頭失敗；-3 讀取長度不符。
int photoLoad(int i);

// cleanup：關閉 handles、SD.end()、GPIO42 斷電。
void photoEnd();

extern uint8_t g_bitmap[BITMAP_BYTES];   // logical bitmap buffer（禁 stack）
extern int g_photoCount;
```

- [ ] **Step 2: 建立 `src/photo_store.cpp`**

```cpp
#include "photo_store.h"
#include "log.h"
#include <SD.h>
#include <string.h>

#define SD_CS   10
#define SD_SCK  39
#define SD_MISO 13
#define SD_MOSI 40
#define SD_PWR  42
#define DIR_PATH "/raw_photos"

static SPIClass sdSPI(HSPI);
uint8_t g_bitmap[BITMAP_BYTES];
int g_photoCount = 0;
static char g_names[MAX_FILES][MAX_NAME_LEN];

static bool isRawName(const char* n) {
  if (n[0] == '.') return false;                       // 隱藏 metadata
  const char* dot = strrchr(n, '.');
  if (!dot) return false;
  return strcasecmp(dot, ".raw") == 0;
}

static int nameCmp(const void* a, const void* b) {
  return strcmp((const char*)a, (const char*)b);       // byte-wise ASCII
}

bool photoBegin() {
  pinMode(SD_PWR, OUTPUT);
  digitalWrite(SD_PWR, LOW);
  digitalWrite(SD_PWR, HIGH);                          // 使用卡片前先拉高
  delay(10);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    LOGF("[fail] SD begin\n");
    digitalWrite(SD_PWR, LOW);
    return false;
  }
  return true;
}

int photoScan() {
  File dir = SD.open(DIR_PATH);
  if (!dir || !dir.isDirectory()) {
    LOGF("[fail] open dir %s\n", DIR_PATH);
    if (dir) dir.close();
    return -3;
  }
  int n = 0;
  File f;
  while ((f = dir.openNextFile()) && n < MAX_FILES) {
    if (f.isDirectory()) { f.close(); continue; }
    const char* name = f.name();
    if (!isRawName(name)) { f.close(); continue; }
    size_t len = strlen(name);
    if (len >= MAX_NAME_LEN) {
      LOGF("[warn] name too long, skip: %s\n", name);
      f.close();
      continue;
    }
    memcpy(g_names[n], name, len + 1);
    n++;
    f.close();
  }
  f.close();
  dir.close();
  if (n >= MAX_FILES) {
    // 確認是否真的超量（128 個後可能仍有檔未讀）
    if (dir.openNextFile()) {
      LOGF("[fail] too many photos\n");
      return -2;
    }
  }
  qsort(g_names, n, MAX_NAME_LEN, nameCmp);
  g_photoCount = n;
  LOGF("scan ok: %d photo(s)\n", n);
  for (int i = 0; i < n; i++) LOGF("  [%d] %s\n", i, g_names[i]);
  return n;
}

const char* photoName(int i) {
  if (i < 0 || i >= g_photoCount) return nullptr;
  return g_names[i];
}

int photoLoad(int i) {
  const char* name = photoName(i);
  if (!name) return -1;
  char path[16 + MAX_NAME_LEN];
  snprintf(path, sizeof(path), "/raw_photos/%s", name);
  File f = SD.open(path);
  if (!f) {
    LOGF("[fail] open %s\n", path);
    return -1;
  }
  if (f.size() != 12 + BITMAP_BYTES) {
    LOGF("[fail] size %s: %llu\n", name, (unsigned long long)f.size());
    f.close();
    return -2;
  }
  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12) {
    LOGF("[fail] read hdr %s\n", name);
    f.close();
    return -3;
  }
  bool hdrOk = memcmp(hdr, "EPFR", 4) == 0 && hdr[4] == 1 && hdr[5] == 0 &&
               hdr[6] == 792 % 256 && hdr[7] == 792 / 256 &&
               hdr[8] == 272 % 256 && hdr[9] == 272 / 256 &&
               hdr[10] == 0 && hdr[11] == 0;
  if (!hdrOk) {
    LOGF("[fail] header %s\n", name);
    f.close();
    return -2;
  }
  if (f.read(g_bitmap, BITMAP_BYTES) != BITMAP_BYTES) {
    LOGF("[fail] read payload %s\n", name);
    f.close();
    return -3;
  }
  f.close();
  LOGF("loaded %s\n", name);
  return 0;
}

void photoEnd() {
  SD.end();
  digitalWrite(SD_PWR, LOW);   // GPIO42 斷電
  LOGF("SD ended, GPIO42 low\n");
}
```

- [ ] **Step 3: `main.cpp` 接上掃描**

```cpp
#include <Arduino.h>
#include "log.h"
#include "photo_store.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("photo frame boot\n");
  if (!photoBegin()) {
    LOGF("no sd\n");
    return;
  }
  int n = photoScan();
  if (n > 0) {
    LOGF("first = %s\n", photoName(0));
    photoEnd();
  } else {
    photoEnd();
    LOGF("scan failed code=%d\n", n);
  }
}

void loop() {
  delay(100);
}
```

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

請使用者把 Task 2 產生的測試檔複製到 SD 卡 `/raw_photos/`（可加入
`001_aaa.raw`、`002_bbb.raw`、`010_ccc.raw` 一組命名樣本），上傳觀察：

```sh
/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor
```

Expected：
- `scan ok: N photo(s)` 與 `[i] name` 清單
- 字典序正確（`001_` < `002_` < `010_`）
- `first = ...` 為排序後第一筆

另測兩條：拔卡 → `[fail] SD begin`；資料夾改名（暫時）→ `open dir` 失敗。

額外（矩陣 #12）：暫時複製出 **129 個 `.raw` 檔** → 應印
`[fail] too many photos`（掃描 code -2）；測完移除多餘檔。

- [ ] **Step 6: Commit**

```bash
git add src/photo_store.h src/photo_store.cpp src/main.cpp
git commit -m "SD 相框：photo_store 掃描與排序

SD 掛載/GPIO42 電源、掃描過濾契約（一般檔/不遞迴/.raw 大小寫不敏感/
跳隱藏檔/>64B 跳過/超 128 報錯）、byte-wise ASCII 排序檔名清單、
cleanup。實機掃描與字典序驗證通過。

驗證等級：有硬體（掃描/排序/無卡/無資料夾路徑通過）。"
```

---

### Task 4: 讀檔驗頭與全幅顯示

**Files:**
- Create: `src/ui.h`
- Create: `src/ui.cpp`（相框版）
- Modify: `src/main.cpp`

- [ ] **Step 1: 建立 `src/ui.h`**

```cpp
#pragma once
#include <Arduino.h>

// GPIO7 上電、SPI/display init、解除深睡 hold。
void uiPowerOnInit();

// 全幅顯示 g_bitmap（白底＋黑色 drawBitmap）。
void uiShowPhoto();

// 全屏提示（NO PHOTOS / NO VALID PHOTOS / TOO MANY PHOTOS）。
void uiShowMessage(const char* title, const char* detail);

// 設定選單：title 為「SLIDESHOW」，options 為 OFF/1/5/15/30 MIN 字串陣列。
void uiMenuScreen(int cursor);

// 選單選項列 partial 更新（上下移動游標時呼叫）。
void uiMenuUpdate(int cursor);

// hibernate + GPIO7 低。
void uiHibernate();

// 深睡前控制線固定 LOW＋hold。
void uiSleepHoldPins();
```

- [ ] **Step 2: 建立 `src/ui.cpp`（相框版）**

```cpp
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

void uiMenuUpdate(int cursor) {
  int y0 = 116;  // 選項列區域（從 y0 起 5 行）
  display.setPartialWindow(40, y0, 400, 5 * 26 + 6);
  display.firstPage();
  do {
    display.fillRect(40, y0, 400, 5 * 26 + 6, GxEPD_WHITE);
    for (int i = 0; i < MENU_COUNT; i++) {
      char line[32];
      snprintf(line, sizeof(line), "%s  %s", i == cursor ? ">" : " ",
               MENU_OPTIONS[i]);
      drawText(60, y0 + 10 + i * 26, F_BODY, line);
    }
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
```

- [ ] **Step 3: `main.cpp` 顯示首張**

```cpp
#include <Arduino.h>
#include "log.h"
#include "photo_store.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("photo frame boot\n");
  uiPowerOnInit();
  if (!photoBegin()) {
    uiShowMessage("NO SD", "insert card with /raw_photos");
    uiHibernate();
    return;
  }
  int n = photoScan();
  if (n < 0) {
    uiShowMessage("SCAN FAIL", "check card filesystem");
    photoEnd();
    uiHibernate();
    return;
  }
  if (n == 0) {
    uiShowMessage("NO PHOTOS", "put .raw files in /raw_photos");
    photoEnd();
    uiHibernate();
    return;
  }
  if (photoLoad(0) != 0) {
    uiShowMessage("LOAD FAIL", "photo 0 invalid");
    photoEnd();
    uiHibernate();
    return;
  }
  uiShowPhoto();
  photoEnd();
  LOGF("shown photo 0, sleeping\n");
  uiHibernate();
}

void loop() {
  delay(100);
}
```

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

```sh
/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor
```

Expected：`shown photo 0, sleeping`；畫面顯示 `white_contain`（應為全白
無雜點）、翻卡改測 `black_contain`（全黑）確認無反相；再放漸層/格線/
標字樣本確認 dithering 呈現。

- [ ] **Step 6: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "SD 相框：讀檔驗頭與全幅顯示

photoLoad 依契約驗證（magic/version/flags/w/h/reserved/檔案大小）
後讀入 g_bitmap；ui 相框版全幅渲染（白底＋黑 drawBitmap）、提示
畫面、選單骨架；main 顯示首張後深睡。實機確認無反相、dithering
正常。

驗證等級：有硬體（首張顯示與 polarity 驗證通過）。"
```

---

### Task 5: 三鍵喚醒分流與翻頁

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 加入喚醒基礎設施與翻頁狀態機**

```cpp
#include <esp_sleep.h>
#include "driver/rtc_io.h"

#define BTN_UP    6
#define BTN_DOWN  4
#define BTN_PRESS 5

#define WAIT_RELEASE_MS 2000
#define SLEEP_US_STUCK  (5ULL * 60ULL * 1000000ULL)
#define SLEEP_US_RETRY  (5ULL * 60ULL * 1000000ULL)

static bool waitButtonsReleased(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (digitalRead(BTN_UP) && digitalRead(BTN_DOWN) && digitalRead(BTN_PRESS)) {
      return true;
    }
    delay(10);
    yield();
  }
  LOGF("[warn] buttons still held after %lu ms\n", (unsigned long)timeoutMs);
  return false;
}

static bool anyWakePinLow() {
  return !digitalRead(BTN_UP) || !digitalRead(BTN_DOWN) || !digitalRead(BTN_PRESS);
}

static void goToDeepSleep(uint64_t timerUs, bool enableExt1) {
  uiHibernate();
  uiSleepHoldPins();
  Serial.flush();
  delay(500);
  if (timerUs > 0) {
    esp_sleep_enable_timer_wakeup(timerUs);
  } else {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }
  if (enableExt1) {
    esp_sleep_enable_ext1_wakeup((1ULL << BTN_UP) | (1ULL << BTN_DOWN) |
                                 (1ULL << BTN_PRESS), ESP_EXT1_WAKEUP_ANY_LOW);
    for (uint8_t p : {(uint8_t)BTN_UP, (uint8_t)BTN_DOWN, (uint8_t)BTN_PRESS}) {
      rtc_gpio_init((gpio_num_t)p);
      rtc_gpio_set_direction((gpio_num_t)p, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_en((gpio_num_t)p);
      rtc_gpio_pulldown_dis((gpio_num_t)p);
    }
  } else {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
  }
  esp_deep_sleep_start();
}
```

- [ ] **Step 2: setup() 改為喚醒分流＋翻頁**

```cpp
static esp_sleep_wakeup_cause_t g_wake;
static uint64_t g_wakeMask = 0;

static int computeIndex(int cur, int count) {
  switch (g_wake) {
    case ESP_SLEEP_WAKEUP_EXT1:
      if (g_wakeMask & (1ULL << BTN_PRESS)) return cur;            // 選單用
      if (g_wakeMask & (1ULL << BTN_UP))    return (cur - 1 + count) % count;
      if (g_wakeMask & (1ULL << BTN_DOWN))  return (cur + 1) % count;
      return cur;
    case ESP_SLEEP_WAKEUP_TIMER:
      return (cur + 1) % count;                                    // 輪播
    default:
      return cur;                                                  // 開機：NVS
  }
}

static bool stuckGuard = false;

void setup() {
  Serial.begin(115200);
  delay(500);
  g_wake = esp_sleep_get_wakeup_cause();
  g_wakeMask = esp_sleep_get_ext1_wakeup_status();
  LOGF("boot wake=%d mask=%llu\n", (int)g_wake, (unsigned long long)g_wakeMask);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_PRESS, INPUT_PULLUP);

  // 喚醒分流依暫存器；互動前等待釋放（卡鍵防護）
  bool released = waitButtonsReleased(WAIT_RELEASE_MS);
  stuckGuard = !released;
  if (stuckGuard) LOGF("[warn] stuck button detected\n");

  uiPowerOnInit();
  if (!photoBegin()) {
    uiShowMessage("NO SD", "insert card with /raw_photos");
    photoEnd();
    uiHibernate();
    goToDeepSleep(SLEEP_US_RETRY, !stuckGuard && !anyWakePinLow());
    return;
  }
  int n = photoScan();
  if (n <= 0) {
    const char* t = (n == -2) ? "TOO MANY PHOTOS" : "NO PHOTOS";
    uiShowMessage(t, "fix card /raw_photos");
    photoEnd();
    uiHibernate();
    goToDeepSleep(0, !stuckGuard && !anyWakePinLow());  // 無照片：不掛 timer
    return;
  }
  int idx = computeIndex(loadIdx(), n);
  int shown = -1;
  for (int attempt = 0; attempt < n; attempt++) {
    int cand = (idx + attempt) % n;                  // 只向前、最多一圈
    if (photoLoad(cand) == 0) { shown = cand; break; }
  }
  if (shown < 0) {
    uiShowMessage("NO VALID PHOTOS", "all files invalid");
    photoEnd();
    uiHibernate();
    goToDeepSleep(0, !stuckGuard && !anyWakePinLow());
    return;
  }
  uiShowPhoto();
  photoEnd();
  saveIdx(shown);
  LOGF("shown photo %d (%s)\n", shown, photoName(shown));
  uiHibernate();
  goToDeepSleep(0, !stuckGuard && !anyWakePinLow());  // timer 由 Task 7 接上
}
```

- [ ] **Step 3: 加入 NVS helper**

```cpp
#include <Preferences.h>

static int loadIdx() {
  Preferences prefs;
  prefs.begin("photo", true);
  int v = prefs.getInt("idx", 0);
  prefs.end();
  return v;
}

static void saveIdx(int v) {
  Preferences prefs;
  prefs.begin("photo", false);
  prefs.putInt("idx", v);
  prefs.end();
}
```

（`loadIdx` 需在 `computeIndex` 前宣告或移至 Step 1 區塊上方。）

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

```sh
/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor
```

- 開機顯示 `001_` 首張（或 NVS 記住的索引）→ 入睡
- 按 UP：上一張、DOWN：下一張（含最後一張→第一張循環）
- 三鍵同時按喚醒：log mask 顯示多 bit，優先序 PRESS 生效（暫以
  log 判斷「進入 cur 不變」的分支）
- 按住 DOWN 不放過 2 s：`[warn] stuck button detected`＋執行一次
  動作＋`boot wake` 後無立即再次喚醒
- （矩陣 #11）把卡片換成**全毀檔**（僅壞檔）：應只搜尋一圈並顯示
  `NO VALID PHOTOS`、入睡且不掛 timer

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "SD 相框：三鍵喚醒分流與翻頁

EXT1 UP/DOWN/PRESS、暫存器分流、優先序 PRESS>UP>DOWN、等待按鍵
釋放與卡鍵防護（stuck 時 timer-only 短睡防喚醒迴圈）、wake->索引
轉換表、壞檔向前搜尋一圈、顯示成功才寫 NVS idx。實機翻頁/循環/
卡鍵路徑驗證通過。

驗證等級：有硬體（喚醒分流與翻頁驗證通過）。"
```

---

### Task 6: 設定選單與 NVS 輪播間隔

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/ui.h`、`src/ui.cpp`（選單已具備，接互動）

- [ ] **Step 1: 加入間隔常數與 NVS `slide`**

```cpp
// 輪播間隔（秒）；與 ui.cpp MENU_OPTIONS 順序對應
static const uint32_t SLIDE_OPTIONS_SEC[] = {0, 60, 300, 900, 1800};
static const int SLIDE_OPTIONS_COUNT = 5;
static const uint32_t SLIDE_DEFAULT_SEC = 0;   // 預設 OFF

static int loadSlideIdx() {
  Preferences prefs;
  prefs.begin("photo", true);
  uint32_t sec = prefs.getUInt("slide", SLIDE_DEFAULT_SEC);
  prefs.end();
  for (int i = 0; i < SLIDE_OPTIONS_COUNT; i++) {
    if (SLIDE_OPTIONS_SEC[i] == sec) return i;
  }
  return 0;
}

static void saveSlideIdx(int v) {
  Preferences prefs;
  prefs.begin("photo", false);
  prefs.putUInt("slide", SLIDE_OPTIONS_SEC[v]);
  prefs.end();
}
```

- [ ] **Step 2: 選單互動迴圈**

```cpp
// 回傳確認的選單游標；PRESS 或 idle 20s 皆保存（語意相同）
static int menuLoop(int cur) {
  uiMenuScreen(cur);
  uint32_t lastAct = millis();
  uint32_t debounceAt = 0;
  bool last[2] = {true, true};   // UP, DOWN（PRESS 在外部處理）
  while (true) {
    uint32_t now = millis();
    if (now - lastAct > 20000) {
      LOGF("menu idle, save cursor %d\n", cur);
      return cur;
    }
    if (now - debounceAt < 30) { delay(5); yield(); continue; }
    debounceAt = now;
    if (!digitalRead(BTN_PRESS)) {
      LOGF("menu confirm %d\n", cur);
      return cur;               // 下壓確認（等待釋放由上一層負責）
    }
    for (int i = 0; i < 2; i++) {
      uint8_t pin = (i == 0) ? BTN_UP : BTN_DOWN;
      bool released = digitalRead(pin);
      if (released != last[i]) {
        last[i] = released;
        if (!released) {
          lastAct = millis();
          cur = (i == 0) ? (cur + 1) % SLIDE_OPTIONS_COUNT
                         : (cur + SLIDE_OPTIONS_COUNT - 1) % SLIDE_OPTIONS_COUNT;
          uiMenuUpdate(cur);
          LOGF("menu cursor %d (%lu s)\n", cur,
               (unsigned long)SLIDE_OPTIONS_SEC[cur]);
        }
      }
    }
    delay(5);
    yield();
  }
}
```

- [ ] **Step 3: setup() 接上 PRESS → 選單 → 還原照片**

在 `uiShowPhoto();` 之後、`photoEnd();` 之前插入（PRESS 喚醒才走）：

```cpp
  if (g_wake == ESP_SLEEP_WAKEUP_EXT1 && (g_wakeMask & (1ULL << BTN_PRESS))) {
    waitButtonsReleased(WAIT_RELEASE_MS);       // 確認前釋放下壓
    int cur = loadSlideIdx();
    int picked = menuLoop(cur);
    if (picked != cur) saveSlideIdx(picked);    // 僅變更時寫入
    if (photoLoad(shown) != 0) {                // 內容被選單覆蓋，重讀原圖
      uiShowMessage("LOAD FAIL", "photo invalid");
      photoEnd();
      uiHibernate();
      goToDeepSleep(0, !stuckGuard && !anyWakePinLow());
      return;
    }
    uiShowPhoto();                              // 照片還原（第 2 次內容 full refresh）
    LOGF("menu done, slideshow idx=%d\n", picked);
  }
```

- [ ] **Step 4: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: 硬體檢查點**

```sh
/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor
```

- 按 PRESS 喚醒：直接進選單（不重刷照片——若照片未變化即正確）
- 上下循環選項（partial 更新）、選 `5 min` 下壓確認 → 照片還原 → 入睡
- RESET 重開：`slide` 讀回 300（選單顯示 `>  5 min`）
- idle 20 s 不按鍵：自動保存當前游標並還原照片

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/ui.h src/ui.cpp
git commit -m "SD 相框：設定選單與輪播間隔 NVS

PRESS 直接進選單（不重刷照片）、上下循環＋partial 更新、下壓或
idle 20s 皆保存（語意相同）、slide 僅變更時寫入、退出後重讀原圖
還原。實機選單/NVS 跨 reset/idle 保存驗證通過。

驗證等級：有硬體（選單與 NVS 驗證通過）。"
```

---

### Task 7: 輪播 timer

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 入睡前依輪播設定掛 timer**

將 setup() 最後的入睡呼叫改為：

```cpp
  int slideIdx = loadSlideIdx();
  uint32_t timerUs = 0;
  if (SLIDE_OPTIONS_SEC[slideIdx] > 0 && g_photoCount > 0) {
    timerUs = (uint64_t)SLIDE_OPTIONS_SEC[slideIdx] * 1000000ULL;
  }
  goToDeepSleep(timerUs, !stuckGuard && !anyWakePinLow());
```

（無照片／OFF 時 `timerUs=0` → 只留三鍵 EXT1，符合 spec。）

- [ ] **Step 2: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 3: 硬體檢查點**

```sh
/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor
```

- 選單設 `1 min` → 入睡後約 1 分鐘自動醒來顯示下一張（log `boot wake=3`）
- 設回 `OFF` → 入睡後 2 分鐘內不自動醒（無 timer），按鍵才喚醒
- 拔卡狀態下設 `1 min`：入睡後不自動醒（`timerUs=0` 路徑）

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "SD 相框：輪播 timer

依 NVS slide 掛載 timer（OFF/無照片一律不掛）；timer 喚醒自動
下一張。實機 1 分鐘檔自動推進與 OFF/無卡停用驗證通過。

驗證等級：有硬體（輪播週期驗證通過）。"
```

---

### Task 8: SD cleanup 強化與讀取中拔卡防護

**Files:**
- Modify: `src/photo_store.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: photoLoad 讀取錯誤時主動回報**

`photoLoad` 的讀取段改為逐次檢查（部分讀取即失敗，不依賴 EOF 例外）：

```cpp
  size_t got = 0;
  while (got < BITMAP_BYTES) {
    int r = f.read(g_bitmap + got, BITMAP_BYTES - got);
    if (r <= 0) break;
    got += r;
  }
  if (got != BITMAP_BYTES) {
    LOGF("[fail] read payload %s got=%u\n", name, (unsigned)got);
    f.close();
    return -3;
  }
```

- [ ] **Step 2: main 全路徑 cleanup 檢查**

確認下列每個分支在 `return`／入睡前都已執行 `photoEnd()`：
1. `photoBegin()` 失敗（無 SD）——begin 失敗自身已 GPIO42 low，無
   `SD.end()` 需求
2. `photoScan()` 負值／0
3. 全毀檔（`shown < 0`）
4. 正常顯示
5. 選單後重載失敗
6. 讀取中拔卡：`f.read` 回傳 `r<=0` 或 `SD` 層級錯誤 → 走 `-3` 分支
   → 呼叫端 `photoEnd()`

以 `rtk grep -n 'photoEnd()' src/main.cpp` 核對每個失敗分支都有一
次呼叫（Step 2 的無 SD 分支由 `photoBegin` 內部處理 GPIO42）。

- [ ] **Step 3: 編譯**

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 4: 硬體檢查點**

```sh
/tmp/opencode/pio-venv/bin/pio run -t upload && /tmp/opencode/pio-venv/bin/pio device monitor
```

- 顯示一張圖的瞬間**拔卡**：log 顯示 `[fail] read payload` 或跳至
  錯誤畫面，無 crash、無 reset；`GPIO42 low` 有印出（`photoEnd` 執行）
- 各錯誤路徑（拔卡喚醒、空資料夾、全毀檔）結尾 GPIO42 為 low
  （以電表或 serial 確認 `SD ended, GPIO42 low`）
- （矩陣 #10）準備 6 個壞檔各驗證一次：version≠1／flags≠0／
  reserved≠0／寬高不符／short file（<26,940 B）／oversized file：
  逐一放置於 `/raw_photos/` 與正常檔混排，應被依序跳過並顯示
  下一有效檔，不 crash

- [ ] **Step 5: Commit**

```bash
git add src/photo_store.cpp src/main.cpp
git commit -m "SD 相框：cleanup 強化與讀取中拔卡防護

photoLoad 逐段讀取並檢查（部分讀取即失敗）；全路徑 photoEnd
核對（grep 驗證各失敗分支皆有 cleanup）；實機拔卡不 crash、
GPIO42 全路徑為 low。

驗證等級：有硬體（拔卡與 cleanup 路徑驗證通過）。"
```

---

### Task 9: 收尾記錄與文件同步

**Files:**
- Modify: `docs/device-research.md`
- Modify: `README.md`

- [ ] **Step 1: ≥20 次連續 sleep/wake（矩陣 #8）**

硬體檢查點：連續執行 20 次「三鍵輪流喚醒→顯示→入睡」（UP/DOWN/PRESS
交替），全程無殘留事件、無 Busy Timeout；log 完整保存。

- [ ] **Step 2: 分段耗時量測**

從各檢查點 serial log 彙整：boot／mount／scan+sort／read／render
（full refresh）／total 六段數值（典型卡）。

- [ ] **Step 2: `docs/device-research.md` 末尾新增小節**

```markdown
## SD 相框應用量測（2026-08-26）

量測方法：SD 相框韌體 serial log（見
`docs/superpowers/specs/2026-08-26-photo-frame-design.md`）。

| 項目 | 結果 |
| --- | --- |
| SD mount | <xxx ms> |
| 掃描＋排序（N 張） | <xxx ms> |
| RAW 讀入（26,928 B） | <xxx ms> |
| full refresh 渲染 | <xxx ms>（與 bring-up 量測一致） |
| 翻頁總耗時（喚醒→顯示） | <xxx ms>（目標 ≤6 s、驗收 ≤8 s） |
| 翻頁循環 | 通過 |
| 選單＋NVS | 通過 |
| 輪播（1 分鐘檔） | 通過 |
| ≥20 次連續 sleep/wake | 通過 |
| 讀取中拔卡 | 通過（無 crash、GPIO42 low） |

異常事件：<無／條列>
```

- [ ] **Step 3: README 同步**

- 「專案狀態」：現行韌體改為 SD 相框 v1；天氣看板改列「由 `weather-v1`
  標籤保存」
- 「工具鏈與依賴」：移除 ArduinoJson
- 「快速開始」：移除 secrets 步驟（本應用無 Wi-Fi）；補「SD 卡 FAT32＋
  `/raw_photos/*.raw`（用 `tools/raw_convert.py` 轉檔）」
- 「目錄結構」：改為 `photo_store.*`、`ui.*`、`tools/raw_convert.py`
- 「文件」：補 SD 相框 spec/plan 連結

- [ ] **Step 4: 最終一致性檢查**

Run: `rtk rg -n 'TBD|TODO|PLACEHOLDER|待補' src/ tools/ docs/superpowers/specs/2026-08-26-photo-frame-design.md docs/superpowers/plans/2026-08-26-photo-frame.md`
Expected: exit 1（無匹配；計畫文件自身指令除外）

Run: `/tmp/opencode/pio-venv/bin/pio run`
Expected: SUCCESS

- [ ] **Step 5: Commit**

```bash
git add docs/device-research.md README.md
git commit -m "記錄 SD 相框量測與文件同步

分段耗時、週期行為、拔卡防護結果記錄至 device-research.md；
README 更新為相框現況（weather-v1 標籤保存天氣看板）。

驗證等級：有硬體（完整應用週期驗證完成）。"
```
