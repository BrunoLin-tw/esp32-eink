# 報價看板設計規格（Quote Board v1）

- 日期：2026-08-29（修訂二版：併入規格審查 P0-1~4 與 R1~5）
- 狀態：已核准（brainstorming 完成、規格審查修訂完成）
- 前置：天氣看板（`weather-v1`，Wi-Fi/NTP/JSON/深睡管線已驗證）、SD 相框（`photo-frame-v1`，現行 master，互動/NVS 紀律已驗證）

## 目的

直立安裝的 792x272 電子紙看板，顯示加權指數與自選股報價。情境 C：
**盤中即時更新（每 5 分鐘邊界對齊）＋收盤定格（含時間戳）**。可隨時按鍵立即更新。

## 硬體與不可變約束（見 `docs/device-research.md`）

- 面板 `GxEPD2_579_GDEY0579T93`，實體 792x272；**本應用以直立安裝（實體轉 90°）**，邏輯版面 272x792。
- 按鍵（active-low）：MENU GPIO2、EXIT GPIO1、rocker up GPIO6、down GPIO4、press GPIO5。
- `PWR` LED GPIO41；UART0 GPIO44/43。顯示器電源／接地控制 GPIO7（hibernate 時拉低）。
- 無電池 ADC、無背光、無原生 USB。電池尚未接入（USB 供電）。

## 資料源

- 端點：`https://mis.twse.com.tw/stock/api/getStockInfo.jsp`
- 單次請求帶全部標的：`ex_ch=tse_2330.tw|tse_2317.tw|tse_0050.tw|tse_006208.tw|tse_t00.tw&json=1`
  - 加權指數代號 `tse_t00.tw`（`n`＝「發行量加權股價指數」）。
- 使用欄位（已實測 2026-08-29）：`c` 代碼、`n` 名稱、`z` 最新價、`y` 昨收、`t` 成交時間、`d` 報價日期。
- 漲跌價＝`z - y`；漲跌%＝`(z-y)/y*100`。
- API 無正式 SLA；回應含 `userDelay:5000` 節流提示（5 分鐘節奏遠低於限制，無礙）。

### HTTP/TLS 契約

- **TLS**：`WiFiClientSecure::setCACertBundle(esp_crt_bundle_get())`——core 2.0.17
  sdkconfig 已啟用 `MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`（Mozilla 全量根庫，
  含 TWCA Global Root CA；實測鏈：葉→TWCA SSL Sub-CA→TWCA Global Root CA，
  葉憑證 2026-11-28 到期，不釘葉）。
  - 備案：若實機 handshake 失敗，改釘 **TWCA Global Root CA** PEM（self-signed，
    2030-12-31 到期，PEM 入版本控管 `src/twse_root_ca.h`）。
  - **`setInsecure()` 禁用於 v1**。
- connect timeout 10 s；整體 timeout 15 s。
- User-Agent：`esp32-eink-quote/1.0`。
- response 上限 32 KB（超出視為失敗）。
- 非 HTTP 200、或 JSON 結構無效 → 走失敗路徑（保留快取＋重試）。

### 欄位有效性（all-or-nothing）

對回應批次驗證，**任一列無效則整批視為失敗**（保留快取，禁止轉 0 渲染）：

1. 5 個預期代碼（2330/2317/0050/006208/t00）各出現恰好一次（`c` 比對）。
2. `z`、`y` 為有限數字且非 `-`／空值（盤中未成交可能出現）。
3. `y != 0`。
4. `d` 為 8 位數日期（YYYYMMDD）；`t` 為 `HH:MM:SS` 格式。
5. 名稱 `n` 非空。

- **備案**：若公開 API 長期不穩，經使用者同意後切換為「本機 Shioaji 橋接」（方案 A）——本規格不含其實作。

## 版面（已確認：直式 A3）

邏輯座標 272x792（直立），寫入實體 792x272 framebuffer。

