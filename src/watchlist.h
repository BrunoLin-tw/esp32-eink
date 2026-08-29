#pragma once
#include <Arduino.h>
#include "ui.h"

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
static_assert(WATCH_N == QUOTE_ROWS, "watchlist size mismatch");

// ex_ch 參數：由 WATCHLIST 組裝（單一資料源）。
// v1 全數為 TWSE 上市股票→固定 tse_ 前綴＋.tw 後綴；新增 TPEx 標的時須改為逐列指定 ex。
// 回傳內部 static 緩衝（首次呼叫組裝），呼叫端僅讀取；非執行緒安全（v1 單執行緒）。
inline const char* quoteExCh() {
  static char buf[96];
  static bool built = false;
  if (!built) {
    buf[0] = '\0';
    for (int i = 0; i < WATCH_N; i++) {
      if (i > 0) strlcat(buf, "|", sizeof buf);
      strlcat(buf, "tse_", sizeof buf);
      strlcat(buf, WATCHLIST[i].code, sizeof buf);
      strlcat(buf, ".tw", sizeof buf);
    }
    built = true;
  }
  return buf;
}
