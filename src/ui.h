#pragma once
#include <cstdint>
#include "watchlist.h"

// 報價看板列數（單頁固定列數；與 watchlist.h PAGE_ROWS 對齊）
static constexpr int QUOTE_ROWS = PAGE_ROWS;

// 報價看板 UI（直式 272x792，display.setRotation(3) 統一旋轉；硬體驗證
// 3 才正立，spec 修訂七版更正）
struct QuoteView {
  const char* names[QUOTE_ROWS];
  bool valid[QUOTE_ROWS];
  double z[QUOTE_ROWS];
  double chg[QUOTE_ROWS];
  double pct[QUOTE_ROWS];
  char dateStr[20];    // "08-28 週五"
  char timeStr[8];     // "13:33"
  uint8_t pageIndex;
  uint8_t pageCount;
  const char* status;  // nullptr=正常；"部分失敗"/"更新失敗"/"時間未同步"
};

void uiInit();
void uiShowQuotes(const QuoteView& v);
void uiShowMessage(const char* line1, const char* line2);
void uiHibernate();
void uiSleepHoldPins();