```
┌────────────────────────────┐
│ 08-28 週五          更新 13:33 │   header（16px CJK/Latin）
├────────────────────────────┤
│ 加權指數（28px CJK 加粗）       │
│ 46,331.45（40px 數字）         │
│ ▲ +356.23  +0.77%（22px）     │
├────────────────────────────┤
│ 台積電（20px）                │
│ 2,420.00                    │
│ ▲ +10.00  +0.41%            │
│（…鴻海、元大台灣50、富邦台50…）     │
└────────────────────────────┘
```

- 5 列：加權指數＋4 檔自選股（2330 台積電、2317 鴻海、0050 元大台灣50、006208 富邦台50）——**v1 程式內固定**，改清單需重刷（watchlist 編輯列為 v2 候選）。
- 每列三段：名稱（加權指數 28px，個股 20px）、現價（40px）、漲跌行（22px：▲/▼＋漲跌價＋%）。
- 漲跌符號 ▲▼ 用 `drawTriangle` 手繪；上漲▲、下跌▼。
- header 時間語意（**無歧義版**）：成功抓取同時保留 `quoteTime`（5 列中最新有效 `t`）
  與 `fetchedEpoch`（抓取當下本地時間）。
  - 盤中成功 → 顯示 `quoteTime`（HH:MM）。
  - 收盤定格成功 → 顯示 `fetchedEpoch`（HH:MM）。
  - 快取畫面（抓取失敗）→ 顯示快取 `savedEpoch`（HH:MM）＋「更新失敗」。
- header 交易日：快取/成功回應之 `d` 轉 `MM-DD 週X`。

### 繪製層規則（旋轉）

- **唯一 portrait 繪製層＝U8g2**：`setDisplayRotation()` 對其**所有**圖元生效
  （文字、`drawTriangle`、`drawLine`、`drawBox`）；一律以邏輯 272x792 座標繪製。
- **禁止**混用 GxEPD2/Adafruit_GFX 原始 `display.draw*()` 於邏輯座標。
- 旋轉方向（`U8G2_R1`／`U8G2_R3`）於實機以「header 位於頂部、文字正立」確認。

## 字型策略（本次新增工具鏈，已獲同意）

U8g2 內建中文字型僅 16px；20px／28px 名稱需自製**子集**字型：

- `tools/gen_fonts.py`（PIL 渲染 → BDF → u8g2 `bdfconv` → `.c`）。
- **可重現性契約**：
  - 固定記錄字型來源檔／版本／授權（Noto Sans CJK Bold，SIL OFL 1.0）與
    `bdfconv` 版本於工具內註解與 spec。
  - **完整 glyph manifest** 明列於工具內（每個字元逐一列出），不做隱式掃描。
  - **產出 `.c` 提交進 repo**——建置不依賴字型工具鏈；僅 glyph 變更時重跑。
- 尺寸：20px（個股名稱）與 28px（加權指數）各一組；glyph：
  加權指數、台積電、鴻海、元大台灣50、富邦台50、週五、更新、失敗、
  一二三四五（+ spec 修訂時補充之 manifest）。
- 數字／符號（0-9, `,` `.` `+` `-` `%`）用 U8g2 內建 Latin 字型（40px 現價、22px 漲跌）。
- header 16px：U8g2 unifont CJK 子集（含 ASCII）或併入 gen_fonts 16px 組——
  實作時以 glyph 覆蓋範圍擇一（manifest 決定）。

## 架構與檔案

沿用相框骨架（`log.h`、`ui.*`、`main.cpp` 狀態機），新增/更動：

