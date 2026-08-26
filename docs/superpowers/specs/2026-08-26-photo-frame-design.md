# SD 相框／圖片播放器設計

- 日期：2026-08-26
- 狀態：已獲使用者核准（RAW 預轉路線、手動＋輪播並存、雙模式轉檔工具、
  `/raw_photos/` 單層、間隔 NVS 可調、精簡模組化、按鍵語意表）
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
  圖片上任何 OSD 文字覆蓋。

## RAW 格式與轉檔工具

- 檔頭 12 bytes：magic `"EPFR"`（4B）+ width u16LE(792) + height u16LE(272)。
- 內容：792x272 1bpp packed，row-major、每 byte MSB 在前
  （相容 Adafruit GFX `drawBitmap`），26,928 bytes；全檔 26,940 bytes。
- 新工具 `tools/raw_convert.py`（比照 `gen_icons.py` 慣例，Pillow）：
  JPG/PNG → 等比縮放 → Floyd–Steinberg 抖動 → `.raw`。
  - `--mode contain`（預設）：完整放入畫面置中留白
  - `--mode cover`：等比放大蓋滿後中央裁切
  - 支援整批目錄轉檔；尺寸固定 792x272
- 檔名建議數字前綴（`001_xxx.raw`）控制排序；韌體只做字典序。

## 按鍵語意

| 按鍵 | GPIO | 行為 |
| --- | --- | --- |
| 撥桿上 | 6 | 直接翻上一張（循環） |
| 撥桿下 | 4 | 直接翻下一張（循環） |
| 撥桿下壓 | 5 | 開啟設定選單（輪播間隔） |
| MENU／EXIT | 2／1 | 本版不使用 |

deep sleep 喚醒源為 UP/DOWN/PRESS 三鍵（EXT1 any-low），睡眠期間
三腳以 rtc_gpio 維持內部拉高；喚醒後以 `esp_sleep_get_ext1_wakeup_status()`
判別按鍵，決定翻頁方向或進選單。深睡前控制線固定 LOW＋hold、
開機時解除 hold——沿用天氣看板已驗證的流程（見 device-research.md
「深睡後白色區域暗灰條紋」節）。

## 主流程（狀態機）

```
開機 → 顯示器 init → SD 上電(GPIO42) → 掃描 /raw_photos/*.raw 字典序
  ├─ 無卡／資料夾不存在／零檔案 → 全屏 "NO PHOTOS" 提示 → 深睡（按鍵重試）
  └─ 正常 → 顯示 NVS 記住的索引（clamp 至實際張數）→ SD flush/卸載
        → GPIO42 斷電 → hibernate → 控制線 hold → 深睡
喚醒分流：
  UP/DOWN → idx±1（循環）→ 讀檔顯示 → 深睡
  PRESS   → 顯示當前張 → 選單：輪播間隔 OFF/1/5/15/30 分
            （撥桿上下循環、下壓確認存 NVS；idle 20 s 逾時視同確認）
  timer(N 分，僅輪播開啟時掛載) → 自動顯示下一張 → 深睡
```

翻頁耗時目標：喚醒→SD 讀檔→full refresh 完成約 2–4 秒（full refresh
實測約 4.4 s 為主要成本，屬電子紙特性）。

## 版面

- 全幅圖片：`drawBitmap(0, 0, ...)`，無 OSD。
- 設定選單與提示畫面使用 U8g2 比例字型（沿用 `helvB24_tf`／`helvR14_tf`
  等既有配置；介面語言維持英文數字慣例）。

## NVS（namespace `photo`）

| key | 型別 | 語意 |
| --- | --- | --- |
| `idx` | u32 | 上次顯示索引；載入時 clamp 到實際張數內（應對換卡） |
| `slide` | u32 | 輪播間隔秒數；0＝關閉 |

寫入時機：僅在值實際變更時寫入（沿用可靠性修補結論，降低 flash 磨損）。

## 錯誤處理

- RAW 檔頭不符（magic 或尺寸錯誤）：跳過該檔續走下一張；
  全部檔案皆無效則顯示錯誤畫面。
- SD init 失敗不自動重試；所有等待帶 timeout（BUSY 由 GxEPD2 內建 10 s）。
- 檔案數可能因換卡變動：索引一律 clamp 後使用。

## 檔案結構（本分支）

| 檔案 | 職責 |
| --- | --- |
| `src/main.cpp` | 狀態機、三鍵喚醒分流、睡眠/輪播 timer、NVS |
| `src/photo_store.h/.cpp` | SD 掛載與 GPIO42 電源、掃描排序、讀 RAW 驗頭入 framebuffer |
| `src/ui.h/.cpp` | U8g2 字型渲染核心、相框版面（全幅圖＋選單/提示畫面）、深睡 hold 流程 |
| `tools/raw_convert.py` | host 端轉檔工具 |
| 刪除 | `weather.*`、`icons*`、`locations.h`（天氣專屬，隨分支取代） |

保留 `log.h`；`secrets.h.example` 本應用無 Wi-Fi 不再需要，一併刪除
（`weather-v1` 標籤中仍有完整天氣版可考）。

## 驗證計畫（有硬體，逐步檢查點）

1. 工具驗證：產生測試組（漸層、格線、標字樣本），contain/cover 各一，
   確認抖動品質與檔頭正確
2. 首次掃描與首張顯示；serial 印出檔案清單與索引
3. 雙向翻頁含循環邊界（最後一張 → 第一張）
4. 選單設定間隔＋NVS 跨 reset 記憶
5. 輪播自動推進（以 1 分鐘檔實測）
6. 無卡路徑（拔卡喚醒 → NO PHOTOS）
7. 毀檔跳過（放入壞檔觀察跳至下一張）
8. 收尾：翻頁與 SD 掛載耗時記錄至 device-research.md

## 未定事項

- 無。依賴版本皆沿用現有 pinned 清單，本應用不新增函式庫。
