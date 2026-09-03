# 報價看板多頁設計規格（Quote Board v2）

- 日期：2026-09-03
- 狀態：設計已核准，待書面規格審閱
- 基礎版本：`quote-v1`（commit `6fed8c8`）
- 關聯規格：`docs/superpowers/specs/2026-08-29-quote-board-design.md`

## 文件優先順序

本規格擴充 Quote Board v1。多頁版面、按鍵喚醒、9 檔資料驗證、NVS layout
及字型內容以本規格為準；市場時段、13:35 收盤定格、HTTPS/TLS、full refresh、
顯示旋轉、GPIO7 電源流程及其他未提及行為沿用 v1 規格。

不可變硬體約束仍以 `docs/device-research.md` 及專案 `AGENTS.md` 為準。本規格不
變更任何腳位、面板 class、framebuffer 契約或電池規則。

## 目的

報價看板從「加權指數＋4 檔股票」擴充為「加權指數＋8 檔股票」。畫面維持
272x792 直式版面，每頁顯示加權指數與 4 檔股票，共兩頁。使用者可用搖桿
UP／DOWN 翻頁；定時更新後保留原頁，不需反覆切回第一頁。

本版仍採程式內固定 watchlist。修改清單後要重新編譯與燒錄。

## 固定清單與分頁

資料陣列順序固定如下：

| 資料索引 | 頁面 | 代號 | 顯示名稱 | Exchange |
| ---: | ---: | --- | --- | --- |
| 0 | 兩頁固定 | `t00` | 加權指數 | `tse` |
| 1 | 1 | `2330` | 台積電 | `tse` |
| 2 | 1 | `2317` | 鴻海 | `tse` |
| 3 | 1 | `0050` | 元大台灣50 | `tse` |
| 4 | 1 | `006208` | 富邦台50 | `tse` |
| 5 | 2 | `1513` | 中興電 | `tse` |
| 6 | 2 | `2412` | 中華電 | `tse` |
| 7 | 2 | `2881` | 富邦金 | `tse` |
| 8 | 2 | `2002` | 中鋼 | `tse` |

程式須集中定義並交叉檢查以下常數，不得在資料邏輯、UI 或測試中另寫裸數字
`5`／`9` 當作陣列大小：

```text
QUOTE_TOTAL = 9
STOCK_TOTAL = 8
PAGE_COUNT = 2
STOCKS_PER_PAGE = 4
PAGE_ROWS = 5
```

所有 watchlist 項目目前都是 TWSE 上市商品，URL 使用 `tse_` 前綴。支援 TPEx
不是本版目標。

## 版面

### 每頁內容

每頁沿用 v1 的 5 列 A3 版面：

```text
┌────────────────────────────┐
│ 09-03 週四    1/2 更新 09:30 │
├────────────────────────────┤
│ 加權指數                    │
│ 46,331.45                  │
│ ▲ +356.23  +0.77%          │
├────────────────────────────┤
│ 個股 1／5                  │
│ ...                        │
├────────────────────────────┤
│ 個股 2／6                  │
│ ...                        │
├────────────────────────────┤
│ 個股 3／7                  │
│ ...                        │
├────────────────────────────┤
│ 個股 4／8                  │
│ ...                        │
└────────────────────────────┘
```

- 加權指數固定在兩頁第一列。
- 第 1 頁顯示資料索引 1 至 4；第 2 頁顯示索引 5 至 8。
- Header 左側日期格式不變。右側改為 `<頁碼>/<總頁數> 更新 HH:MM`，靠右對齊；
  本版只會出現 `1/2` 或 `2/2`。
- 翻頁沒有抓取新資料，header 使用快取 `savedEpoch`，與既有快取時間語意一致。
- 5 分鐘 timer 或 MENU 抓取成功時，header 時間規則沿用 v1：盤中用
  `quoteTime`，收盤定格用 `fetchedEpoch`。
