# ESP32 E-Paper

## 裝置

本專案以 **ELECROW CrowPanel ESP32 5.79-inch `DIS08792E`** 電子紙開發板為目標裝置。

- MCU 模組：`ESP32-S3-WROOM-1-N8R8`
- 記憶體：8 MB Flash、8 MB OPI PSRAM
- 顯示器：792x272、黑白電子紙、雙 `SSD1683`
- 裝置沒有觸控與背光

## 專案狀態

| 階段 | 狀態 |
| --- | --- |
| Bring-up（編譯/上傳/serial/全刷/局部刷新/sleep-wake＋按鍵、microSD、Wi-Fi 掃描） | 完成，標籤 `bringup-v1` |
| 天氣看板 v1（四地點 Open-Meteo、30 分睡眠週期、撥桿切換地點） | 完成，標籤 `weather-v1` |
| SD 相框 v1（`/raw_photos/*.raw` 瀏覽、手動翻頁＋自動輪播、三鍵喚醒） | 完成，標籤 `photo-frame-v1` |
| 報價看板 v2（雙頁八檔）— 開發中，分支 `feature/quote-board-multipage` | **候選韌體** |

目前韌體即**報價看板 v2 候選**：直立安裝，第 1 頁顯示加權指數＋台積電／鴻海／
元大台灣50／富邦台50，第 2 頁顯示加權指數＋中興電／中華電／富邦金／中鋼，
盤中（09:00–13:30）每 5 分鐘邊界更新並保留目前頁，13:35 收盤定格後長睡至次
交易日 09:00。先前應用保留於 git 標籤：bring-up＝`bringup-v1`、天氣看板＝
`weather-v1`、SD 相框＝`photo-frame-v1`（`git checkout photo-frame-v1` 即可
回復相框）；開發紀錄見各 feature 分支 commit 歷史、
`docs/superpowers/specs/2026-08-29-quote-board-design.md` 與
`docs/superpowers/specs/2026-09-03-quote-board-multipage-design.md`。

## 工具鏈與依賴（已固定版本）

PlatformIO + Arduino framework。`platformio.ini` 已 pin：

- `espressif32@7.0.1`（Arduino core ESP32 2.0.17）
- `zinggjm/GxEPD2@1.6.9`（面板 class `GxEPD2_579_GDEY0579T93`）
- `olikraus/U8g2_for_Adafruit_GFX@1.8.0`
- `bblanchon/ArduinoJson@7.4.3`

Board 設定：`esp32-s3-devkitc-1` 相容定義、8 MB Flash、`qio_opi` PSRAM、涵蓋完整 8 MiB 的 partition layout。

## 開發環境建置

### 前置需求

- git、Python 3（含 `venv` 模組）
- 可傳輸資料的 USB-C 線
- Linux 序列埠權限：`sudo usermod -a -G dialout $USER`，執行後需**重新登入**才生效

### PlatformIO 安裝（隔離 venv）

系統 pip 受 PEP 668（externally-managed-environment）限制，因此安裝在獨立 venv：

```sh
python3 -m venv /tmp/opencode/pio-venv
/tmp/opencode/pio-venv/bin/pip install platformio pillow
```

- `pio` 二進位路徑：`/tmp/opencode/pio-venv/bin/pio`（本專案文件與慣例均以此完整路徑呼叫）
- `pillow` 供字型重產工具 `tools/gen_fonts.py` 使用（僅重產字型時需要）

### 首次編譯

第一次 `pio run` 會自動下載工具鏈至 `~/.platformio/`（espressif32@7.0.1、xtensa/riscv toolchain、esptool、scons 等，約數百 MB，耗時數分鐘）；之後增量編譯很快。專案函式庫由 `platformio.ini` 固定版本並自動解析至 `.pio/libdeps/`。

### 環境重建

`/tmp/opencode/pio-venv` 若被系統清理，重跑上述兩行指令即可；`~/.platformio/` 的工具鏈不受影響，無須重新下載。

### Host 測試（純邏輯單元測試）

