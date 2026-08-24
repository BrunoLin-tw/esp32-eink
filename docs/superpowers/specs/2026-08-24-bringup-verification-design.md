# Bring-up 驗證韌體設計

- 日期：2026-08-24
- 狀態：已獲使用者核准（採用方案 A；sleep 用 timer 喚醒；測試圖樣用黑白格線＋文字框）
- 上位文件：`docs/device-research.md`（技術依據）、`AGENTS.md`（作業規則）

## 目標

在實機上完成全套週邊驗證並記錄數據，建立後續開發的可重現基礎。

## 範圍

- 本輪涵蓋：build、upload、serial log、full refresh、partial refresh、deep sleep/wake、按鍵、microSD 讀寫、Wi-Fi 掃描。
- 明確不做：電池／電流量測（本輪 USB 供電）、觸控與背光（硬體不存在）、OTA、任何應用功能、Wi-Fi 憑證連線（僅掃描）。

## 架構

- PlatformIO 工程建在 repo 根目錄；`platformio.ini` 建立時即固定版本：
  - `lib_deps`：GxEPD2 `1.6.9`（研究文件核對過的版本），面板 class `GxEPD2_579_GDEY0579T93`。
  - platform 與 framework 版本首次編譯成功後核對並記錄回本文件。
- Board 定義：`esp32-s3-devkitc-1` 相容定義 + Arduino framework。
- Flash/PSRAM：8 MB Flash；`qio_opi` memory type（N8R8 的 OPI PSRAM）；build flag `-DBOARD_HAS_PSRAM`。
- Partition：使用涵蓋完整 8 MiB 的 layout（如 `default_8MB.csv`）；不得照抄官方僅映射 4 MiB 的配置。
- 單檔 `src/main.cpp`，serial 選單驅動；monitor 115200 baud；所有輸出帶 `millis()` 時間戳。
- Upload speed 先用 460800，不穩時降至 115200。

## 測試階段（serial 單鍵指令）

| 指令 | 測試 | 內容 |
| --- | --- | --- |
| `i` | info | 晶片型號／revision、Flash 大小、PSRAM 偵測結果。 |
| `d` | display | GPIO7 拉高 → GxEPD2 init（SPI：SCK GPIO12、MOSI GPIO11、CS GPIO45、DC GPIO46、RST GPIO47、BUSY GPIO48）→ clean full refresh 顯示黑白格線＋文字框測試圖樣，記錄耗時。 |
| `p` | partial | 局部刷新循環，畫面顯示更新計數器，逐次計時；依 AGENTS.md 每 5–30 次插入一次 full refresh。 |
| `b` | buttons | MENU GPIO2、EXIT GPIO1、撥桿 up GPIO6／down GPIO4／press GPIO5（皆 active-low，含 debounce），按下即 serial 回報。 |
| `s` | sd | GPIO42 拉高 → init → 寫入測試檔並讀回比對 → 卸載 → GPIO42 拉低。 |
| `w` | wifi | Wi-Fi 掃描（不需憑證），回報 SSID/RSSI 清單；完成即停用射頻。 |
| `z` | sleep | hibernate 顯示器 → GPIO7 拉低 → Wi-Fi off → 若 SD 已掛載則 flush/unmount 後關閉 GPIO42 → deep sleep；timer 10 秒喚醒，喚醒後印出 wake reason 與 RTC memory 的 boot count。 |

## 錯誤處理

- 所有 BUSY 等待必須帶 timeout；逾時回報錯誤並中止該項測試，禁止無限阻斷等待。
- 各測試互相獨立；任一失敗回報後返回選單，不影響其他測試。
- SD 未插卡或 init 失敗：回報錯誤，不自動重試。

## microSD 卡前置條件

- 必須事先格式化為 FAT32；Arduino `SD` library 不支援 exFAT。
- 建議 ≤32 GB 卡；64 GB 以上需以第三方工具強制 FAT32。

## 記錄與驗證等級

- 全程保留 serial log；將刷新耗時、上傳速度結果、異常事件記錄至 `docs/device-research.md`（附量測方法）。
- 驗證等級：有硬體。依序執行並記錄：build → upload → serial log → full refresh → partial refresh → sleep/wake，外加按鍵、microSD、Wi-Fi 掃描。
- 回報時明確標示實際執行的驗證，不得宣稱未執行項目。

## 未定事項

- platform／framework 的確切 pinned 版本：首次編譯成功後填入並記錄。
