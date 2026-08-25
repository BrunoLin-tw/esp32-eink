# 後續應用開發候選

記錄於 bring-up 驗證完成後（2026-08-24）。排序依上手速度與依賴複雜度；
每個應用啟動前仍須走 brainstorming → spec → plan 流程。

## 1. 天氣看板（已選定，進行中）

- 資料源：Open-Meteo（免費、免 API key、JSON 小），NTP 對時顯示時間。
- 節奏：喚醒 → 連 Wi-Fi → 抓資料 → 全刷一次 → deep sleep 30–60 分鐘。
- 入選原因：無憑證問題、渲染只需文字＋簡單圖示、直接套用已驗證的
  sleep 流程；可完整練到 Wi-Fi＋JSON＋刷新節奏＋電源管理鏈路。

## 2. SD 相框／圖片播放器

- 從 SD 卡讀 BMP／raw 圖檔輪播，撥桿換頁。
- 不碰網路與 JSON，練「host 端圖片轉 1bpp framebuffer → 顯示」管線。
- 技術上最簡，但需另寫 host 端轉檔步驟。

## 3. 報價看板

- 利用工作區既有台股 Shioaji 環境做每日收盤看板。
- 資料鏈路最長（本機 API 橋接或公開 API），建議作為第二或第三個應用。

## 共同設計注意事項（來自實機量測）

- full refresh 實測 4415 ms，partial 單次約 664 ms；更新節奏設計時以此為準。
- 每 5–30 次 partial 插入一次 full refresh（AGENTS.md）。
- 電池尚未接入；接入前須量測 SH1.0 極性並在 device-research.md 記錄方法。
