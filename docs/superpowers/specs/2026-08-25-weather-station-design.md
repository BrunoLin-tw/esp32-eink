# 天氣看板設計

- 日期：2026-08-25
- 狀態：已獲使用者核准（英文＋數字＋自製圖示；模組化多檔；EXT1 撥桿喚醒；失敗短睡眠重試）
- 上位文件：`docs/device-research.md`（硬體依據）、`AGENTS.md`（作業規則）、`docs/app-ideas.md`（候選背景）
- 前置：bring-up 驗證已完成並合併至 master（量測見 device-research.md「實機 bring-up 量測」）

## 目標

在 792x272 黑白電子紙上顯示多地點天氣看板：定時 deep sleep 喚醒更新，
撥桿可切換地點與手動刷新，為後續電池供電的常駐應用建立完整鏈路
（Wi-Fi → HTTP/JSON → 渲染 → 刷新節奏 → 電源管理）。

## 範圍

- 本期做：Open-Meteo 現況＋未來 6 小時預報、四地點切換（NVS 記憶）、
  NTP 對時與當地時間顯示、自製天氣圖示、30 分鐘睡眠週期、按鍵互動、
  離線錯誤畫面與短睡眠重試。
- 明確不做：中文顯示字型（首版英文＋數字）、SD 卡功能、電池／電流量測、
  OTA、多日預報、任何憑證寫入原始碼或 log。

## 地點表（內建）

| 名稱 | 緯度 | 經度 |
| --- | --- | --- |
| Banqiao（預設） | 25.0133 | 121.4619 |
| Dali, Taichung | 24.1016 | 120.6825 |
| Sapporo | 43.0618 | 141.3545 |
| San Francisco | 37.7749 | -122.4194 |

選擇結果存 NVS；開機沿用上次地點。awake 模式下撥桿上／下循環選擇、
下壓確認並立即刷新。

## 主流程（狀態機）

```
喚醒(timer 30min 或 EXT1 撥桿下壓)
  → GPIO7 拉高 → 顯示 init（首次開機做 clean full refresh）
  → Wi-Fi 連線（timeout 15 s）→ NTP 對時
  → Open-Meteo 抓取（NVS 記住的地點）
  → 渲染 + full refresh
  → [若為按鍵喚醒：進入 awake 模式，等待操作]
  → hibernate → GPIO7 低 → Wi-Fi off → deep sleep
```

- timer 喚醒路徑：刷新後直接睡 30 分鐘。
- 按鍵喚醒路徑：進 awake 模式，畫面保留資料並於角落提示；
  撥桿上／下即時循環顯示所選地點名（不抓資料），下壓確認後抓該地點
  資料並全刷；20 秒無操作自動離開 awake 模式進入 sleep。
- awake 模式中的等待必須讓出執行權（delay/yield），不得無限阻斷。

## 資料來源

- API：`https://api.open-meteo.com/v1/forecast`，一次 GET 帶：
  `latitude, longitude, current=temperature_2m,weather_code,wind_speed_10m,
  relative_humidity_2m, hourly=temperature_2m,precipitation_probability,
  weather_code, timezone=auto, forecast_days=1`
  （`timezone=auto` 使回應含 `utc_offset_seconds`，用於當地時間換算。）
- 解析：ArduinoJson v7；取 current 四項與 hourly 中「下一個整點起 6 格」
  的溫度、降雨率、天氣碼。
- NTP：`configTime(0,0,"pool.ntp.org")` 取 UTC，再以 utc_offset_seconds
  換算當地日期時間。

## 版面（792x272）

```
┌──────────────────────────────────────────────────────────┐
│ [Location name]        ┌────────┐                        │
│ YYYY-MM-DD HH:MM       │ icon   │   28.4°C               │
│ (當地時間)              │ 64x64  │   H:31° L:25°          │
│                        └────────┘   Rain 60%  Wind 12km/h│
├──────────────────────────────────────────────────────────┤
│ [icon]14:00 29° [icon]15:00 30° [icon]16:00 ... （6 格）  │
└──────────────────────────────────────────────────────────┘
```

