#pragma once
#include <Arduino.h>

struct WatchItem {
  const char* code;   // API c 欄位
  const char* name;   // 直式版面顯示名
};

// 順序＝顯示順序（加權指數第一列）；v1 程式內固定（spec）
static const WatchItem WATCHLIST[] = {
  {"t00",    "加權指數"},
  {"2330",   "台積電"},
  {"2317",   "鴻海"},
  {"0050",   "元大台灣50"},
  {"006208", "富邦台50"},
};
static const int WATCH_N = 5;

// ex_ch 參數（與 WATCHLIST 同集合）
static const char* QUOTE_EX_CH =
  "tse_t00.tw|tse_2330.tw|tse_2317.tw|tse_0050.tw|tse_006208.tw";
