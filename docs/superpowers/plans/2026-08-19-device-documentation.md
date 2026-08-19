# ESP32 E-Paper Device Documentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立繁體中文的專案入口、完整裝置研究文件與代理人操作規則，讓後續 PlatformIO + GxEPD2 開發有一致且可追溯的技術基礎。

**Architecture:** `README.md` 只保留開發者第一次進入專案需要的資訊；`docs/device-research.md` 保存完整硬體研究與來源；`AGENTS.md` 將研究結論轉換成代理人可執行的限制和驗證規則。三份文件共享固定規格，但避免大段重複內容。

**Tech Stack:** Markdown、PlatformIO、Arduino framework、ESP32-S3、GxEPD2 1.5.9+。

---

## File map

- Create: `README.md`，專案入口、快速開始與文件索引。
- Create: `docs/device-research.md`，硬體、韌體、生態系、限制與來源的完整研究記錄。
- Create: `AGENTS.md`，代理人的開發、安全與驗證規則。
- Existing: `docs/superpowers/specs/2026-08-19-device-documentation-design.md`，已批准的文件設計。
- Existing: `docs/superpowers/plans/2026-08-19-device-documentation.md`，本實作計畫。

### Task 1: Write the detailed device research

**Files:**
- Create: `docs/device-research.md`
- Reference: `docs/superpowers/specs/2026-08-19-device-documentation-design.md`

- [ ] **Step 1: Create the document skeleton**

建立下列章節，順序固定：

```markdown
# ELECROW CrowPanel ESP32 5.79 吋電子紙裝置研究

## 摘要
## 型號與資料可信度
## 硬體規格
## 外觀、按鍵與介面
## 腳位定義
## 顯示器架構
## 刷新模式與殘影
## 電源、電池與低功耗
## USB、燒錄與開發環境
## 官方軟體資源
## GxEPD2 開發建議
## ESPHome 與 MicroPython
## 圖片資料格式與轉換
## 已知問題
## 建議驗證順序
## 參考資料
```

- [ ] **Step 2: Fill in confirmed hardware facts**

硬體資料必須包含：

- 型號 `DIS08792E`、硬體版本 V1.0。
- `ESP32-S3-WROOM-1-N8R8`、8 MB Flash、8 MB OPI PSRAM。
- 792x272 黑白面板、有效區 139.00x47.74 mm、兩顆 SSD1683。
- microSD、UART0、2x10 GPIO、SH1.0-2P 電池接頭、CH340C。
- 無觸控、無背光、畫面斷電保留。
- 顯示、按鍵、microSD、UART、LED 與擴充 GPIO 的完整腳位表。

- [ ] **Step 3: Explain the display data path**

明確區分下列尺寸：

```text
Visible image:       792 x 272 = 26,928 bytes at 1 bpp
Elecrow framebuffer: 800 x 272 = 27,200 bytes at 1 bpp
Controller split:    two SSD1683 controllers with seam padding
```

說明官方 `EPD_ShowPicture()` 會處理接縫 padding，而官方 `EPD_Display()` 預期 800x272 buffer。GxEPD2 使用 `GxEPD2_579_GDEY0579T93` 封裝雙控制器處理。

- [ ] **Step 4: Document refresh and power behavior**

記錄 full、fast、partial update 的官方控制值，並將 GxEPD2 的 2.2 秒全刷新與 450 ms 部分刷新標為函式庫名目值。週期性全刷新、每 5 至 30 次部分刷新一次的策略必須標為實務起點，不得寫成面板保證。

電池章節必須說明單節 3.7 V LiPo/Li-ion、接頭極性需量測、主板不可假設有完整電芯保護、沒有電池電壓 ADC，以及 GPIO7 控制顯示器接地回路。低功耗流程需按刷新完成、等待 BUSY、hibernate、GPIO7 拉低、停用網路、ESP32 deep sleep 的順序描述。

- [ ] **Step 5: Document software support and known issues**

列出 12 個官方 Arduino sketches、factory firmware 還原位址、Arduino/PlatformIO 設定、CH340C 上傳方式、BOOT/RESET 手動下載模式。說明官方沒有可用的 ESP-IDF 或 MicroPython 專案，ESPHome 目前依賴 external component。

已知問題至少包含：官方 partial-refresh issue、filled rectangle off-by-one、BUSY 無 timeout、官方 4 MB/8 MB Flash 設定矛盾、Windows-only Image2Lcd 流程及 repository 維護度偏低。

- [ ] **Step 6: Add traceable sources**

至少提供 ELECROW 商品頁、Wiki、Arduino tutorial、官方 GitHub repository、GxEPD2 driver、ESPHome external component、MicroPython 官方 ESP32-S3 firmware 頁與相關 community driver URL。每項社群資料要標明不是官方保證。

- [ ] **Step 7: Inspect the finished research document**

Run:

```bash
rg -n '^#{1,3} |792x272|800x272|SSD1683|GPIO7|CH340C|GxEPD2_579_GDEY0579T93' docs/device-research.md
```

Expected: 所有規定章節與固定技術名詞都有結果；`792x272` 和 `800x272` 的用途沒有混用。

### Task 2: Write the project README

**Files:**
- Create: `README.md`
- Reference: `docs/device-research.md`

- [ ] **Step 1: Create a concise project entry point**

使用下列章節：