報價驗證／排程／NVS blob 邏輯抽成 `src/quote_logic.h`，於 host 以 g++ 測試：

```sh
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src \
    tests/host/test_quote_logic.cpp -o /tmp/opencode/test_quote_logic
/tmp/opencode/test_quote_logic        # 預期 ALL PASS
g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/GxEPD2/src \
    tests/host/test_rotation.cpp -o /tmp/opencode/test_rotation
/tmp/opencode/test_rotation
```

### 字型重產（僅 manifest 變更時）

`src/fonts_quote.c` 已提交 repo，日常開發不需要重產。新增/修改中文字元時：

```sh
git clone --depth 1 --branch 2.37.1 https://github.com/olikraus/u8g2 /tmp/opencode/u8g2
make -C /tmp/opencode/u8g2/tools/font/bdfconv
/tmp/opencode/pio-venv/bin/python tools/gen_fonts.py   # 需 Noto Sans CJK Bold（OFL）
```

產出 byte-identical 才算通過（可重現性契約，見 spec R5）。

## 快速開始（報價看板）

1. 建立 `src/secrets.h`（已 gitignore，不得 commit；範本見 `src/secrets.h.example`）：

   ```c
   #define WIFI_SSID "你的SSID"
   #define WIFI_PASS "你的密碼"
   ```

2. 使用可傳輸資料的 USB-C 線連接裝置（經板上 CH340C）。Linux 需將使用者加入 `dialout` 群組並重新登入。
3. 編譯、上傳、監看：

   ```sh
   /tmp/opencode/pio-venv/bin/pio run
   /tmp/opencode/pio-venv/bin/pio run -t upload
   /tmp/opencode/pio-venv/bin/pio device monitor -b 115200
   ```

4. 直立安裝（面板實體轉 90°）。運作週期：

   | 時段（週一~五） | 行為 |
   | --- | --- |
   | 00:00–08:59 | 長睡至 09:00 |
   | 09:00–13:30 | 每 5 分鐘邊界抓取＋渲染；未成交列顯示 `--` |
   | 13:30–13:34 | 收盤緩衝（MIS 撮合同步），畫面不變 |
   | 13:35 | 收盤定格（寫入 NVS）→ 長睡至次交易日 09:00 |
   | 週末／休市日 | 長睡至次交易日 09:00 |

    - **MENU 鍵**（GPIO2）：任何時刻按一下＝立即更新目前頁（一次抓取全部 9 檔）。抓取失敗顯示快取＋「更新失敗」，5 分後自動重試。
    - **UP（GPIO6）／DOWN（GPIO4）**：快取翻頁（不連網、不做 NTP、不寫 NVS），約一次 full refresh（約 4.4 秒）；翻頁後睡回原排程目標並保留目前頁。標頭顯示 `1/2` 或 `2/2`，快取時間沿用 `savedEpoch`。
    - **EXIT（GPIO1）／PRESS（GPIO5）**：本版未使用，不加入喚醒 mask。
    - Wi-Fi／NTP 失敗：顯示快取或錯誤訊息，5 分短睡重試；NTP 未同步時不判定市場狀態、不誤判假日。
    - 按住 MENU／UP／DOWN 任一喚醒鍵開機（卡鍵）：該輪 5 分鐘 timer-only（防喚醒迴圈），放開後恢復正常；按住 EXIT 或 PRESS 不觸發卡鍵防護。
    - 抓取走 HTTPS 並釘選 `TWCA Global Root CA`（`src/twse_root_ca.h`，2030-12-31 到期前需輪換）；資料源 `mis.twse.com.tw`，一次請求帶全部 9 個 `ex_ch`。

Upload speed 預設 `460800`；若燒錄不穩再降 `115200`。無法自動進入下載模式時：按住 `BOOT` → 點按 `RESET` → 放開 `RESET` → 放開 `BOOT` → 重試上傳。

## RAW 轉檔工具用法（SD 相框應用）

