#pragma once
#include <cstdint>

// 報價看板列數（單一資料源；watchlist.h 以 static_assert 對齊 WATCH_N）
static constexpr int QUOTE_ROWS = 5;

// 報價看板 UI（直式 272x792，display.setRotation(1) 統一旋轉）
struct QuoteView {
  const char* names[QUOTE_ROWS];
  double z[QUOTE_ROWS];
  double chg[QUOTE_ROWS];
  double pct[QUOTE_ROWS];
  char dateStr[20];    // "08-28 週五"
  char timeStr[8];     // "13:33"
  const char* status;  // nullptr=正常；"更新失敗"/"時間未同步"
};

void uiInit();
void uiShowQuotes(const QuoteView& v);
void uiShowMessage(const char* line1, const char* line2);
void uiHibernate();
void uiSleepHoldPins();
