# ESP32 E-Paper 裝置文件設計

日期：2026-08-19

## 目標

為 ELECROW CrowPanel ESP32 5.79 吋電子紙裝置建立一套可直接支援後續開發的文件。文件以繁體中文為主，技術名詞、命令與程式識別字保留英文。

這次只建立文件，不初始化韌體專案，也不宣稱完成建置、燒錄或實機顯示測試。

## 標準開發方向

- 主要工具鏈：PlatformIO。
- 顯示器函式庫：GxEPD2 1.5.9 以上版本的 `GxEPD2_579_GDEY0579T93`。
- 官方 Arduino driver：用於核對腳位、控制流程、原廠範例及硬體初步驗證，不作為正式應用程式的預設基礎。
- 目標板：`ESP32-S3-WROOM-1-N8R8`，8 MB Flash、8 MB OPI PSRAM。

## 文件結構

### `README.md`

README 是專案入口，內容保持可快速掃讀：

- 裝置與專案定位。
- 目前專案狀態。
- 已確認的核心規格。
- 推薦工具鏈與理由。
- 專案建立後使用的 PlatformIO 命令。
- 首次接上裝置、燒錄與上傳失敗時的基本處理方式。
- 電池、觸控、雙 SSD1683 與刷新速度等重要警告。
- 詳細研究文件與代理人規則的連結。
- 預計採用的原始碼與測試目錄配置；必須標明這是後續規劃，不能冒充已存在的檔案。

README 不重複完整腳位表、電路分析與所有來源。

### `docs/device-research.md`

研究文件保存可追溯的技術資料：

- 商品與硬體版本識別。
- 完整規格、實體介面及按鍵。
- 顯示器、microSD、UART、按鍵與擴充 GPIO 腳位。
- 雙 SSD1683 的分區方式、800x272 內部 framebuffer、792x272 圖片大小與接縫 padding。
- 全畫面、快速及部分刷新的行為、限制與建議策略。
- USB-C、CH340C、下載模式、PlatformIO/Arduino 設定。
- USB 與電池供電、充電器、電池保護及 deep sleep 的限制。
- 官方 Arduino 範例、原廠韌體還原方式。
- GxEPD2、ESPHome、MicroPython 與圖片轉換工具的支援狀態。
- 官方 repository 的已知程式問題與維護狀況。
- 推薦的開發及實機驗證順序。
- 官方與社群來源連結。

官方資料與社群經驗分開標示。沒有量測或正式規格支持的功耗、充電電流與刷新週期不得寫成保證值。

### `AGENTS.md`

AGENTS 只包含代理人能直接遵循的工作規則：

- 繼承上層 `@RTK.md`。
- 文件使用繁體中文；程式碼、命令與識別字使用英文。
- 以 PlatformIO 和 GxEPD2 為預設，不引入第二套正式工具鏈。
- 固定保存硬體腳位與 8 MB Flash、OPI PSRAM 設定。
- 不得假設裝置有觸控、原生 USB、電池電量 ADC 或電池保護。
- 不得把 792x272 圖片直接當作官方 800x272 內部 framebuffer。
- 規範顯示初始化、刷新、BUSY 等待、休眠及 GPIO7 斷電順序。
- 修改顯示流程時須考慮 ghosting、previous RAM 和週期性全刷新。
- Wi-Fi 憑證與 API key 不得提交到版本庫。
- 依賴版本要固定，新增依賴前先確認必要性。
- 沒有裝置時只能驗證編譯；不能聲稱完成燒錄或實機顯示驗證。
- 有裝置時按編譯、上傳、serial log、全刷新、局部刷新、休眠的順序記錄測試結果。

## 資料來源

主要依據如下：

- ELECROW 官方商品頁與 Wiki。
- ELECROW 官方 GitHub repository、原廠範例、電路圖與 factory firmware。
- SSD1683 與 ESP32-S3-WROOM-1 文件。
- GxEPD2 的 `GxEPD2_579_GDEY0579T93` 實作。
- ESPBoards ESPHome external component。
- MicroPython 社群 driver 與實機專案僅作補充，不覆蓋官方硬體資料。

每個主要外部來源都要提供可點擊 URL。社群資訊應附來源並明確標為實作經驗或推測。

## 一致性要求

三份文件中的下列資料必須一致：

- 解析度為 792x272；官方 driver 的內部 framebuffer 為 800x272。
- MCU 為 ESP32-S3-WROOM-1-N8R8。
- Flash 與 PSRAM 均為 8 MB，PSRAM 類型為 OPI。
- 顯示器腳位為 MOSI 11、SCK 12、CS 45、DC 46、RESET 47、BUSY 48、電源控制 7。
- USB-C 經 CH340C，不是 ESP32-S3 原生 USB 連線。
- 裝置沒有觸控。
- 電池為單節 3.7 V 鋰電池；不得假設主板提供完整電芯保護。

## 驗證方式

完成正式文件後執行以下檢查：

1. 確認三份文件都存在，Markdown 標題層級合理。
2. 檢查 README 的相對連結可以指向研究文件與 AGENTS。
3. 比對三份文件中的規格與腳位。
4. 掃描 `TODO`、`TBD`、佔位符與未解釋的空白章節。
5. 檢查外部 URL 格式與主要來源可存取性。
6. 確認命令都標明適用前提，沒有暗示目前已有 PlatformIO 工程。
7. 確認沒有寫入 Wi-Fi、API key 或其他憑證。

因工作目錄目前不是 Git repository，不執行 commit。因這次沒有韌體工程，不執行 `pio run`。