- 圖示（最終決策，2026-08-25）：PROGMEM 1bpp 點陣，64px 與 32px 兩套，
  由 `tools/gen_icons.py`（Pillow）產生至 `src/icons_bitmaps.h`；
  依 WMO weather_code 映射：晴(0)、多雲(1-2)、陰(3)、霧(45,48)、
  毛雨(51-57)、雨(61-67)、雪(71-77)、陣雨(80-82)、雪陣雨(85-86)、
  雷雨(95,96,99)，共 10 種。調整樣式須改產生器腳本後重跑。
- 文字（最終決策，2026-08-25）：U8g2 比例字型
  （`U8g2_for_Adafruit_GFX@1.8.0`），經基線座標繪製於 GFX 畫布。
  配置：地點 `helvB24_tf`、日期時間 `helvR18_tf`、大字溫度
  `logisoso62_tn`（純數字）＋°C 以 `helvR18_tf` 接續繪製、
  RH/Wind 列與離線畫面說明 `helvR14_tf`、六格時間 `helvR14_tf`
  ／數值 `helvB14_tf`、awake 提示條 `helvR12_tf`。
- 刷新策略：每次喚醒更新走 full refresh（實測 4415 ms 可接受，
  30 分鐘才一次）；awake 模式中的地點名提示用 partial refresh。
  注意：GxEPD2 的 partial 視窗狀態會殘留，渲染整頁前必須先
  `setFullWindow()`。

## 憑證

- commit `src/secrets.h.example`（含 SSID/PASSWORD 佔位說明）。
- 使用者本地建立 `src/secrets.h` 填入實際值；檔案已在 `.gitignore`。
- `main.cpp` 以 `#include "secrets.h"` 引用；檔案不存在時編譯失敗，
  錯誤訊息指引複製範本。
- serial log 不得輸出密碼；SSID 屬環境資訊亦應避免輸出。

## 錯誤處理

- Wi-Fi 連線逾時（15 s）或 HTTP 失敗：顯示錯誤畫面（保留上次成功
  資料若有、加 "OFFLINE - retry in 5 min"），deep sleep 改為 5 分鐘。
- JSON 解析失敗或欄位缺漏：同上，serial 記錄原因碼。
- 所有等待（Wi-Fi、HTTP、BUSY）帶 timeout；BUSY 由 GxEPD2 內建
  10 s 逾時處理。

## 檔案結構

| 檔案 | 職責 |
| --- | --- |
| `src/main.cpp` | setup/loop、狀態機、sleep/wake、NVS 讀寫 |
| `src/locations.h` | 地點表（名稱、經緯度） |
| `src/weather.h/.cpp` | Wi-Fi 連線、NTP、Open-Meteo GET、JSON 解析成結構體 |
| `src/ui.h/.cpp` | 版面渲染、WMO 碼→圖示映射、錯誤畫面 |
| `src/icons.h` | 圖示 PROGMEM 位元陣列 |
| `src/secrets.h.example` | 憑證範本 |

依賴新增：`bblanchon/ArduinoJson@v7.x`（platformio.ini 固定版本，
首次編譯成功後核對記錄）。

## 驗證計畫（有硬體，逐步檢查點）

1. build → upload → serial：secrets 引入與 Wi-Fi 連線成功
2. Open-Meteo 抓取與解析正確（serial 印出數值人工核對）
3. 版面渲染：實際畫面比對設計版面
4. 地點切換：awake 模式撥桿操作、NVS 記憶（斷電重開驗證）
5. 失敗路徑：改錯密碼觸發 OFFLINE 畫面與 5 分短睡眠
6. 完整週期：timer 30 分自動喚醒刷新；全程 serial log 存查
7. 收尾：耗時與行為記錄至 device-research.md

## 已定事項補記

- ArduinoJson pinned 版本（首次編譯成功後核對）：`bblanchon/ArduinoJson@7.4.3`。
- U8g2 字型引擎 pinned 版本：`olikraus/U8g2_for_Adafruit_GFX@1.8.0`。
- 圖示方案決策歷程：
  1. 原案：PROGMEM 手寫點陣（本文件初版）。
  2. 計畫修訂：改幾何繪製（省 flash、免轉檔工具）。
  3. 實作回饋：幾何繪製品質不足，最終採 **`tools/gen_icons.py`
     產生之 PROGMEM 點陣**（64px＋32px 兩套），commit `d36c103`。
  最終狀態以本文件「版面」章節與 `src/icons_bitmaps.h` 為準；
  產生器腳本入庫，樣式調整一律經由腳本重跑。
