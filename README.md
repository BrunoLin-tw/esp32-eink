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
| SD 相框 v1（`/raw_photos/*.raw` 瀏覽、手動翻頁＋自動輪播、三鍵喚醒） | **現行韌體** |
| 實機量測紀錄 | 見 [device-research.md](docs/device-research.md) 的量測章節 |

目前韌體即 **SD 相框**。先前應用保留於 git 標籤：bring-up＝`bringup-v1`、
天氣看板＝`weather-v1`（`git checkout weather-v1` 即可回復）；相框分支
開發紀錄見 `feature/photo-frame` 的 commit 歷史。

## 工具鏈與依賴（已固定版本）

PlatformIO + Arduino framework。`platformio.ini` 已 pin：

- `espressif32@7.0.1`（Arduino core ESP32 2.0.17）
- `zinggjm/GxEPD2@1.6.9`（面板 class `GxEPD2_579_GDEY0579T93`）
- `olikraus/U8g2_for_Adafruit_GFX@1.8.0`

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
- `pillow` 供轉檔工具 `tools/raw_convert.py` 使用

### 首次編譯

第一次 `pio run` 會自動下載工具鏈至 `~/.platformio/`（espressif32@7.0.1、xtensa/riscv toolchain、esptool、scons 等，約數百 MB，耗時數分鐘）；之後增量編譯很快。專案函式庫由 `platformio.ini` 固定版本並自動解析至 `.pio/libdeps/`。

### 環境重建

`/tmp/opencode/pio-venv` 若被系統清理，重跑上述兩行指令即可；`~/.platformio/` 的工具鏈不受影響，無須重新下載。

## 快速開始

1. 安裝 PlatformIO（VS Code extension 或 PlatformIO Core）。
2. 準備 SD 卡：**FAT32** 格式化（SD library 不支援 exFAT），建立
   `/raw_photos/` 資料夾，放入 `.raw` 檔。轉檔（JPG/PNG → RAW）：

   ```sh
   /tmp/opencode/pio-venv/bin/python tools/raw_convert.py 照片.jpg --out RAW輸出目錄
   # 支援 --mode contain（預設，置中留白）／cover（裁切填滿）、
   # --force（覆寫）、批次多檔；把輸出的 .raw 複製到卡上 /raw_photos/
   ```

3. 使用可傳輸資料的 USB-C 線連接裝置（經板上 CH340C）。Linux 需將使用者加入 `dialout` 群組並重新登入。
4. 編譯、上傳、監看：

   ```sh
   pio run
   pio run -t upload
   pio device monitor -b 115200
   ```

操作：撥桿**上／下**翻頁、**下壓**進選單設定輪播間隔（OFF/1/5/15/30 分，存 NVS）。

Upload speed 預設 `460800`；若燒錄不穩再降 `115200`。無法自動進入下載模式時：按住 `BOOT` → 點按 `RESET` → 放開 `RESET` → 放開 `BOOT` → 重試上傳。

## RAW 轉檔工具用法（`tools/raw_convert.py`）

將 JPG/PNG 轉成裝置可顯示的 `.raw`（792x272、1bpp 黑白、Floyd–Steinberg 抖動）。

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
│   ├── main.cpp            # 狀態機、三鍵喚醒分流、輪播 timer、NVS
│   ├── photo_store.h/.cpp  # SD 掛載/GPIO42、掃描排序、驗頭讀檔、cleanup
│   ├── ui.h/.cpp           # 全幅顯示、選單/提示畫面（U8g2 字型）、深睡 hold
│   ├── log.h               # 時間戳 LOGF
│   └── secrets.h           # 本機檔（gitignored，本應用不使用）
├── tools/raw_convert.py    # JPG/PNG → EPFR .raw 轉檔（Pillow）
├── docs/                   # 研究、規格、計畫、應用候選
├── README.md
└── AGENTS.md               # 代理人作業規則
```

## 開發時先知道

- 此型號沒有觸控與背光，不能套用 touch controller、觸控校正或 backlight GPIO 的設定。
- 顯示器由雙 `SSD1683` 驅動；可見畫面是 792x272，但 ELECROW 官方 driver 使用 800x272 內部 framebuffer，兩者不能直接互換。
- GxEPD2 的 partial 視窗狀態會殘留：`setPartialWindow()` 之後渲染整頁前必須先 `setFullWindow()`；且此面板 partial 視窗有座標對齊問題，**選單游標更新採整頁重繪**。
- RAW 契約：12-byte 檔頭（`EPFR`＋version/flags/w/h/reserved）＋26,928 B payload，bit1=黑 bit0=白、每列 99 B。
- USB-C 經 CH340C 連到 UART，不是 ESP32-S3 原生 USB。
- 電池只能使用具保護電路的 1S 3.7 V LiPo／Li-ion；接線前要量測並確認極性，不得假設主板有完整 cell protection 或可讀取電池電壓的 ADC。目前仍以 USB 供電，電池尚未接入。

## 文件

- [完整裝置研究](docs/device-research.md)——硬體事實、framebuffer、刷新流程與實機量測
- [代理人開發規則](AGENTS.md)
- [後續應用候選](docs/app-ideas.md)
- 規格與計畫：
  - [文件設計規格](docs/superpowers/specs/2026-08-19-device-documentation-design.md)
  - [Bring-up 驗證設計](docs/superpowers/specs/2026-08-24-bringup-verification-design.md)／[計畫](docs/superpowers/plans/2026-08-24-bringup-verification.md)
  - [天氣看板設計](docs/superpowers/specs/2026-08-25-weather-station-design.md)／[計畫](docs/superpowers/plans/2026-08-25-weather-station.md)
  - [SD 相框設計](docs/superpowers/specs/2026-08-26-photo-frame-design.md)／[計畫](docs/superpowers/plans/2026-08-26-photo-frame.md)

## 資料來源

- [ELECROW 官方商品頁](https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html)
- [ELECROW 官方 Wiki](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-paper_5.79-inch_HMI_Display.html)
- [ELECROW 官方 GitHub repository](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792)
- [GxEPD2 1.6.9 `GxEPD2_579_GDEY0579T93` reference](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdey/GxEPD2_579_GDEY0579T93.h)