SD 相框（`photo-frame-v1` 標籤）用的轉檔工具；報價看板不使用，**檔案不在
現行工作樹**。使用前先切換：`git checkout photo-frame-v1`。將 JPG/PNG 轉成
相框可顯示的 `.raw`（792x272、1bpp 黑白、Floyd–Steinberg 抖動）。

### 基本轉檔

```sh
/tmp/opencode/pio-venv/bin/python tools/raw_convert.py 圖片.png --out 輸出資料夾
```

輸出檔名＝輸入 basename 加 `.raw` 副檔名（`圖片.png` → `圖片.raw`）。

### 參數

| 參數 | 說明 |
| --- | --- |
| `--mode contain`（預設） | 等比縮放完整放入畫面，上下/左右置中留白 |
| `--mode cover` | 等比放大蓋滿後中央裁切（畫面飽滿但裁邊） |
| `--force` | 輸出檔已存在時覆寫（預設拒絕，避免誤覆） |
| `--selftest` | 產生測試樣本組（漸層/格線/標字/純白/純黑 × contain/cover） |

批次：一次列出多個輸入檔即可（不遞迴）：

```sh
/tmp/opencode/pio-venv/bin/python tools/raw_convert.py a.png b.jpg c.png --out 輸出資料夾
```

### 注意事項

- 支援 `.png`／`.jpg`／`.jpeg`（大小寫不敏感）；批次會跳過非一般檔案與
  不接受之副檔名
- EXIF 旋轉資訊自動轉正；RGBA 透明區自動合成白底
- 檔名建議數字前綴（`001_xxx.raw`）控制播放順序（韌體按字典序排列）
- 轉出的 `.raw` **必須恰好 26,940 bytes**（12B 檔頭＋26,928 B 點陣）；
  不符表示轉檔失敗，韌體會視為壞檔跳過
- 畫面比例 2.91:1（792x272）：接近此比例的橫圖效果最好；直式圖用
  `contain` 會留白縮小、`cover` 會裁切

### 完整流程範例

```sh
mkdir -p /tmp/opencode/photos
/tmp/opencode/pio-venv/bin/python tools/raw_convert.py 全家福.png 風景.jpg --out /tmp/opencode/photos
ls -l /tmp/opencode/photos/*.raw    # 確認 26940 bytes
# 複製到 SD 卡 /raw_photos/ 後插卡即可瀏覽
```

## 目錄結構（現況）

```text
esp32-eink/
├── platformio.ini          # 工程設定（版本已固定）
├── src/
│   ├── main.cpp            # 狀態機（四市場狀態＋MENU 例外＋睡眠雙源）
│   ├── quote_logic.h       # 純邏輯：驗證/JSON/排程/blob（host 測試）
│   ├── quote_store.h/.cpp  # Wi-Fi/NTP/HTTPS（TLS 釘選）/JSON/NVS
│   ├── ui.h/.cpp           # 直式 A3 版面、狀態列、深睡 hold
│   ├── watchlist.h         # 固定九檔清單（t00＋八檔個股）＋ex_ch 組裝（修改後須同步字型 manifest 並重編譯燒錄）
│   ├── fonts_quote.c/.h    # U8g2 中文子集字型（tools/gen_fonts.py 產出）
│   ├── twse_root_ca.h      # TWCA Global Root CA（釘選；2030-12-31 到期）
│   ├── log.h               # 時間戳 LOGF
│   ├── secrets.h           # 本機檔（gitignored）；secrets.h.example 為範本
│   └── secrets.h.example
├── tests/host/             # quote_logic/rotation 純邏輯測試（g++）
├── tools/gen_fonts.py      # 中文子集字型重產（PIL→BDF→bdfconv）
├── docs/                   # 研究、規格、計畫、應用候選
├── README.md
└── AGENTS.md               # 代理人作業規則
```

## 開發時先知道