- 名稱、現價、漲跌、分隔線、row stride、`--`、▲／▼ 與狀態列座標不變。
- 顯示器仍使用 `display.setRotation(3)`、邏輯尺寸 272x792、`setFullWindow()`
  page loop；每次翻頁也是一次 full refresh，實測預期約 4.4 秒。

`QuoteView` 只裝一頁的 5 列，另帶 `pageIndex`、`pageCount` 與每列有效旗標。
UI 不直接讀取 9 列總資料，也不自行決定分頁切片。

### 列狀態

以下兩種情況都在現價與漲跌行顯示 `--`，不畫 ▲／▼：

1. 有效列的 `z="-"`：盤中未成交，屬正常資料。
2. 個股列 `valid=false`：MIS 回應缺列、重複或欄位無效。

只有第二種情況要在該頁底部顯示「部分失敗」。如果失敗列在另一頁，目前頁
不顯示「部分失敗」，翻到包含該列的頁面才顯示。狀態字優先順序如下：

```text
時間未同步 > 更新失敗 > 部分失敗 > 無狀態字
```

這個順序避免抓取或 NTP 失敗時，被舊快取中的個別無效列遮蔽。

## 按鍵與頁面狀態

| 按鍵 | GPIO | 行為 |
| --- | ---: | --- |
| MENU | 2 | 立即抓取全部 9 檔並渲染目前頁；既有排程語意不變 |
| Rocker UP | 6 | 上一頁，從第 1 頁迴繞到第 2 頁 |
| Rocker DOWN | 4 | 下一頁，從第 2 頁迴繞到第 1 頁 |
| EXIT | 1 | 本版不用，不加入喚醒 mask |
| Rocker PRESS | 5 | 本版不用，不加入喚醒 mask |

深睡 EXT1 mask 從 MENU 擴充為 MENU＋UP＋DOWN。三鍵都是 active-low，睡眠期間
須啟用 RTC input 與 pull-up。多鍵同時喚醒時採下列規則：

- MENU 與其他鍵同時出現：MENU 優先，忽略翻頁。
- UP 與 DOWN 同時出現但沒有 MENU：不翻頁，畫面維持原頁。
- 單獨 UP 或 DOWN：執行對應翻頁。

頁碼保存在 `RTC_DATA_ATTR`，跨 deep sleep 保留。非 deep-sleep 啟動（wake cause
不是 TIMER／EXT1）須把頁碼與原睡眠目標初始化，回到第 1 頁。每次使用前也要
把越界頁碼歸零，避免 RTC 記憶體或後續清單變更造成越界。

### 快取翻頁路徑

UP／DOWN 喚醒不連 Wi-Fi、不做 NTP、不呼叫 TWSE API，也不寫 NVS：

1. 解除 GPIO hold，等待按鍵釋放並執行卡鍵檢查。
2. 初始化顯示器。
3. 載入 NVS `QuoteRecord`。
4. 更新 RTC 頁碼並組成該頁的 `QuoteView`。
5. 有快取就 full refresh；無有效快取則用既有英文訊息畫面顯示
   `NO DATA`／`cache unavailable`。
6. hibernate、關閉 GPIO7，睡回原本的排程目標。

若按鍵 2 秒後仍未放開，不執行 MENU 或翻頁動作，該輪改用 300 秒 timer-only；
這是 v1 卡鍵規則 R4 對三個喚醒鍵的延伸。

### 保留原睡眠目標

每次進入 deep sleep 前，RTC 記憶體須保存「未經 24 小時 cap 的最終目標
epoch」。RTC 目標有效的定義是非 0，且與目前 RTC epoch 的差距不超過 7 天。
翻頁完成後不套用 v1 `nextTradingBoundary()` 的 30 秒保護，而採以下規則：

- 有效目標仍在未來：睡到原目標，單段仍受 24 小時上限限制；即使只剩 1 至
  29 秒也不得跳過該目標。
