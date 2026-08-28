# SD 相框／圖片播放器設計

- 日期：2026-08-26（修訂二版：整併格式契約、排序清單方案、卡鍵防護、
  掃描過濾契約、converter CLI、雙級性能門檻、驗證矩陣 23 項）
- 狀態：已獲使用者核准（含全部審查意見之定案）
- 上位文件：`docs/device-research.md`（硬體依據）、`AGENTS.md`（作業規則）
- 分支策略（方案 2 快照式）：master 標籤 `weather-v1` 保存天氣看板；
  本應用在 `feature/photo-frame` 分支開發，完成後合併回 master 正式取代；
  跑天氣看板隨時 `git checkout weather-v1`

## 目標

瀏覽 microSD 卡 `/raw_photos/*.raw` 的黑白圖片：撥桿手動翻頁，
可設定間隔的自動輪播；顯示後即深睡，發揮電子紙不耗電維持畫面的特性。

## 範圍

- 本期做：host 端 RAW 轉檔工具、SD 掃描排序與瀏覽、三鍵喚醒分流、
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
  version／flags 欄位區分，不得更動既有欄位語意。**未知 version 或
  未知 flags 一律拒絕，不得降級解碼。**

## 轉檔工具（`tools/raw_convert.py`）

比照 `gen_icons.py` 慣例（Pillow、venv 環境）：

- 輸入 JPG/PNG → 等比縮放 → Floyd–Steinberg 抖動 → 依契約輸出 `.raw`。
- `--mode contain`（預設）：完整放入畫面置中留白。
- `--mode cover`：等比放大蓋滿後中央裁切。
- 批次轉檔**不遞迴**；輸入副檔名大小寫不敏感（jpg/jpeg/png 等）。
- 輸出名稱與輸入 basename 同名（`.raw`）；**預設拒絕覆寫既有 .raw，
  需 `--force` 才覆寫**；同 basename 碰撞報錯。
- EXIF orientation transpose 正規化；RGBA 透明像素合成白底。
- 檔名建議數字前綴（`001_xxx.raw`）控制排序。

## 掃描與排序（photo_store）

- 掃描 `/raw_photos/` 單層：**只收一般檔案（不含目錄）、不遞迴、
  副檔名大小寫不敏感等於 `.raw`（接受 `.raw`／`.RAW`）、跳過點開頭
  隱藏 metadata 檔（macOS 可能寫入）。**
- 檔名 >64 bytes：**跳過並 warning（不截斷，避免碰撞）**。
- 候選數 >128 張：**顯示 `TOO MANY PHOTOS` 錯誤畫面**（不接受「前 128
  張」的歧義子集，請使用者整理卡片）。
- **保存排序後檔名清單**：static `char g_names[128][64]`（最壞 8 KiB，
  記憶體帳目見下），以 byte-wise ASCII sort 排序；掃描後所有
  idx／壞檔搜尋／NVS 一律以這張穩定表為準。排序基準為 SD library
  實際回傳之名稱（8.3 或 LFN 下數字前綴排序皆穩定）。
- 檔名清單在每次喚醒時重建（換卡即重新掃描排序，語意一致）。

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
- 喚醒後進入任何互動迴圈前，先等待三鍵全部釋放（timeout 2 s）。
- **卡鍵防護**：2 s 內未全釋放 → 記錄 warning；**仍執行一次由 wake
  status 決定的原始動作**；進入深睡前若 wake pin 仍 low，**本輪不
  啟用 EXT1**，改用 5 分鐘 timer-only 睡眠，下輪再試（避免立即
  喚醒迴圈與高耗電）。
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
開機 → 顯示器 init → SD 上電(GPIO42) → 掃描/排序（重建檔名清單）
  ├─ SD init 失敗／資料夾不存在／零 RAW／全部無效 →
  │     全屏提示（NO PHOTOS / NO VALID PHOTOS / TOO MANY PHOTOS）
  │     → 深睡：本輪停用 timer，只掛三鍵 EXT1（NVS slide 不清零）
  └─ 正常 → 依 wake cause 計算 idx → 驗頭驗長 → 讀入 buffer
        → 渲染（白底＋黑色 drawBitmap）→ SD cleanup → GPIO42 斷電
        → hibernate → 控制線 hold → 深睡
