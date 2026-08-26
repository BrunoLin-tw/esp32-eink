# SD 相框／圖片播放器設計

- 日期：2026-08-26（修訂版：整併 3 項格式缺陷修正＋15 項實作前提建議）
- 狀態：已獲使用者核准（RAW 預轉路線、手動＋輪播並存、雙模式轉檔工具、
  `/raw_photos/` 單層、間隔 NVS 可調、精簡模組化、按鍵語意表、
  header/polarity/buffer 契約、本修訂版全部建議項目）
- 上位文件：`docs/device-research.md`（硬體依據）、`AGENTS.md`（作業規則）
- 分支策略（方案 2 快照式）：master 標籤 `weather-v1` 保存天氣看板；
  本應用在 `feature/photo-frame` 分支開發，完成後合併回 master 正式取代；
  跑天氣看板隨時 `git checkout weather-v1`

## 目標

瀏覽 microSD 卡 `/raw_photos/*.raw` 的黑白圖片：撥桿手動翻頁，
可設定間隔的自動輪播；顯示後即深睡，發揮電子紙不耗電維持畫面的特性。

## 範圍

- 本期做：host 端 RAW 轉檔工具、SD 掃描與排序瀏覽、三鍵喚醒分流、
  輪播間隔設定（NVS）、無卡／毀檔處理。
- 明確不做：子目錄、playlist 檔、裝置端圖片解碼、Wi-Fi、旋轉縮放、
  圖片上任何 OSD 文字覆蓋、partial 浮動選單（列為日後優化）。

## 前置條件

- microSD 卡必須格式化為 **FAT32**；SD library 不支援 exFAT
  （沿用 bring-up 實測結論）。建議 ≤32 GB。

## RAW 格式契約

### 檔頭（12 bytes，正式定版）

| Offset | 大小 | 欄位 | 首版值 |
| --- | --- | --- | --- |
| 0 | 4 | magic `"EPFR"` | — |
| 4 | 1 | format version | `1` |
| 5 | 1 | flags | `0` |
| 6 | 2 | width u16LE | `792` |
| 8 | 2 | height u16LE | `272` |
| 10 | 2 | reserved | 必為 `0` |

### 像素契約

- **bit 1＝黑，bit 0＝白**。
- 內容：792x272 1bpp packed，row-major、每 byte MSB 在前，
  每列 99 bytes（792÷8 整除），**無 row padding**。
- payload 26,928 bytes；全檔恆為 26,940 bytes。
- 渲染順序固定：`fillScreen(WHITE)` → `drawBitmap(..., GxEPD_BLACK)`
  （GFX 語意：設定位元以指定色繪製、未設定保持背景，兩者搭配下
  bit 1 即為黑）。
- contain 模式留白填充值為 `0x00`（白）。
- 轉檔工具：Floyd–Steinberg 抖動後，亮度 <128 之像素設 bit（黑）；
  不得多餘反相。
- 格式演進：日後 polarity 反轉、壓縮或新 pixel format 一律以
  version／flags 欄位區分，不得更動既有欄位語意。

## 轉檔工具（`tools/raw_convert.py`）

比照 `gen_icons.py` 慣例（Pillow、venv 環境）：

- 輸入 JPG/PNG → 等比縮放 → Floyd–Steinberg 抖動 → 依契約輸出 `.raw`。
- `--mode contain`（預設）：完整放入畫面置中留白。
- `--mode cover`：等比放大蓋滿後中央裁切。
- 支援整批目錄轉檔；尺寸固定 792x272。
- 檔名建議數字前綴（`001_xxx.raw`）控制排序；韌體只做字典序。

## 按鍵語意與喚醒分流

| 按鍵 | GPIO | 行為 |
| --- | --- | --- |
| 撥桿上 | 6 | 直接翻上一張（循環） |
| 撥桿下 | 4 | 直接翻下一張（循環） |
| 撥桿下壓 | 5 | 開啟設定選單（輪播間隔） |
| MENU／EXIT | 2／1 | 本版不使用 |