| 檔案 | 內容 |
| --- | --- |
| `src/main.cpp` | 狀態機：喚醒分流、市場狀態判定、睡眠排程、MENU 立即更新 |
| `src/quote_store.h/.cpp` | Wi-Fi 連線、HTTPS 抓取（含 TLS 契約）、JSON 解析與欄位驗證、漲跌計算、NVS blob |
| `src/ui.h/.cpp` | 直式版面渲染（僅經 U8g2 繪製層）、header、狀態字 |
| `src/watchlist.h` | 標的清單常數（代碼＋`ex_ch`＋中文名） |
| `src/secrets.h.example` | Wi-Fi 憑證範本（`secrets.h` 本機檔、gitignored） |
| `src/twse_root_ca.h` | 備案用釘選 CA（僅於 bundle 失敗時啟用） |
| `tools/gen_fonts.py` | 中文子集字型產生器（manifest＋可重現性契約） |
| `platformio.ini` | **重新引入 `ArduinoJson@7.4.3`**（固定版，其餘版本不動） |

- **刪除**：`photo_store.*`（相框專屬；分支標籤 `photo-frame-v1` 已保存）、`tools/raw_convert.py`。
- 渲染採 GxEPD2 full window（此面板 partial 有對齊風險，沿用相框決策）；每次更新即一次 full refresh（實測約 4.4 s）。

## 狀態機與睡眠排程

市場狀態由**本地時間（NTP 同步 epoch，UTC+8）**判定，規則表：

| 狀態 | 條件（週一~五 / 六日） | 本輪動作 | 睡眠目標 |
| --- | --- | --- | --- |
| `PRE_MARKET` | 平日 00:00–08:59 | 不抓取（MENU 除外） | **當日 09:00** |
| `TRADING` | 平日 09:00–13:30 | 抓取＋渲染 | 下一個 5 分邊界（R1） |
| `POST_CLOSE` | 平日 13:30 後 | 收盤定格邏輯（見下） | 次交易日 09:00 |
| `WEEKEND` | 六日 | 不抓取（MENU 除外） | 下週一 09:00 |

- **假日判定僅限 `TRADING` 時段**：本次成功、五列完整有效之回應 `d != today`
  → 今日休市 → 睡到**隔日 09:00**。快取顯示與失敗路徑**永不**觸發假日判定。
- **收盤定格**：`POST_CLOSE` 且 NVS `lastCloseDate != today` → 抓取一次；
  成功且 `d == today` → 寫入 `lastCloseDate = today`、渲染定格；
  失敗 → 5 分後重試（不掛長睡眠）。已定格（`lastCloseDate == today`）→ 直接長睡眠。
- **MENU（GPIO2）＝立即更新（例外路徑）**：任何狀態按下 → 立即抓取渲染
  （失敗則顯示快取＋「更新失敗」）→ 依**現狀態**回歸該排程
  （`TRADING` 回下一 5 分邊界；`PRE_MARKET` 睡回當日 09:00；餘類推）。
- **5 分邊界對齊（R1）**：`TRADING` 睡眠時長＝`next = floor(epoch/300)*300 + 300 - now`；
  若單輪工作後已越過邊界則取再下一邊界；**最小安全等待 30 s**（不足則順延一個邊界）。
- **按鍵卡住防護（R4）**：醒來先解除控制線 hold；`waitButtonsReleased` 等待放開；
  2 秒 stuck-low 偵測 → 該輪 **5 分鐘 timer-only**（**停用 EXT1**），防按住 MENU 立即喚醒迴圈。
- 睡眠一律「timer＋EXT1（MENU）」雙源（stuck 時 timer-only）；深睡前
  `uiHibernate` → GPIO7 拉低 → 控制線 hold；喚醒反向重新初始化（沿用相框
  已驗證流程與零遮罩 EXT1 清除，見 `docs/device-research.md`）。
- 時間基準：NTP 同步之 epoch（RTC 於深睡中維持）；所有「睡到某時點」的時長以
  epoch 差值計算；**單段睡眠上限 24 小時，超過則分段**。
- 失敗路徑：抓取失敗但無有效快取 → 錯誤畫面 → 5 分鐘重試（不掛長睡眠）。
- 等待 BUSY 有 timeout 並讓出執行權；禁止無限阻斷等待。