- 有效目標已到或已過：設定 1 秒 timer，讓下一輪走一般 timer 喚醒流程。
- RTC 目標無效：退回 `now + 300`。

30 秒保護只用於一般抓取輪完成後計算新的盤中邊界。翻頁快路徑不重算排程，
因此 09:04:40 完成翻頁時仍會在 09:05 喚醒更新；不會直接跳到 09:10。收盤後
或週末翻頁也會回到原本的長睡目標。

## 資料抓取

一次 HTTPS 請求帶全部 9 個 `ex_ch`：

```text
tse_t00.tw|tse_2330.tw|tse_2317.tw|tse_0050.tw|tse_006208.tw|
tse_1513.tw|tse_2412.tw|tse_2881.tw|tse_2002.tw
```

實際參數不得在原始碼另存第二份清單，仍由 `WATCHLIST` 組裝。組裝介面改為可
回報成功／失敗的 bounded function；目的 buffer 至少 192 bytes，逐次 append
都要檢查截斷。若 buffer 不足，整次抓取在發出 HTTP 前失敗，走既有快取與重試
路徑。HTTP response 32 KB cap、TLS 根 CA、timeout 與 User-Agent 沿用 v1。

## 容錯驗證

### 指數列

`t00` 是整批成功的必要條件，而且必須恰好出現一次。它須符合 v1 的欄位規則：

- `n` 非空。
- `y` 是有限數字、非 `-`、非空且不等於 0。
- `z` 是有限數字，或恰為 `-`；後者記為 `z=0` 並顯示 `--`。
- `d` 是 8 位數 `YYYYMMDD`。
- `t` 是 `HH:MM:SS`。

缺少、重複或欄位無效都使整批失敗。成功批次的 `date` 固定取指數列 `d`；
`d != today` 的休市判定也只看指數列，不能由快取或個股列觸發。

### 個股列

8 檔個股各自驗證。某個預期代碼符合以下全部條件才是 `valid=true`：

- 在 `msgArray` 恰好出現一次。
- `n` 非空。
- `y` 是有限數字、非 `-`、非空且不等於 0。
- `z` 是有限數字，或恰為 `-`。
- `d` 是有效日期且與指數列 `d` 完全相同。
- `t` 是 `HH:MM:SS`。

缺列、重複、欄位無效或日期不同只讓該個股 `valid=false`，不使整批失敗。
回應中未列入 `WATCHLIST` 的代碼直接忽略。有效個股的 `z="-"` 仍是
`valid=true`，記為 `z=0`，不觸發「部分失敗」。

無效個股在記憶體與 NVS 使用固定表示：預期 `code`、`valid=false`、`z=0`、
`y=0`、`t=""`。不得保留該列上一次的價格，避免把舊資料當成這次更新。

`quoteTime` 取與指數同日期、`valid=true` 列中最新的 `t`。指數列一定有效，
所以成功批次至少有一個可用時間。

## NVS 快取

`quote:rec` 仍是一次 `Preferences::putBytes()` 的單一 versioned blob：

```text
QuoteRecord {
  version,
  rows[QUOTE_TOTAL],        // 每列含 code、valid、z、y、t
  quoteDate,
  quoteTime,
  lastCloseDate,
  savedEpoch
}
```

- `BLOB_VERSION` 從 1 遞增為 2。舊 v1 blob 不遷移；首次啟動視為無快取並重新
  連網抓取。
- `recordSane()` 要求 index 0 是有效 `t00`；有效列檢查有限數字、`y != 0`
  與時間格式；無效個股必須符合前述固定表示。
- `recordDiffers()` 逐列比較 `code`、`valid`、`z`、`y`、`t`，再比較
  `quoteDate`／`lastCloseDate`。仍禁止以 `memcmp` 做語意比較。
- `quoteTime`／`savedEpoch` 單獨變更不觸發寫入，沿用 v1 write-on-change 規則。
- blob 結構須以 `static_assert(sizeof(...))` 鎖定；layout 改變時必須再次遞增
  version。另須斷言 persisted struct 是 standard-layout，並鎖定 `alignof` 及
  每個欄位的 `offsetof`；只檢查總大小不足以發現同尺寸欄位重排。