- deep sleep 喚醒源為 UP/DOWN/PRESS 三鍵（EXT1 any-low），睡眠期間
  三腳以 rtc_gpio 維持內部拉高。
- **分流依據 `esp_sleep_get_ext1_wakeup_status()` 暫存器**，而非喚醒後
  的電位讀值。
- **多鍵同時喚醒優先序**：PRESS > UP > DOWN。
- 喚醒後進入任何互動迴圈前，先等待三鍵全部釋放（timeout 2 s），
  避免喚醒瞬間的殘留按壓被當成新事件。
- 深睡前控制線固定 LOW＋hold、開機時解除 hold——沿用天氣看板已驗證
  流程（見 device-research.md「深睡後白色區域暗灰條紋」節）。

### wake cause → 索引轉換（精確規則，全部 mod n）

| wake cause | 索引運算 | 後續動作 |
| --- | --- | --- |
| UP | idx−1 | 讀檔顯示 → 深睡 |
| DOWN | idx+1 | 讀檔顯示 → 深睡 |
| PRESS | idx 不變 | 直接進設定選單（**不先重刷同一張照片**） |
| timer（輪播開啟時） | idx+1 | 讀檔顯示 → 深睡 |
| power-on／其他 reset | NVS idx（clamp 至張數內） | 讀檔顯示 → 深睡 |

## 主流程（狀態機）

```
開機 → 顯示器 init → SD 上電(GPIO42) → 掃描 /raw_photos/*.raw 字典序
  ├─ 無卡／資料夾不存在／零檔案 → 全屏 "NO PHOTOS" 提示 → 深睡（按鍵重試）
  └─ 正常 → 依 wake cause 計算 idx → 驗頭驗長 → 讀入 buffer
        → 渲染（白底＋黑色 drawBitmap）→ SD flush/卸載 → GPIO42 斷電
        → hibernate → 控制線 hold → 深睡
設定選單（PRESS 進入）：
  - 全屏文字頁：輪播間隔 OFF/1/5/15/30 分
  - 撥桿上下循環選項（partial 快速更新選項列）、下壓確認存 NVS；
    idle 20 s 視同確認退出
  - 退出後：從 SD 重讀原圖重繪（內容已被選單覆蓋）→ 深睡
  - 誠實成本：進一次設定約 3 次 full refresh（≈13 s），屬低頻操作可接受
```

- 翻頁耗時目標：**≤6 s**（full refresh 實測 4.4 s 為主要成本＋SD 與
  啟動開銷；輪播週期同理）。
- 毀檔搜尋：**只向前搜**，上限＝檔案總數（最多檢查一圈）；仍無效檔
  則顯示錯誤畫面。`idx` **僅在顯示成功後**寫入 NVS。

## 版面

- 全幅圖片：`drawBitmap(0, 0, ...)`，無 OSD。
- 設定選單與提示畫面使用 U8g2 比例字型（沿用 `helvB24_tf`／`helvR14_tf`
  等既有配置；介面語言維持英文數字慣例）。

## 記憶體策略（buffer 所有權）

- `photo_store` 擁有 **static 26,928-byte logical bitmap buffer**
  （檔案層級 `.bss`、內部 RAM）。**禁止**：
  - 配置於函式 stack（Arduino loop task 堆疊僅約 8 KB）；
  - 使用 27,200-byte 官方 800x272 framebuffer 格式（AGENTS framebuffer
    規則）。
- 記憶體帳目：GxEPD2 全高 page buffer 約 26.9 KB＋RAW buffer 26.9 KB
  ≈54 KB；相框無 Wi-Fi，餘裕充足。
- 不做 streaming 分頁讀取（GxEPD2 全窗更新 page loop 可能跑兩輪，
  靜態 buffer 使單次讀檔即可服務所有輪次）。
- 生命週期：完整驗證（magic、version==1、w/h、reserved==0、
  檔案大小==26,940）後讀入 → 才交給 UI；**SD 卸載嚴格在 `nextPage()`
  迴圈結束後**。