## NVS（單一 versioned blob，僅變更時寫入）

- 單一 key `quote:rec`（一次 `Preferences::putBytes()`）：`{version, quotes[5],
  quoteTime, lastCloseDate, savedEpoch}`。**禁止**拆多 key（避免掉電時不一致）。
- `quotes[5]`：每列 {code, price z, prevClose y, time t}。
- 寫入時機：成功抓取且（任一報價欄位變更 **或** `lastCloseDate` 變更）才寫。
- 載入驗證：version 符合、長度正確、欄位 sanity（數字有效、日期格式）→
  失敗視為無有效快取。
- 快取與假日判定隔離：**快取內容僅供顯示**，`d`/`lastCloseDate` 之假日判定
  只允許在「本次成功抓取且五列完整」條件下使用（見狀態機）。

## 非目標（v1 不做）

- watchlist 選單編輯、指數以外商品（櫃買/期貨）。
- 五檔、開高低、成交量、K 線圖、觸發警示。
- 盤中 partial 更新（維持 full refresh）。
- 跨日曆表維護（假日靠 `TRADING` 時段回傳日期偵測）。

## 驗證矩陣

host 端（不需硬體）：
1. `gen_fonts.py` 產出字型含 manifest 全部 glyph、尺寸正確（20/28px）；產出 `.c` 入 repo。
2. 漲跌計算（正/負/零、整數與小數）單元測試。
3. JSON 解析＋欄位有效性（R2）：缺列/重複代碼、`z`/`y` 為 `-` 或空、`y==0`、
   `d`/`t` 格式錯誤 → 整批失敗。
4. **旋轉 golden test**：邏輯四角落座標映射、▲▼三角形方向、文字 baseline、
   框線完整性、272x792 邊界裁切——全部經唯一 U8g2 繪製層驗證。
5. 睡眠排程單元測試：5 分邊界對齊（R1，含最小 30 s、越界跳下一邊界）、
   `PRE_MARKET`→當日 09:00、`POST_CLOSE`→次交易日、六日→週一 09:00、
   24 小時分段。
6. NVS blob：版本/長度/sanity 驗證、單次 putBytes、僅變更時寫入。
7. `pio run` 編譯 SUCCESS。

硬體（逐項記錄結果與時間戳）：
8. 開機：Wi-Fi 連線＋NTP 對時成功。
9. TLS handshake 經 bundle 成功（若失敗→備案釘選 CA）。
10. 直立顯示方向正確（header 在頂、文字正立；golden test 四角落對應）。
11. 中文名稱 20px/28px 渲染正確無缺字。
12. 報價數值與證交所網頁一致（2330/2317/0050/006208/指數）。
13. 漲跌▲▼與正負符號正確。
14. header 時間語意：盤中 `quoteTime`／定格 `fetchedEpoch`／快取 `savedEpoch`＋「更新失敗」。
15. MENU 按下→立即更新→依現狀態回歸排程。
16. 盤中 5 分邊界自動更新（2 個以上週期，檢查喚醒 timestamp 對齊 :00/:05 邊界）。
17. 收盤定格＋`lastCloseDate` 寫入＋時間戳正確。
18. `PRE_MARKET` 不抓取、假日（`d != today`）跳過（可改系統時間模擬）。
19. 抓取失敗保留快取＋「更新失敗」標示＋5 分重試。
20. 卡鍵防護：按住 MENU 開機→stuck 偵測→timer-only 5 分→釋放後恢復 EXT1。
21. 20+ 次連續 sleep/wake 無異常（無 Busy Timeout）。
22. 收盤後按 MENU 仍可立即更新並回歸長睡眠。

## 里程碑

- 完成後：fast-forward 合併 master、標籤 `quote-v1`（`photo-frame-v1` 保存現行相框）。
- 實作依 `docs/superpowers/plans/2026-08-29-quote-board.md` 執行（由 writing-plans
  產出，實作開始前必存在；前向參照）。