- 此型號沒有觸控與背光，不能套用 touch controller、觸控校正或 backlight GPIO 的設定。
- 顯示器由雙 `SSD1683` 驅動；可見畫面是 792x272，但 ELECROW 官方 driver 使用 800x272 內部 framebuffer，兩者不能直接互換。
- GxEPD2 的 partial 視窗狀態會殘留：`setPartialWindow()` 之後渲染整頁前必須先 `setFullWindow()`；且此面板 partial 視窗有座標對齊問題，**報價看板每次更新都整頁重繪**。
- **直式版面契約**：`display.setRotation(3)`（唯一旋轉層，硬體驗證過 3 才正立；1 是 180° 顛倒）＋邏輯座標 272x792＋`setFullWindow()` page loop。
- **U8g2 換字型必須走 `setFontT()`**（`setFont`＋`setFontMode(1)`＋白底）：直接 `u8g2_SetFont()` 會重設 solid mode，出現黑色 glyph 方塊（實機驗證）。
- 報價資料驗證：指數列（`t00`）缺列／重複／欄位無效則整批拒絕；個股列缺列／重複／欄位無效或日期與指數不同僅該列 `valid=false` 並顯示 `--`，所在頁另顯示「部分失敗」（另一頁不顯示；優先序：時間未同步＞更新失敗＞部分失敗）。盤中未成交 `z="-"` 記 0 並顯示 `--`，不觸發「部分失敗」。
- NVS 快取 v2（`BLOB_VERSION=2`）：舊 v1 blob 不遷移，升級後首次啟動視為無快取並重新連網抓取。write-on-change：逐欄位 `recordDiffers()`，禁止 `memcmp`（struct padding）；`quoteTime`/`savedEpoch` 單獨變更不寫。
- USB-C 經 CH340C 連到 UART，不是 ESP32-S3 原生 USB。
- 字型 manifest（`tools/gen_fonts.py`）：已含第二頁名稱（中興電／中華電／富邦金／中鋼新增字為中、興、華、金、鋼）與「部分失敗」（部、分）及標頭 `/`；修改清單名稱須同步更新 manifest 並重產 `src/fonts_quote.c`（byte-identical 才算通過）。
- 電池只能使用具保護電路的 1S 3.7 V LiPo／Li-ion；接線前要量測並確認極性，不得假設主板有完整 cell protection 或可讀取電池電壓的 ADC。目前仍以 USB 供電，電池尚未接入。
- 已知韌體行為（實機觀測，見 device-research）：NTP 首包偶有 stale 回應使時鐘偏差數分鐘、deep sleep timer 整夜漂移可達數分鐘（RC slow clock）；兩者皆因「睡眠目標為絕對 epoch」而自我修復，最壞多跑一輪循環。

## 文件

- [完整裝置研究](docs/device-research.md)——硬體事實、framebuffer、刷新流程與實機量測
- [代理人開發規則](AGENTS.md)
- [後續應用候選](docs/app-ideas.md)
- 規格與計畫：
  - [文件設計規格](docs/superpowers/specs/2026-08-19-device-documentation-design.md)
  - [Bring-up 驗證設計](docs/superpowers/specs/2026-08-24-bringup-verification-design.md)／[計畫](docs/superpowers/plans/2026-08-24-bringup-verification.md)
  - [天氣看板設計](docs/superpowers/specs/2026-08-25-weather-station-design.md)／[計畫](docs/superpowers/plans/2026-08-25-weather-station.md)
  - [SD 相框設計](docs/superpowers/specs/2026-08-26-photo-frame-design.md)／[計畫](docs/superpowers/plans/2026-08-26-photo-frame.md)
   - [報價看板設計](docs/superpowers/specs/2026-08-29-quote-board-design.md)／[計畫](docs/superpowers/plans/2026-08-29-quote-board.md)
   - [報價看板雙頁八檔設計](docs/superpowers/specs/2026-09-03-quote-board-multipage-design.md)／[計畫](docs/superpowers/plans/2026-09-03-quote-board-multipage.md)

## 資料來源

- [ELECROW 官方商品頁](https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html)
- [ELECROW 官方 Wiki](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-paper_5.79-inch_HMI_Display.html)
- [ELECROW 官方 GitHub repository](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792)
- [GxEPD2 1.6.9 `GxEPD2_579_GDEY0579T93` reference](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdey/GxEPD2_579_GDEY0579T93.h)
