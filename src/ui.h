#pragma once
#include <cstdint>

// 報價看板 UI（直式 272x792，display.setRotation(1) 統一旋轉）
struct QuoteView {
  const char* names[5];
  double z[5];
  double chg[5];
  double pct[5];
  char dateStr[20];    // "08-28 週五"
  char timeStr[8];     // "13:33"
  const char* status;  // nullptr=正常；"更新失敗"/"時間未同步"
};

void uiInit();
void uiShowQuotes(const QuoteView& v);
void uiShowMessage(const char* line1, const char* line2);
void uiHibernate();
void uiSleepHoldPins();
