@RTK.md

本檔案是 `esp32-eink/` 整包專案目錄的代理人作業手冊，涵蓋該目錄下所有子目錄與檔案，並與父層 `/home/brunolin/AGENTS.md` 並行互補；兩者衝突時，以更特定於本專案的規則為準。

## 語言與閱讀規則

- 文件一律用繁體中文撰寫；程式識別字、命令與 API 名稱保持英文原樣。
- 修改韌體或文件前，必須先讀 `README.md` 與 `docs/device-research.md`。
- 若 `docs/superpowers/specs/` 有 spec、`docs/plans/` 有 plan，一律遵循；spec 與 device-research 衝突時，以檔名或內容標註日期較新者為準，並須註明原因。
- 不可變硬體約束與電源／電池規則優先於任何 spec／plan；除非使用者明確同意且已完成原理圖核對，並留下核對紀錄，否則不得變更。

## 工具鏈規則

- 標準路徑：PlatformIO + Arduino framework + GxEPD2，面板 class 固定用 `GxEPD2_579_GDEY0579T93`。
- ELECROW 官方 Arduino EPD driver 僅作參考資料與硬體 bring-up 驗證用，不作正式應用程式基礎。
- 不得在未經使用者同意下導入第二套官方工具鏈（例如 ESP-IDF、MicroPython）。
- `platformio.ini` 首次建立時即固定依賴版本（library、framework、toolchain），首次成功編譯後核對並記錄；`platformio.ini` 必須放在專案根目錄。
- PlatformIO 安裝於 `/tmp/opencode/pio-venv/`（含 `pillow`），呼叫時用完整路徑 `/tmp/opencode/pio-venv/bin/pio`；venv 若被清理，依 README「開發環境建置」章節重建。
- partition layout 必須涵蓋完整 8 MiB Flash；不得照抄官方只映射 4 MiB 的 `Huge APP` 配置。
- 大而雜的輸出用 `rtk` 過濾；但需要精確原始輸出、除錯失敗、或權限需精準對應底層命令時，不要用 `rtk`。

## 不可變硬體約束

以下來自 `docs/device-research.md`，未經原理圖核對與使用者同意，不得變更或重指配：

- MCU：`ESP32-S3-WROOM-1-N8R8`；8 MB Flash；8 MB OPI PSRAM。
- 顯示器：`SCK` GPIO12、`MOSI` GPIO11、`RESET` GPIO47、`DC` GPIO46、`CS` GPIO45、`BUSY` GPIO48；顯示器電源／接地控制 GPIO7。
- GPIO7 僅切換顯示器的接地／供電路徑，不是整板電源開關；拉低不代表整板斷電。
- 按鍵（active-low）：`MENU` GPIO2、`EXIT` GPIO1、rocker up GPIO6、rocker down GPIO4、rocker press GPIO5。
- microSD：`MOSI` GPIO40、`MISO` GPIO13、`SCLK` GPIO39、`CS` GPIO10、`PWR_EN` GPIO42；使用卡片前先將 GPIO42 拉高，低功耗時關閉。
- `PWR` LED GPIO41；`UART0` GPIO44（RX）／43（TX），經 CH340C 連 USB-C。
- 不可假設有觸控輸入、原生 USB、背光、電池電壓 ADC 或內建完整電池保護。

## Framebuffer 規則

- 可見影像：792x272，1 bpp，共 26,928 bytes。
- 官方 driver 內部 framebuffer：800x272，共 27,200 bytes，含兩顆 SSD1683 之間的接縫 padding。
- 不得把 792x272 raw buffer 直接傳給 `EPD_Display` 這類 full-buffer API；使用 `EPD_ShowPicture` 語意或 GxEPD2 抽象層。
- 自寫 driver 時必須同時處理兩顆 controller 與接縫 byte。

## 顯示更新規則

- 開機或重新上電後，先做一次 clean full refresh。
- 自寫 driver 或使用官方 driver 路徑時，進行 differential partial refresh 前必須明確同步 current 與 previous RAM；呼叫 full update 本身不保證 previous RAM 同步（官方 driver 行為）。
- 使用已驗證的 library flow（如 GxEPD2 的 previous-frame 支援）時，library 內部自行維護兩份 RAM；應用層不得自行插入 previous-RAM 寫入。
- 每 5 至 30 次 partial refresh 插入一次 full refresh，是實務建議，不是面板保證；依殘影、內容與溫度調整。
- 等待 `BUSY` 必須有 timeout 並讓出執行權；禁止無限阻斷等待。
- 更新完成後：hibernate controller → GPIO7 拉低 → 依需停用無線電；若使用 microSD，先 flush、卸載卡片再關閉 GPIO42 → ESP32 deep sleep；喚醒時反向並重新初始化。
- GxEPD2 partial refresh 只限制 RAM data write，waveform 刷新範圍仍是 controller 層級；小矩形不表示更新時間按面積縮短。

## 電源與電池規則

- 電池：1S 3.7 V 具保護電路的 LiPo。
- 接 SH1.0 電池接頭前，必須用電表量測極性，不得相信線色。
- 不得假設板上有過放／短路保護；充電電流（約 500 mA）是推論，除非實測否則標示為 inference。
- 在 `docs/device-research.md` 記錄實測 sleep／active 電流時，附上量測方法說明。

## 密鑰規則

- 不得 commit 或 log Wi-Fi 憑證、API key、私鑰、token 或裝置身分。
- 憑證放 `.gitignore` 列出的設定檔或 env 變數，不得硬編碼進原始碼。
- 發現憑證外洩時：輪換該憑證並回報。

## 驗證等級

- 無硬體：允許依賴解析、`pio run` 編譯驗證，以及不涉及硬體的主機端測試（如 framebuffer packing、圖片轉換）；仍不得宣稱 flash、顯示、按鍵、電池、Wi-Fi 或 deep-sleep 成功。
- 有硬體：依序驗證並記錄結果：build → upload → serial log → full refresh → partial refresh → sleep/wake；記錄時間戳、刷新耗時、bus 設定與任何異常。
- 只回報實際執行的驗證；回應與 commit message 中明確標示驗證等級。
