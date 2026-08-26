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
| 天氣看板 v1（四地點 Open-Meteo、30 分睡眠週期、撥桿切換地點） | 完成 |
| 實機量測紀錄 | 見 [device-research.md](docs/device-research.md) 的量測章節 |

目前韌體即天氣看板；bring-up 測試韌體保留在 git 歷史（標籤 `bringup-v1`）。

## 工具鏈與依賴（已固定版本）

PlatformIO + Arduino framework。`platformio.ini` 已 pin：

- `espressif32@7.0.1`（Arduino core ESP32 2.0.17）
- `zinggjm/GxEPD2@1.6.9`（面板 class `GxEPD2_579_GDEY0579T93`）
- `bblanchon/ArduinoJson@7.4.3`
- `olikraus/U8g2_for_Adafruit_GFX@1.8.0`

Board 設定：`esp32-s3-devkitc-1` 相容定義、8 MB Flash、`qio_opi` PSRAM、涵蓋完整 8 MiB 的 partition layout。

## 快速開始

1. 安裝 PlatformIO（VS Code extension 或 PlatformIO Core）。
2. 填入 Wi-Fi 憑證（此檔已被 `.gitignore` 排除，不得 commit）：

   ```sh
   cp src/secrets.h.example src/secrets.h
   # 編輯 src/secrets.h 填入 WIFI_SSID / WIFI_PASS
   ```

3. 使用可傳輸資料的 USB-C 線連接裝置（經板上 CH340C）。Linux 需將使用者加入 `dialout` 群組並重新登入。
4. 編譯、上傳、監看：

   ```sh
   pio run
   pio run -t upload
   pio device monitor -b 115200
   ```

Upload speed 預設 `460800`；若燒錄不穩再降 `115200`。無法自動進入下載模式時：按住 `BOOT` → 點按 `RESET` → 放開 `RESET` → 放開 `BOOT` → 重試上傳。

## 目錄結構（現況）

```text
esp32-eink/
├── platformio.ini          # 工程設定（版本已固定）
├── src/
│   ├── main.cpp            # 狀態機、sleep/wake、NVS 地點記憶
│   ├── weather.h/.cpp      # Wi-Fi、NTP、Open-Meteo 抓取與解析
│   ├── ui.h/.cpp           # 版面渲染（U8g2 字型）
│   ├── icons.h/.cpp        # WMO 天氣碼映射與圖示繪製
│   ├── icons_bitmaps.h     # 圖示點陣（由 tools/gen_icons.py 產生）
│   ├── locations.h         # 四地點表
│   ├── log.h               # 時間戳 LOGF
│   └── secrets.h.example   # Wi-Fi 憑證範本（secrets.h 不入庫）
├── tools/gen_icons.py      # 圖示產生器（Pillow）
├── docs/                   # 研究、規格、計畫、應用候選
├── README.md
└── AGENTS.md               # 代理人作業規則
```

## 開發時先知道

- 此型號沒有觸控與背光，不能套用 touch controller、觸控校正或 backlight GPIO 的設定。
- 顯示器由雙 `SSD1683` 驅動；可見畫面是 792x272，但 ELECROW 官方 driver 使用 800x272 內部 framebuffer，兩者不能直接互換。
- GxEPD2 的 partial 視窗狀態會殘留：`setPartialWindow()` 之後渲染整頁前必須先 `setFullWindow()`。
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

## 資料來源

- [ELECROW 官方商品頁](https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html)
- [ELECROW 官方 Wiki](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-paper_5.79-inch_HMI_Display.html)
- [ELECROW 官方 GitHub repository](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792)
- [GxEPD2 1.6.9 `GxEPD2_579_GDEY0579T93` reference](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdey/GxEPD2_579_GDEY0579T93.h)