- 翻頁只讀 NVS。頁碼與原睡眠目標放 RTC 記憶體，不增加 flash 寫入。

收盤定格寫入全部 9 列及其 `valid` 狀態。13:30 至 13:34 緩衝、13:35 抓取及
`lastCloseDate` 規則不變；定格後從任一頁翻到另一頁，應看到同一批收盤快照。

## 字型

沿用 `tools/gen_fonts.py` 的固定 manifest 與 Noto Sans CJK Bold。這次修改 manifest
時一併收緊可重現環境：

- Noto Sans CJK Bold 輸入 SHA256 固定為
  `faa5f3656a78b2e2d450d27fe8382c778bc2b6bb5ea29c986664a6a435056ceb`。
- Pillow 固定為 12.3.0；`bdfconv` 固定使用 u8g2 tag 2.37.1。
- 產物 metadata 只記字型 basename、SHA256 與工具版本，不寫本機絕對路徑。
- 輸入 hash 或工具版本不符時直接失敗，不接受悄悄產生不同字形。

20px 個股名稱字型加入第二頁四個名稱，新增加的中文字是：

```text
中 興 華 金 鋼
```

「電」、「富」、「邦」已在 v1 manifest。16px manifest 增加「部」、「分」及
ASCII `/`；前兩字用於「部分失敗」，斜線用於 header `1/2`。

字型產出 `src/fonts_quote.c` 仍提交版本庫；日常 PlatformIO build 不依賴字型
工具鏈。

## 程式邊界

| 檔案 | v2 職責 |
| --- | --- |
| `src/watchlist.h` | 唯一清單、總數／頁數常數、安全 `ex_ch` 組裝 |
| `src/quote_logic.h` | 9 列 parsing／容錯驗證、頁面切片、RTC 目標純邏輯、NVS v2 |
| `src/quote_store.cpp` | 沿用 HTTPS／NVS I/O，處理 `ex_ch` 組裝失敗 |
| `src/main.cpp` | 喚醒分流、RTC page／target、快取翻頁、目前頁渲染 |
| `src/ui.h/.cpp` | 單頁 5 列、`1/2`、每列有效狀態與「部分失敗」 |
| `tools/gen_fonts.py` | 新名稱與狀態字 glyph manifest |
| `tests/host/test_quote_logic.cpp` | 9 列驗證、切頁、RTC target、NVS v2 回歸測試 |

`watchlist.h` 須改為不依賴 `Arduino.h` 或 `ui.h` 的純 C++ header，避免 UI、清單與
quote logic 形成 include cycle，也讓安全 URL 組裝能由 host 直接測試。

頁面切片與目標選擇要寫成不依賴 Arduino／GxEPD2 的純函式，留在 host 測試可
直接編譯的邏輯層。UI 只接收一頁資料，網路層不處理頁碼。

## 失敗處理

- URL 組裝、transport、TLS、HTTP、JSON 或指數列失敗：整次抓取失敗，顯示
  快取＋「更新失敗」，再走 v1 的 5 分重試或原狀態排程。
- 個股列失敗：批次成功並保存該列 `valid=false`；含該列的頁顯示 `--`＋
  「部分失敗」。
- NTP 失敗：不判斷市場狀態，顯示「時間未同步」，5 分短睡；優先級高於快取
  的「部分失敗」。
- 快取翻頁時 NVS 無效：顯示 `NO DATA`／`cache unavailable`。有效 RTC 目標在
  未來時，睡到 `min(target, now + 300)`；有效目標已到或已過時睡 1 秒；RTC
  目標無效時睡 300 秒。這是無快取時唯一規則，優先於一般快取翻頁的睡回原目標。
- 任一喚醒鍵 stuck-low：不執行該鍵動作，300 秒 timer-only。

