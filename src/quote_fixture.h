#pragma once
// 受控硬體 fixture 純邏輯（無 Arduino 依賴）：僅 quote_logic.h + cstring
// host 測試：tests/host/test_quote_fixture.cpp
//   g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src ...
#include "quote_logic.h"

#include <cstring>

enum class QuoteFixtureKind {
  Normal,
  Untraded,
  PartialFailure,
};

// 以呼叫端傳入之 date（YYYYMMDD）填入 9 個預期代碼、固定有效價格、13:30:00。
// 不讀取系統時間。quoteTime 固定為 "13:30:00"。
// Untraded：index 5 為 valid=true、z=0、y!=0（盤中未成交語意）。
// PartialFailure：index 6 為 canonical invalid row，其餘列維持有效。
inline void fillQuoteFixture(qlogic::MarketBatch* out, const char* date,
                             QuoteFixtureKind kind) {
  static const double kZ[QUOTE_TOTAL] = {
      24000.0, 1200.0, 180.0, 200.0, 60.0, 120.0, 65.0, 90.0, 28.0,
  };
  static const double kY[QUOTE_TOTAL] = {
      23900.0, 1190.0, 178.0, 198.0, 59.0, 119.0, 64.0, 89.0, 27.5,
  };
  *out = qlogic::MarketBatch{};
  strncpy(out->date, date, sizeof(out->date) - 1);
  out->date[sizeof(out->date) - 1] = '\0';
  strncpy(out->quoteTime, "13:30:00", sizeof(out->quoteTime) - 1);
  out->quoteTime[sizeof(out->quoteTime) - 1] = '\0';
  for (int i = 0; i < QUOTE_TOTAL; ++i) {
    strncpy(out->rows[i].code, WATCHLIST[i].code, sizeof(out->rows[i].code) - 1);
    out->rows[i].code[sizeof(out->rows[i].code) - 1] = '\0';
    out->rows[i].valid = true;
    out->rows[i].z = kZ[i];
    out->rows[i].y = kY[i];
    strncpy(out->rows[i].t, "13:30:00", sizeof(out->rows[i].t) - 1);
    out->rows[i].t[sizeof(out->rows[i].t) - 1] = '\0';
  }
  if (kind == QuoteFixtureKind::Untraded) {
    out->rows[5].valid = true;
    out->rows[5].z = 0.0;
    // y 維持 kY[5]（!=0），t 維持 "13:30:00"
  } else if (kind == QuoteFixtureKind::PartialFailure) {
    qlogic::invalidateRow(&out->rows[6], WATCHLIST[6].code);
  }
}