```markdown
# ESP32 E-Paper

## 裝置
## 開發方向
## 專案狀態
## 快速開始
## 常用命令
## 開發時先知道
## 預計目錄結構
## 文件
## 資料來源
```

- [ ] **Step 2: Add the recommended development path**

README 必須將 PlatformIO + Arduino framework + GxEPD2 1.5.9+ 定為標準路徑，面板 class 寫成 `GxEPD2_579_GDEY0579T93`。說明目前只完成文件，尚未建立 `platformio.ini` 或韌體程式。

- [ ] **Step 3: Add future commands without implying they already work**

使用以下命令，前文加上「PlatformIO 專案建立後」：

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

補充第一次上傳可從 460800 開始，失敗時降至 115200，並提供 BOOT/RESET 手動下載模式步驟。

- [ ] **Step 4: Add warnings and links**

簡短提示無觸控、雙 SSD1683、刷新速度、ghosting、電池保護及 CH340C。加入以下相對連結：

```markdown
- [完整裝置研究](docs/device-research.md)
- [代理人開發規則](AGENTS.md)
- [文件設計規格](docs/superpowers/specs/2026-08-19-device-documentation-design.md)
```

- [ ] **Step 5: Verify README scope**

Run:

```bash
wc -l README.md
rg -n '尚未建立|PlatformIO|GxEPD2_579_GDEY0579T93|docs/device-research.md|AGENTS.md' README.md
```

Expected: README 明確說明專案狀態，包含標準工具鏈與兩個主要文件連結；內容比研究文件短。

### Task 3: Write agent instructions

**Files:**
- Create: `AGENTS.md`
- Reference: `/home/brunolin/AGENTS.md`
- Reference: `/home/brunolin/RTK.md`
- Reference: `docs/device-research.md`

- [ ] **Step 1: Inherit the parent instruction**

文件第一個有效規則保留：

```markdown
@RTK.md
```

接著說明這份規則適用於 `/home/brunolin/projects/esp32-eink` 全目錄。

- [ ] **Step 2: Add project and language rules**

規定繁體中文文件、英文程式識別字，並將 PlatformIO、Arduino framework、GxEPD2 1.5.9+ 設為預設。代理人在修改前要先閱讀 `README.md` 與 `docs/device-research.md`。

- [ ] **Step 3: Add immutable hardware assumptions**

列出 8 MB Flash、8 MB OPI PSRAM、固定顯示腳位、CH340C、無觸控、無 battery ADC，以及 792x272/800x272 的差異。禁止未經電路圖和實機驗證便改動固定腳位。

- [ ] **Step 4: Add coding, safety, and secrets rules**

規定顯示刷新前後的 BUSY、hibernate 和 GPIO7 次序；部分刷新須管理 previous RAM 與週期性全刷新。禁止提交 Wi-Fi 密碼、API keys、私鑰或裝置專屬 token。電池文件與程式不得假設反接、過放或短路保護存在。

- [ ] **Step 5: Add verification levels**

定義兩級驗證：

```text
Without hardware: dependency resolution and pio run only.
With hardware: build, upload, serial log, full refresh, partial refresh, sleep/wake.
```

代理人只能報告實際執行過的層級；沒有實機時不得宣稱顯示、按鍵、電池、Wi-Fi 或 deep sleep 已通過。

- [ ] **Step 6: Inspect agent instructions**

Run:

```bash
rg -n '@RTK.md|PlatformIO|GxEPD2|GPIO7|BUSY|792x272|800x272|Wi-Fi|API key|實機' AGENTS.md
```

Expected: 上層規則、工具鏈、硬體限制、安全和驗證界線都有明確條文。

### Task 4: Cross-document verification

**Files:**
- Verify: `README.md`
- Verify: `AGENTS.md`
- Verify: `docs/device-research.md`

- [ ] **Step 1: Verify required files and relative links**

Run:

```bash
test -f README.md && test -f AGENTS.md && test -f docs/device-research.md && test -f docs/superpowers/specs/2026-08-19-device-documentation-design.md
```

Expected: exit code 0 with no output.

- [ ] **Step 2: Compare fixed specifications**

Run:

```bash
rg -n 'ESP32-S3-WROOM-1-N8R8|8 MB|OPI PSRAM|792x272|800x272|MOSI.*11|SCK.*12|CS.*45|DC.*46|RESET.*47|BUSY.*48|GPIO7|CH340C|觸控' README.md AGENTS.md docs/device-research.md
```

Expected: README 可只列核心資料；AGENTS 與研究文件中的數字不得矛盾。

- [ ] **Step 3: Scan for unfinished content**

Run:

```bash
rg -n 'TBD|TODO|PLACEHOLDER|待補|稍後填寫' README.md AGENTS.md docs/device-research.md
```

Expected: no matches and exit code 1.

- [ ] **Step 4: Check Markdown links and source URLs manually**

確認 README 的三個相對連結都指向現有檔案。抽查 ELECROW 商品頁、Wiki、GitHub repository、GxEPD2 driver 與 ESPHome component URL 可存取，redirect 可接受。

- [ ] **Step 5: Review changes**

Run:

```bash
ls -la README.md AGENTS.md docs/device-research.md docs/superpowers/specs/2026-08-19-device-documentation-design.md docs/superpowers/plans/2026-08-19-device-documentation.md
```

Expected: 五份文件存在且大小大於 0。工作目錄目前不是 Git repository，因此不執行 `git diff` 或 commit。
