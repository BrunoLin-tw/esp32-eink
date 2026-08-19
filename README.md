# ESP32 E-Paper

## 裝置

本專案以 **ELECROW CrowPanel ESP32 5.79-inch `DIS08792E`** 電子紙開發板為目標裝置。

- MCU 模組：`ESP32-S3-WROOM-1-N8R8`
- 記憶體：8 MB Flash、8 MB OPI PSRAM
- 顯示器：792x272、黑白電子紙、雙 `SSD1683`
- 裝置沒有觸控與背光

## 開發方向

本專案規劃的標準開發路徑是 **PlatformIO + Arduino framework + GxEPD2**，面板 class 固定使用 `GxEPD2_579_GDEY0579T93`。GxEPD2 自 1.5.9 起支援此 class；裝置研究已對照 1.6.9，但編譯與實機驗證仍待完成。

ELECROW 官方 Arduino driver 主要作為硬體與參考資料來源，用於核對腳位、控制流程、原廠範例及初步硬體驗證，不作為正式應用程式的預設基礎。

## 專案狀態

目前專案只有文件。`platformio.ini`、原始碼與自動化測試都尚未建立；編譯、燒錄及上傳驗證也尚未完成。

## 快速開始

目前尚無可直接執行的 PlatformIO 工程。後續開始 bring-up 時：

1. 安裝 PlatformIO，可使用 VS Code extension 或 PlatformIO Core。
2. 使用可傳輸資料的 USB-C 線連接裝置；連線會經過板上的 CH340C USB-to-UART bridge。
3. 確認 serial device：Linux 通常是 `/dev/ttyUSB*`，macOS 通常是 `/dev/cu.usbserial-*`，Windows 則是 `COM` 編號。若沒有出現，先檢查線材、供電、系統裝置清單與 CH340 driver。
4. 初始化工程時，PlatformIO 應選用相容的 ESP32-S3 board definition、Arduino framework、8 MB Flash 與 OPI PSRAM。由於 serial port 使用 CH340C UART，需停用原生 USB CDC-on-boot；partition 應選用並驗證涵蓋 8 MiB 的 layout，或使用 custom CSV，不可直接照抄官方只映射 4 MiB 的 `Huge APP`。完成後先進行最小化編譯及 upload 驗證。

若無法自動進入下載模式，請按住 `BOOT`，按下再放開 `RESET`，接著放開 `BOOT`，然後重新 upload。

## 常用命令

以下命令只適用於 PlatformIO 工程建立完成之後：

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

初次 upload speed 建議設為 `460800`；若燒錄不穩，再降為 `115200`。

## 開發時先知道

- 此型號沒有觸控與背光，不能套用 touch controller、觸控校正或 backlight GPIO 的設定。
- 顯示器由雙 `SSD1683` 驅動；可見畫面是 792x272，但 ELECROW 官方 driver 使用 800x272 內部 framebuffer，兩者不能直接互換。
- 電子紙刷新較慢，快速或局部刷新可能產生 ghosting；實際策略必須依面板、內容與溫度驗證。
- USB-C 經 CH340C 連到 UART，不是 ESP32-S3 原生 USB。
- 電池只能使用具保護電路的 1S 3.7 V LiPo／Li-ion；接線前要量測並確認極性，不得假設主板有完整 cell protection 或可讀取電池電壓的 ADC。
- 目前沒有實機硬體驗證；在完成編譯、上傳、serial log 與顯示測試前，不應宣稱硬體流程可用。

## 預計目錄結構

以下是後續初始化工程時的規劃，不代表這些檔案與目錄目前已存在：

```text
esp32-eink/
├── platformio.ini
├── src/
├── include/
├── test/
├── docs/
├── README.md
└── AGENTS.md
```

## 文件

- [完整裝置研究](docs/device-research.md)
- [代理人開發規則](AGENTS.md)
- [文件設計規格](docs/superpowers/specs/2026-08-19-device-documentation-design.md)

## 資料來源

- [ELECROW 官方商品頁](https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html)
- [ELECROW 官方 Wiki](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-paper_5.79-inch_HMI_Display.html)
- [ELECROW 官方 GitHub repository](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792)
- [GxEPD2 1.6.9 `GxEPD2_579_GDEY0579T93` reference](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdey/GxEPD2_579_GDEY0579T93.h)