設定選單（PRESS 進入）：
  - 全屏文字頁：輪播間隔 OFF/1/5/15/30 分
  - 撥桿上下循環選項、下壓確認存 NVS；
    （實作註記：游標更新改 **整頁重繪（full refresh）**，因 dual-controller
    partial window 座標對齊風險；每次游標移動 ≈4.4 s，選單往返成本
    由 2 次內容 full refresh 增為「2＋游標移動次數」次）
    idle 20 s 逾時：保存當前游標選項，語意等同 PRESS 確認
  - slide 僅在值與原值不同時寫入
  - 退出後：從 SD 重讀原圖重繪（內容已被選單覆蓋）→ 深睡
  - 成本：選單往返＝2 次內容 full refresh（選單＋照片還原）；
    每次喚醒另有開機 clean refresh（獨立計入喚醒預算，不併入此數）
```

- 毀檔搜尋：**只向前搜**，上限＝檔案總數（最多檢查一圈）；仍無有效
  檔則顯示 `NO VALID PHOTOS`。`idx` **僅在顯示成功後**寫入 NVS。
- **性能雙級門檻**：性能目標＝典型卡、少量照片時 ≤6 s；
  **驗收上限 ≤8 s**。分段記錄 boot／mount／scan+sort／read／render
  ／total，慢卡不誤判失敗。

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
- 另含排序檔名清單 `char g_names[128][64]`（最壞 8 KiB）。
- 記憶體帳目：GxEPD2 全高 page buffer ≈26.9 KB＋RAW buffer 26.9 KB＋
  檔名清單 8 KB ≈62 KB；相框無 Wi-Fi，餘裕充足。
- 不做 streaming 分頁讀取（GxEPD2 全窗更新 page loop 可能跑兩輪，
  靜態 buffer 使單次讀檔即可服務所有輪次）。
- 生命週期：完整驗證（magic、version==1、**flags==0**、w/h、
  reserved==0、檔案大小==26,940）後讀入 → 才交給 UI；**SD 卸載嚴格
  在 `nextPage()` 迴圈結束後**。
- PSRAM 配置列為日後選項（需另測 GFX 對 PSRAM 位址讀取效能與失敗
  處理），首版不採用。

## NVS（namespace `photo`）

| key | 型別 | 語意 |
| --- | --- | --- |
| `idx` | u32 | 上次**顯示成功**的排序位置；載入時 clamp 到實際張數內 |
| `slide` | u32 | 輪播間隔秒數；0＝關閉 |

- `idx` **只保存排序位置**：換卡或檔案集合變動後，僅保證索引合法，
  **不保證指向同一張照片**；首版不保存 filename hash／card identity。
- 寫入時機：僅在值實際變更時寫入（降低 flash 磨損）；`idx` 僅於
  顯示成功後寫入；`slide` 僅於選單確認值與原值不同時寫入。

## 錯誤處理與 SD cleanup

- **cleanup 語意**（播放器唯讀，無需 fsync/flush）：關閉 RAW file
  handle → 關閉 directory handle → `SD.end()` → GPIO42 斷電。
  所有路徑（成功、掛載失敗、讀檔失敗、全毀檔、選單退出、讀取中拔卡）
  最後 **GPIO42 必須為 low**。
- RAW 驗頭失敗（magic/version/flags/w/h/reserved/檔案大小任一不符）：
  依「毀檔搜尋」規則向前跳過。
- SD init 失敗不自動重試；所有等待帶 timeout（BUSY 由 GxEPD2 內建
  10 s）。
- SD 讀取期間拔卡：handles 清理、GPIO42 low、不 crash（驗證矩陣
  覆蓋）。

## 檔案結構（本分支）

| 檔案 | 職責 |
| --- | --- |
| `src/main.cpp` | 狀態機、三鍵喚醒分流、卡鍵防護、睡眠/輪播 timer、NVS |
| `src/photo_store.h/.cpp` | SD 掛載與 GPIO42 電源、掃描過濾、排序清單、驗頭、讀入 logical bitmap buffer、cleanup |
| `src/ui.h/.cpp` | U8g2 字型渲染核心、相框版面（全幅圖＋選單/提示畫面）、深睡 hold 流程 |
| `tools/raw_convert.py` | host 端轉檔工具 |
| 刪除 | `weather.*`、`icons*`、`locations.h`（天氣專屬，隨分支取代） |

保留 `log.h`；`secrets.h.example` 本應用無 Wi-Fi 不再需要，一併刪除
（`weather-v1` 標籤中仍有完整天氣版可考）。

## 驗證矩陣（有硬體，逐步檢查點）

| # | 項目 | 預期結果 |
| --- | --- | --- |
| 1 | 工具驗證：漸層、格線、標字樣本＋純白、純黑反相測試圖，contain/cover 各一 | 抖動品質正常、無整張反相、檔頭與總長正確 |
| 2 | 首次掃描與首張顯示 | 排序清單（serial）與畫面一致 |
| 3 | 雙向翻頁含兩端循環邊界 | idx 循環正確 |
| 4 | 選單設定間隔＋NVS 跨 reset | 設定保留、退出後原圖重繪 |
| 5 | 輪播自動推進（1 分鐘檔實測） | timer 喚醒、下一張顯示 |
| 6 | 無卡路徑 | NO PHOTOS 提示＋深睡（無 timer loop） |
| 7 | 毀檔跳過（中段壞檔） | 跳至下一有效檔、NVS 不寫壞索引 |
| 8 | **≥20 次連續 sleep→wake→sleep** | 三鍵交替喚醒無殘留事件、無 Busy Timeout |
| 9 | EN-reset 冷開機 | 索引 clamp、正常顯示 |
| 10 | header 各欄位錯誤（version≠1／flags≠0／reserved≠0／尺寸不符／short file／oversized file） | 逐項跳過該檔或錯誤畫面，不 crash |
| 11 | 全部 RAW 損壞 | 只搜尋一圈、顯示 NO VALID PHOTOS |
| 12 | 129 張候選 | TOO MANY PHOTOS 錯誤畫面 |
| 13 | 檔名大小寫與字典序（001/002/010） | 排序正確（serial 清單核對） |
| 14 | PRESS＋UP＋DOWN 同時喚醒 | 依優先序 PRESS > UP > DOWN |
| 15 | 按鍵持續按住 >2 s（stuck-low） | warning＋執行原始動作一次＋timer-only 睡眠，無喚醒迴圈 |
| 16 | 輪播已開啟但無卡／無照片 | 本輪不掛 timer，只留三鍵 EXT1 |
| 17 | Timer 關閉時 | 確認喚醒源只剩 EXT1 三鍵 |
| 18 | SD 讀取期間拔卡 | handles 清理、GPIO42 low、不 crash |
| 19 | 所有錯誤路徑結尾 | GPIO42 確實為 low |
| 20 | contain 透明 PNG | 透明區合成白底、無黑塊 |
| 21 | EXIF 旋轉 JPEG | transpose 後方向正確 |
| 22 | 輸出碰撞（同名已存在） | 預設拒絕、--force 才覆寫 |
| 23 | 收尾 | 分段耗時（boot/mount/scan+sort/read/render/total）記錄至 device-research.md |

## 未定事項

- 無。依賴版本皆沿用現有 pinned 清單，本應用不新增函式庫。