## 非目標

- 瀏覽器、BLE、microSD 或裝置端 watchlist 編輯。
- 超過 8 檔個股、動態頁數或第三頁。
- TPEx、期貨、選擇權、五檔、成交量、K 線與警示。
- EXIT／PRESS 新功能。
- partial refresh 或快速連續捲動；每次翻頁仍需等待 full refresh。
- 舊 NVS v1 blob 相容或遷移。

## 驗證矩陣

### Host 與靜態驗證

1. 9 個預期代碼完整有效時，解析、計算與 `quoteTime` 正確。
2. `t00` 缺列、重複、`y` 無效、日期／時間無效時整批失敗。
3. 任一個股缺列、重複、空名稱、`y` 無效、非法 `z`、非法 `t` 或日期與指數
   不同時，只有該列 `valid=false`，批次仍成功。
4. 有效個股 `z="-"` 得到 `valid=true`、`z=0`，且不標成部分失敗。
5. 未知代碼被忽略，不改變 9 個預期 slot 的對應關係。
6. 無效個股寫入固定表示；NVS 載入拒絕其他無效表示。
7. 頁面切片：兩頁 index 0 相同；第 1 頁對應 1 至 4，第 2 頁對應 5 至 8。
8. UP／DOWN 迴繞、多鍵優先級與 RTC 頁碼越界歸零。
9. RTC 原目標在未來時保留，含剩餘 1 至 29 秒；已到／已過時睡 1 秒；超出
   正負 7 天或為 0 時睡 300 秒。另測 09:04:40 翻頁不跳過 09:05 更新。
10. 9 檔 `ex_ch` 完整且未截斷；人為縮小 buffer 時安全失敗。
11. 較新 `t` 只出現在無效列時，`quoteTime` 忽略該列；至少退回有效指數時間。
12. 失敗列只在另一頁時，目前頁不顯示「部分失敗」；並測狀態字四級優先序。
13. NVS version 2、`sizeof`／`alignof`／`offsetof` layout 斷言、`recordSane` 與
    write-on-change 回歸。
14. 字型 manifest 含全部新名稱、「部分失敗」及 header `/`；指定字型 hash、
    Pillow 12.3.0 與 u8g2 2.37.1 環境重產兩次，輸出 byte-identical。
15. 既有 quote logic 與 rotation golden tests 全部通過。
16. `pio run` 編譯成功。

### 實機驗證

1. 第 1 頁與第 2 頁方向正確、名稱無缺字，頁碼分別為 `1/2`、`2/2`。
2. 每頁加權指數一致；8 檔個股對應正確，數值與 TWSE 網頁一致。
3. UP／DOWN 從 deep sleep 喚醒，約一次 full refresh 後完成迴繞翻頁；serial
   log 證明沒有 Wi-Fi／NTP／HTTP。
4. 翻頁後睡回原 timer 目標，下一個 5 分鐘邊界沒有被延後。
5. 頁碼跨 deep sleep 與 timer 更新保留；非 deep-sleep 啟動回第 1 頁。
6. 在第 2 頁按 MENU，抓取後仍顯示第 2 頁。
7. 用測試 payload 驗證：正常未成交列顯示 `--` 且無「部分失敗」；個股無效
   顯示 `--`＋「部分失敗」。
8. 按住 MENU、UP、DOWN 個別開機，均進入 300 秒 timer-only；放開後恢復三鍵
   EXT1。
9. 13:30 緩衝與 13:35 定格仍成功；定格後兩頁顯示同一批日期／時間資料。
10. 20 次以上混合 timer／翻頁／MENU sleep-wake，無 Busy Timeout、喚醒迴圈、
    頁碼越界或 NVS 異常。

## 完成條件

Host 測試、`pio run` 與上列實機矩陣通過後，才可宣稱 Quote Board v2 完成。
合併 `master`、建立 `quote-v2` 標籤及 push 均由使用者在實機驗證完成後確認。