- PSRAM 配置列為日後選項（需另測 GFX 對 PSRAM 位址讀取效能與失敗
  處理），首版不採用。

## 檔案數與路徑上限

- `MAX_FILES = 128`；掃描時僅計數＋翻頁時逐項走目錄至第 N 筆
  （免存檔名清單，單次走訪 <100 ms 量級）。
- 檔名長度上限 64 bytes（含延伸字元）。
- ESP32 SD library 對長檔名的支援可能退回 8.3 短名——數字前綴命名
  使排序在 8.3 與 LFN 下皆穩定；韌體不顯示檔名，不影響 UX。

## NVS（namespace `photo`）

| key | 型別 | 語意 |
| --- | --- | --- |
| `idx` | u32 | 上次**顯示成功**的索引；載入時 clamp 到實際張數內（應對換卡） |
| `slide` | u32 | 輪播間隔秒數；0＝關閉 |

寫入時機：僅在值實際變更時寫入（降低 flash 磨損）；`idx` 僅於顯示
成功後寫入。

## 錯誤處理與 SD cleanup 路徑清單

- 所有路徑（成功、掛載失敗、讀檔失敗、全毀檔、選單退出）必須執行
  SD cleanup（flush→`SD.end()`→GPIO42 斷電），不得漏卸載。
- RAW 驗頭失敗（magic/version/w/h/reserved/檔案大小任一不符）：
  依「毀檔搜尋」規則向前跳過。
- SD init 失敗不自動重試；所有等待帶 timeout（BUSY 由 GxEPD2 內建
  10 s）。
- 檔案數可能因換卡變動：索引一律 clamp 後使用。

## 檔案結構（本分支）

| 檔案 | 職責 |
| --- | --- |
| `src/main.cpp` | 狀態機、三鍵喚醒分流、睡眠/輪播 timer、NVS |
| `src/photo_store.h/.cpp` | SD 掛載與 GPIO42 電源、掃描排序、驗頭、讀入 logical bitmap buffer、cleanup |
| `src/ui.h/.cpp` | U8g2 字型渲染核心、相框版面（全幅圖＋選單/提示畫面）、深睡 hold 流程 |
| `tools/raw_convert.py` | host 端轉檔工具 |
| 刪除 | `weather.*`、`icons*`、`locations.h`（天氣專屬，隨分支取代） |

保留 `log.h`；`secrets.h.example` 本應用無 Wi-Fi 不再需要，一併刪除
（`weather-v1` 標籤中仍有完整天氣版可考）。

## 驗證矩陣（有硬體，逐步檢查點）

| # | 項目 | 預期結果 |
| --- | --- | --- |
| 1 | 工具驗證：漸層、格線、標字樣本＋**純白、純黑反相測試圖**，contain/cover 各一 | 抖動品質正常、無整張反相、檔頭與總長正確 |
| 2 | 首次掃描與首張顯示 | 檔案清單（serial）與畫面一致 |
| 3 | 雙向翻頁含兩端循環邊界 | idx 循環正確 |
| 4 | 選單設定間隔＋NVS 跨 reset | 設定保留、退出後原圖重繪 |
| 5 | 輪播自動推進（1 分鐘檔實測） | timer 喚醒、下一張顯示 |
| 6 | 無卡路徑 | NO PHOTOS 提示＋深睡 |
| 7 | 毀檔跳過（中段壞檔） | 跳至下一有效檔、NVS 不寫壞索引 |
| 8 | **連續快速 sleep→wake→sleep** | 三鍵交替喚醒無殘留事件、無 Busy Timeout |
| 9 | EN-reset 冷開機 | 索引 clamp、正常顯示 |
| 10 | 收尾 | 翻頁與 SD 掛載耗時記錄至 device-research.md |

## 未定事項

- 無。依賴版本皆沿用現有 pinned 清單，本應用不新增函式庫。
