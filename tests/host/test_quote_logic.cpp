// host 測試：g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src tests/host/test_quote_logic.cpp
#include "quote_logic.h"
#include "watchlist.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <type_traits>

static qlogic::RawBatch validRawBatch() {
  qlogic::RawBatch raw = {};
  const char* z[QUOTE_TOTAL] = {
      "46331.45", "2420", "253", "106.95", "245.10",
      "180.5", "132.0", "95.6", "21.4"};
  const char* y[QUOTE_TOTAL] = {
      "45975.22", "2410", "252", "106.05", "243.15",
      "179.0", "131.5", "96.0", "21.1"};
  for (int i = 0; i < QUOTE_TOTAL; ++i) {
    raw.count[i] = 1;
    strcpy(raw.rows[i].code, WATCHLIST[i].code);
    strcpy(raw.rows[i].name, WATCHLIST[i].name);
    strcpy(raw.rows[i].z, z[i]);
    strcpy(raw.rows[i].y, y[i]);
    strcpy(raw.rows[i].t, i == 0 ? "13:33:00" : "13:30:00");
    strcpy(raw.rows[i].d, "20260903");
  }
  return raw;
}

static std::string jsonRows(std::initializer_list<const char*> codes) {
  std::string body = "{\"msgArray\":[";
  bool first = true;
  for (const char* code : codes) {
    if (!first) body += ',';
    first = false;
    body += "{\"c\":\"" + std::string(code) +
            "\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\","
            "\"t\":\"13:30:00\",\"d\":\"20260903\"}";
  }
  return body + "]}";
}

static void testParseNum() {
  double v = 0;
  assert(qlogic::parseNum("2420.0000", &v) && v == 2420.0);
  assert(qlogic::parseNum("0.00", &v) && v == 0.0);
  assert(!qlogic::parseNum("-", &v));
  assert(!qlogic::parseNum("", &v));
  assert(!qlogic::parseNum("abc", &v));
  assert(!qlogic::parseNum("24 20", &v));
  printf("parseNum ok\n");
}

static void testValidDate() {
  assert(qlogic::validDate("20260828"));
  assert(qlogic::validDate("20240229"));   // 閏年
  assert(!qlogic::validDate("20260231"));  // 不存在的日期
  assert(!qlogic::validDate("20230229"));  // 非閏年
  assert(!qlogic::validDate("20261301"));  // 月 13
  assert(!qlogic::validDate("20260010"));  // 月 0
  assert(!qlogic::validDate("2026-828"));
  assert(!qlogic::validDate("2026082"));
  printf("validDate ok\n");
}

static void testParseJson() {
  static const char* FIX_OK =
      "{\"msgArray\":["
      "{\"c\":\"t00\",\"n\":\"發行量加權股價指數\",\"z\":\"46331.45\",\"y\":\"45975.22\",\"t\":\"13:33:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2330\",\"n\":\"台積電\",\"z\":\"2420.0000\",\"y\":\"2410.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2317\",\"n\":\"鴻海\",\"z\":\"253.0000\",\"y\":\"252.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"0050\",\"n\":\"元大台灣50\",\"z\":\"106.9500\",\"y\":\"106.0500\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"006208\",\"n\":\"富邦台50\",\"z\":\"245.1000\",\"y\":\"243.1500\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"1513\",\"n\":\"中興電\",\"z\":\"180.5000\",\"y\":\"179.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2412\",\"n\":\"中華電\",\"z\":\"132.0000\",\"y\":\"131.5000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2881\",\"n\":\"富邦金\",\"z\":\"95.6000\",\"y\":\"96.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2002\",\"n\":\"中鋼\",\"z\":\"21.4000\",\"y\":\"21.1000\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}";
  qlogic::RawBatch raw;
  assert(qlogic::parseJsonToRaw(FIX_OK, strlen(FIX_OK), &raw) == qlogic::V_OK);
  qlogic::MarketBatch out;
  assert(qlogic::validateBatch(raw, &out) == qlogic::V_OK);
  assert(out.rows[0].z == 46331.45);

  // 壞 JSON
  assert(qlogic::parseJsonToRaw("{bad", 5, &raw) == qlogic::V_JSON);
  // 無 msgArray
  static const char* FIX_NOARR = "{\"rtcode\":\"0000\"}";
  assert(qlogic::parseJsonToRaw(FIX_NOARR, strlen(FIX_NOARR), &raw) == qlogic::V_JSON);
  // 列數不足不再是 JSON 錯誤：slot-based 收集接受任意列數，缺列由 count 反映
  static const char* FIX_4ROWS =
      "{\"msgArray\":[{\"c\":\"t00\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2330\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2317\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"0050\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}";
  assert(qlogic::parseJsonToRaw(FIX_4ROWS, strlen(FIX_4ROWS), &raw) == qlogic::V_OK);
  assert(raw.count[0] == 1 && raw.count[4] == 0);
  // 未知代碼直接忽略，不使整批失敗
  static const char* FIX_UNK =
      "{\"msgArray\":[{\"c\":\"t00\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2330\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2317\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"0050\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"9999\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}";
  assert(qlogic::parseJsonToRaw(FIX_UNK, strlen(FIX_UNK), &raw) == qlogic::V_OK);
  assert(raw.count[0] == 1);
  printf("parseJson ok\n");
}

static void testParseJsonTruncation() {
  // n 欄 40 個 '名'（UTF-8 120 bytes）遠超 name[32]：
  // parse 仍成功，name 截斷至 sizeof(name)-1 = 31 bytes 且第 32 byte 為 NUL
  char name[3 * 40 + 1];
  name[0] = '\0';
  for (int i = 0; i < 40; i++) strcat(name, "名");
  char fix[2048];
  snprintf(fix, sizeof fix,
           "{\"msgArray\":["
           "{\"c\":\"t00\",\"n\":\"%s\",\"z\":\"46331.45\",\"y\":\"45975.22\",\"t\":\"13:33:00\",\"d\":\"20260828\"},"
           "{\"c\":\"2330\",\"n\":\"台積電\",\"z\":\"2420.0000\",\"y\":\"2410.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"2317\",\"n\":\"鴻海\",\"z\":\"253.0000\",\"y\":\"252.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"0050\",\"n\":\"元大台灣50\",\"z\":\"106.9500\",\"y\":\"106.0500\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"006208\",\"n\":\"富邦台50\",\"z\":\"245.1000\",\"y\":\"243.1500\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"1513\",\"n\":\"中興電\",\"z\":\"180.5000\",\"y\":\"179.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"2412\",\"n\":\"中華電\",\"z\":\"132.0000\",\"y\":\"131.5000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"2881\",\"n\":\"富邦金\",\"z\":\"95.6000\",\"y\":\"96.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"2002\",\"n\":\"中鋼\",\"z\":\"21.4000\",\"y\":\"21.1000\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}",
           name);
  qlogic::RawBatch raw;
  assert(qlogic::parseJsonToRaw(fix, strlen(fix), &raw) == qlogic::V_OK);
  assert(strlen(raw.rows[0].name) == sizeof(raw.rows[0].name) - 1);
  assert(raw.rows[0].name[sizeof(raw.rows[0].name) - 1] == '\0');
  printf("parseJson truncation ok\n");
}

static void testValidateBatch() {
  auto raw = validRawBatch();
  // validRawBatch 用 20260903；此舊測試沿用同一份資料，僅改日期斷言
  for (int i = 0; i < QUOTE_TOTAL; ++i) strcpy(raw.rows[i].d, "20260828");
  qlogic::MarketBatch out;
  assert(qlogic::validateBatch(raw, &out) == qlogic::V_OK);
  assert(strcmp(out.date, "20260828") == 0);
  assert(strcmp(out.quoteTime, "13:33:00") == 0);
  assert(out.rows[0].z == 46331.45 && out.rows[0].y == 45975.22);
  assert(out.rows[0].valid);

  // index 缺列／重複 → 整批 V_STRUCT（count 語意）
  qlogic::RawBatch missIdx = raw;
  missIdx.count[0] = 0;
  assert(qlogic::validateBatch(missIdx, &out) == qlogic::V_STRUCT);

  qlogic::RawBatch dupIdx = raw;
  dupIdx.count[0] = 2;
  assert(qlogic::validateBatch(dupIdx, &out) == qlogic::V_STRUCT);

  // z 為 "-"（盤中未成交，spec 修訂七版）：該列記 z=0，整批仍有效
  qlogic::RawBatch dash = raw;
  strcpy(dash.rows[2].z, "-");
  assert(qlogic::validateBatch(dash, &out) == qlogic::V_OK);
  assert(dash.rows[2].z[0] == '-');  // raw 不被改寫
  qlogic::MarketBatch dashOut;
  assert(qlogic::validateBatch(dash, &dashOut) == qlogic::V_OK);
  assert(dashOut.rows[2].valid && dashOut.rows[2].z == 0.0);
  assert(dashOut.rows[2].y == 252.0);  // y 欄不受影響

  // 個股 y 為 "-" 降級為無效列，不再整批 V_NUMERIC
  qlogic::RawBatch ydash = raw;
  strcpy(ydash.rows[2].y, "-");
  qlogic::MarketBatch yOut;
  assert(qlogic::validateBatch(ydash, &yOut) == qlogic::V_OK);
  assert(!yOut.rows[2].valid);
  assert(yOut.rows[2].z == 0 && yOut.rows[2].y == 0);
  // index y 為 "-" 仍整批 V_NUMERIC
  qlogic::RawBatch idxYdash = raw;
  strcpy(idxYdash.rows[0].y, "-");
  assert(qlogic::validateBatch(idxYdash, &out) == qlogic::V_NUMERIC);

  // 全部九列都未成交：整批仍有效（開盤瞬間的合法狀態）
  qlogic::RawBatch allDash = raw;
  for (int i = 0; i < QUOTE_TOTAL; i++) strcpy(allDash.rows[i].z, "-");
  qlogic::MarketBatch allDashOut;
  assert(qlogic::validateBatch(allDash, &allDashOut) == qlogic::V_OK);
  for (int i = 0; i < QUOTE_TOTAL; i++) {
    assert(allDashOut.rows[i].valid && allDashOut.rows[i].z == 0.0);
  }

  // 個股 z 為空字串降級；index z 空仍 V_NUMERIC
  qlogic::RawBatch emptyZ = raw;
  strcpy(emptyZ.rows[2].z, "");
  qlogic::MarketBatch emptyOut;
  assert(qlogic::validateBatch(emptyZ, &emptyOut) == qlogic::V_OK);
  assert(!emptyOut.rows[2].valid);
  qlogic::RawBatch idxEmptyZ = raw;
  strcpy(idxEmptyZ.rows[0].z, "");
  assert(qlogic::validateBatch(idxEmptyZ, &out) == qlogic::V_NUMERIC);

  // 個股 y == 0 降級；index y == 0 仍 V_NUMERIC
  qlogic::RawBatch zero = raw;
  strcpy(zero.rows[1].y, "0");
  qlogic::MarketBatch zeroOut;
  assert(qlogic::validateBatch(zero, &zeroOut) == qlogic::V_OK);
  assert(!zeroOut.rows[1].valid);
  qlogic::RawBatch idxZero = raw;
  strcpy(idxZero.rows[0].y, "0");
  assert(qlogic::validateBatch(idxZero, &out) == qlogic::V_NUMERIC);

  // 個股 d 格式錯誤降級；index d 錯仍 V_FORMAT
  qlogic::RawBatch badd = raw;
  strcpy(badd.rows[3].d, "2026-8-2");
  qlogic::MarketBatch badDOut;
  assert(qlogic::validateBatch(badd, &badDOut) == qlogic::V_OK);
  assert(!badDOut.rows[3].valid);
  qlogic::RawBatch idxBadD = raw;
  strcpy(idxBadD.rows[0].d, "2026-8-2");
  assert(qlogic::validateBatch(idxBadD, &out) == qlogic::V_FORMAT);

  // 個股 t 格式錯誤降級；index t 錯仍 V_FORMAT
  qlogic::RawBatch badt = raw;
  strcpy(badt.rows[3].t, "9:30:00");
  qlogic::MarketBatch badTOut;
  assert(qlogic::validateBatch(badt, &badTOut) == qlogic::V_OK);
  assert(!badTOut.rows[3].valid);
  qlogic::RawBatch idxBadT = raw;
  strcpy(idxBadT.rows[0].t, "9:30:00");
  assert(qlogic::validateBatch(idxBadT, &out) == qlogic::V_FORMAT);

  // 個股 d 與指數不同降級，不再整批 V_DATE_DIFF
  qlogic::RawBatch mixd = raw;
  strcpy(mixd.rows[2].d, "20260827");
  qlogic::MarketBatch mixOut;
  assert(qlogic::validateBatch(mixd, &mixOut) == qlogic::V_OK);
  assert(!mixOut.rows[2].valid);

  printf("validateBatch ok\n");
}

static void testCalc() {
  qlogic::QuoteCalc c = qlogic::calcQuote(2420.0, 2410.0);
  assert(c.chg == 10.0);
  assert(fabs(c.pct - 0.4149378) < 1e-6);
  c = qlogic::calcQuote(100.0, 110.0);
  assert(c.chg == -10.0 && c.pct < 0);
  c = qlogic::calcQuote(50.0, 50.0);
  assert(c.chg == 0.0 && c.pct == 0.0);
  printf("calc ok\n");
}

static void testFormatPrice() {
  char buf[24];
  qlogic::formatPrice(46331.45, buf, sizeof buf);
  assert(strcmp(buf, "46,331.45") == 0);
  qlogic::formatPrice(2420.0, buf, sizeof buf);
  assert(strcmp(buf, "2,420.00") == 0);
  qlogic::formatPrice(253.0, buf, sizeof buf);
  assert(strcmp(buf, "253.00") == 0);
  printf("formatPrice ok\n");
}

static void testCivil() {
  // 2026-08-28 00:00 台北 = 1787846400 UTC；週五
  qlogic::Civil c = qlogic::civilFromEpoch(1787846400u, qlogic::TZ_TW);
  assert(c.y == 2026 && c.m == 8 && c.d == 28 && c.wday == 5);
  char buf[24];
  qlogic::formatDateTW("20260828", buf, sizeof buf);
  assert(strcmp(buf, "08-28 週五") == 0);
  qlogic::formatDateTW("20260830", buf, sizeof buf);
  assert(strcmp(buf, "08-30 週日") == 0);
  char d[16];
  qlogic::dateOfEpoch(1787846400u, qlogic::TZ_TW, d, sizeof d);
  assert(strcmp(d, "20260828") == 0);
  printf("civil ok\n");
}

static void testFormatDateInvalid() {
  // 不存在的日期 → validDate 拒絕 → invalid 分支輸出 "??-??"
  char buf[24];
  qlogic::formatDateTW("20260231", buf, sizeof buf);
  assert(strcmp(buf, "\?\?-\?\?") == 0);
  printf("formatDate invalid ok\n");
}

static void testSchedule() {
  using qlogic::marketState;
  // 2026-08-28 = 週五；00:00 台北 = 1787846400 UTC
  uint32_t fri00 = 1787846400u;
  assert(marketState(fri00 + 7 * 3600) == qlogic::MarketState::PreMarket);
  assert(marketState(fri00 + 9 * 3600) == qlogic::MarketState::Trading);
  assert(marketState(fri00 + 13 * 3600 + 29 * 60) == qlogic::MarketState::Trading);
  assert(marketState(fri00 + 13 * 3600 + 30 * 60) == qlogic::MarketState::PostClose);
  assert(marketState(fri00 + 2 * 86400) == qlogic::MarketState::Weekend);  // 週日
  assert(marketState(fri00 + 86400) == qlogic::MarketState::Weekend);      // 週六

  // PRE_MARKET → 當日 09:00
  assert(qlogic::todayAt9(fri00 + 7 * 3600) == fri00 + 9 * 3600);

  // POST_CLOSE 週五 14:00 → 次交易日（週一）09:00
  uint32_t fri14 = fri00 + 14 * 3600;
  assert(qlogic::nextWeekdayAt9(fri14) == fri00 + 3 * 86400 + 9 * 3600);

  // 週六 10:00 → 週一 09:00
  assert(qlogic::nextWeekdayAt9(fri00 + 86400 + 10 * 3600) == fri00 + 3 * 86400 + 9 * 3600);

  // 週三 14:00 → 週四 09:00
  uint32_t wed14 = fri00 - 2 * 86400 + 14 * 3600;
  assert(qlogic::nextWeekdayAt9(wed14) == fri00 - 86400 + 9 * 3600);

  // 假日（TRADING 中 d!=today）→ 隔日 09:00（含跨週末場景由呼叫端決定）
  assert(qlogic::nextDayAt9(fri00 + 10 * 3600) == fri00 + 86400 + 9 * 3600);

  // 5 分邊界：09:02:10 → 09:05:00（170s）；09:04:50 → 跳 09:10（310s，最小 30s）
  uint32_t t1 = fri00 + 9 * 3600 + 2 * 60 + 10;
  assert(qlogic::nextTradingBoundary(t1) - t1 == 170);
  uint32_t t2 = fri00 + 9 * 3600 + 4 * 60 + 50;
  assert(qlogic::nextTradingBoundary(t2) - t2 == 310);

  // 24h cap
  assert(qlogic::capSleep(fri00, fri00 + 3 * 86400 + 9 * 3600) == 86400);
  assert(qlogic::capSleep(fri00, fri00 + 100) == 100);

  // 收盤定格緩衝（spec 修訂八版）：13:35 起才允許定格
  assert(!qlogic::closeFinalReady(fri00 + 13 * 3600 + 30 * 60));       // 13:30:00
  assert(!qlogic::closeFinalReady(fri00 + 13 * 3600 + 34 * 60 + 59));  // 13:34:59
  assert(qlogic::closeFinalReady(fri00 + 13 * 3600 + 35 * 60));        // 13:35:00
  assert(qlogic::closeFinalReady(fri00 + 23 * 3600 + 59 * 60 + 59));   // 23:59:59
  // closeFinalAt：13:30:20 → 當日 13:35:00（前置條件 13:30 ≤ now < 13:35）
  uint32_t cf = fri00 + 13 * 3600 + 30 * 60 + 20;
  assert(qlogic::closeFinalAt(cf) == fri00 + 13 * 3600 + 35 * 60);
  printf("schedule ok\n");
}

static void testBlob() {
  static_assert(qlogic::BLOB_VERSION == 2);
  static_assert(std::is_standard_layout_v<qlogic::QuoteRow>);
  static_assert(std::is_standard_layout_v<qlogic::QuoteRecord>);
  static_assert(sizeof(qlogic::QuoteRow) == 48);
  static_assert(alignof(qlogic::QuoteRow) == 8);
  static_assert(offsetof(qlogic::QuoteRow, code) == 0);
  static_assert(offsetof(qlogic::QuoteRow, valid) == 12);
  static_assert(offsetof(qlogic::QuoteRow, z) == 16);
  static_assert(offsetof(qlogic::QuoteRow, y) == 24);
  static_assert(offsetof(qlogic::QuoteRow, t) == 32);
  static_assert(sizeof(qlogic::QuoteRecord) == 472);
  static_assert(offsetof(qlogic::QuoteRecord, rows) == 8);
  static_assert(offsetof(qlogic::QuoteRecord, quoteDate) == 440);
  static_assert(offsetof(qlogic::QuoteRecord, quoteTime) == 449);
  static_assert(offsetof(qlogic::QuoteRecord, lastCloseDate) == 458);
  static_assert(offsetof(qlogic::QuoteRecord, savedEpoch) == 468);
  qlogic::QuoteRecord a = {};
  a.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < QUOTE_TOTAL; i++) {
    strcpy(a.rows[i].code, WATCHLIST[i].code);
    a.rows[i].valid = true;
    a.rows[i].z = 10.0 + i;
    a.rows[i].y = 9.0 + i;
    strcpy(a.rows[i].t, "13:30:00");
  }
  strcpy(a.quoteDate, "20260828");
  strcpy(a.quoteTime, "13:30:00");
  a.savedEpoch = 1787894400u;

  qlogic::QuoteRecord b = a;
  assert(!qlogic::recordDiffers(a, b));
  b.rows[1].z = 2420.0;
  assert(qlogic::recordDiffers(a, b));       // 價格變更
  b = a; strcpy(b.quoteDate, "20260831");
  assert(qlogic::recordDiffers(a, b));       // 交易日變更
  b = a; strcpy(b.lastCloseDate, "20260828");
  assert(qlogic::recordDiffers(a, b));       // 定格旗標變更
  b = a; strcpy(b.quoteTime, "13:31:00");
  assert(!qlogic::recordDiffers(a, b));      // 僅時間變更不寫（spec 修訂四）
  b = a; b.savedEpoch++;
  assert(!qlogic::recordDiffers(a, b));      // 僅 savedEpoch 不寫
  b = a; b.rows[2].valid = false; b.rows[2].z = 0; b.rows[2].y = 0; b.rows[2].t[0] = '\0';
  assert(qlogic::recordDiffers(a, b));       // valid 變更觸發寫入

  assert(qlogic::recordSane(a));
  b = a; b.version = qlogic::BLOB_VERSION + 1;
  assert(!qlogic::recordSane(b));
  b = a; b.rows[3].y = 0.0;
  assert(!qlogic::recordSane(b));
  b = a; strcpy(b.quoteDate, "2026-8-28");
  assert(!qlogic::recordSane(b));
  // 個股 canonical invalid 可接受
  b = a; b.rows[3].valid = false; b.rows[3].z = 0; b.rows[3].y = 0; b.rows[3].t[0] = '\0';
  assert(qlogic::recordSane(b));
  // 個股非 canonical invalid（保留舊價）拒絕
  b = a; b.rows[3].valid = false;
  assert(!qlogic::recordSane(b));
  // index 不接受無效
  b = a; b.rows[0].valid = false; b.rows[0].z = 0; b.rows[0].y = 0; b.rows[0].t[0] = '\0';
  assert(!qlogic::recordSane(b));

  // 序列化 roundtrip（putBytes 語意）
  char bytes[sizeof(qlogic::QuoteRecord)];
  memcpy(bytes, &a, sizeof a);
  qlogic::QuoteRecord c;
  memcpy(&c, bytes, sizeof c);
  assert(memcmp(&a, &c, sizeof a) == 0);
  printf("blob v2 ok\n");
}

static void testPageRtc() {
  assert(qlogic::quoteIndexForPageRow(0, 0) == 0);
  assert(qlogic::quoteIndexForPageRow(0, 4) == 4);
  assert(qlogic::quoteIndexForPageRow(1, 0) == 0);
  assert(qlogic::quoteIndexForPageRow(1, 1) == 5);
  assert(qlogic::quoteIndexForPageRow(1, 4) == 8);
  assert(qlogic::quoteIndexForPageRow(2, 1) == -1);
  assert(qlogic::quoteIndexForPageRow(0, 5) == -1);
  assert(qlogic::changedPage(0, qlogic::WakeAction::PrevPage) == 1);
  assert(qlogic::changedPage(1, qlogic::WakeAction::NextPage) == 0);
  assert(qlogic::chooseWakeAction(true, true, false) == qlogic::WakeAction::Menu);
  assert(qlogic::chooseWakeAction(false, true, true) == qlogic::WakeAction::None);

  qlogic::QuoteRtcState rtc{1, 1234};
  qlogic::normalizeRtcState(&rtc, false);
  assert(rtc.pageIndex == 0 && rtc.targetEpoch == 0);
  rtc = {9, 1234};
  qlogic::normalizeRtcState(&rtc, true);
  assert(rtc.pageIndex == 0 && rtc.targetEpoch == 1234);

  const uint32_t now = 1000000;
  assert(qlogic::resumeTarget(now, now + 20) == now + 20);
  assert(qlogic::resumeTarget(now, now) == now + 1);
  assert(qlogic::resumeTarget(now, now - 1) == now + 1);
  assert(qlogic::resumeTarget(now, 0) == now + 300);
  assert(qlogic::resumeTarget(now, now + 7 * 86400 + 1) == now + 300);
  assert(qlogic::resumeTarget(now, now - 7 * 86400 - 1) == now + 300);
  assert(qlogic::noCacheRetryTarget(now, now + 1000) == now + 300);
  assert(qlogic::noCacheRetryTarget(now, now + 20) == now + 20);
  // nineOhFive 代表 09:04:40→09:05:00 的 20 秒差：翻頁不得跳過下一個 5 分邊界
  uint32_t nineOhFive = 1000020;
  assert(qlogic::resumeTarget(1000000, nineOhFive) == nineOhFive);
  uint32_t retry = qlogic::noCacheRetryTarget(1000000, 1002000);
  assert(retry == 1000300);
  // 第二次翻頁不得把 retry deadline 往後滑動
  assert(qlogic::noCacheRetryTarget(1000100, retry) == 1000300);
  printf("page/rtc ok\n");
}

static void testWatchlistAndExCh() {
  static const char EXPECTED[] =
      "tse_t00.tw|tse_2330.tw|tse_2317.tw|tse_0050.tw|tse_006208.tw|"
      "tse_1513.tw|tse_2412.tw|tse_2881.tw|tse_2002.tw";
  static_assert(QUOTE_TOTAL == 9);
  static_assert(STOCK_TOTAL == 8);
  static_assert(PAGE_COUNT == 2);
  static_assert(STOCKS_PER_PAGE == 4);
  static_assert(PAGE_ROWS == 5);
  assert(strcmp(WATCHLIST[5].code, "1513") == 0);
  assert(strcmp(WATCHLIST[5].name, "中興電") == 0);
  assert(strcmp(WATCHLIST[8].code, "2002") == 0);

  const size_t len = strlen(EXPECTED);
  char exact[sizeof EXPECTED];
  memset(exact, 'x', sizeof exact);
  assert(buildQuoteExCh(exact, len + 1));
  assert(strcmp(exact, EXPECTED) == 0);

  char shortBuf[sizeof EXPECTED];
  memset(shortBuf, 'x', sizeof shortBuf);
  assert(!buildQuoteExCh(shortBuf, len));
  assert(shortBuf[0] == '\0');
  assert(!buildQuoteExCh(nullptr, len + 1));
  char zeroCap = 'x';
  assert(!buildQuoteExCh(&zeroCap, 0));
  assert(zeroCap == 'x');

  char again[sizeof EXPECTED];
  assert(buildQuoteExCh(again, sizeof again));
  assert(strcmp(again, exact) == 0);
  printf("watchlist/ex_ch ok\n");
}

static void testNineRowValidation() {
  qlogic::MarketBatch out;
  auto raw = validRawBatch();
  assert(qlogic::validateBatch(raw, &out) == qlogic::V_OK);
  assert(out.rows[0].valid && out.rows[8].valid);
  assert(strcmp(out.date, "20260903") == 0);
  assert(strcmp(out.quoteTime, "13:33:00") == 0);

  auto missingIndex = raw;
  missingIndex.count[0] = 0;
  assert(qlogic::validateBatch(missingIndex, &out) == qlogic::V_STRUCT);

  auto duplicateIndex = raw;
  duplicateIndex.count[0] = 2;
  assert(qlogic::validateBatch(duplicateIndex, &out) == qlogic::V_STRUCT);

  auto badStock = raw;
  strcpy(badStock.rows[6].y, "-");
  assert(qlogic::validateBatch(badStock, &out) == qlogic::V_OK);
  assert(!out.rows[6].valid);
  assert(out.rows[6].z == 0 && out.rows[6].y == 0);
  assert(out.rows[6].t[0] == '\0');

  auto stockDash = raw;
  strcpy(stockDash.rows[5].z, "-");
  assert(qlogic::validateBatch(stockDash, &out) == qlogic::V_OK);
  assert(out.rows[5].valid && out.rows[5].z == 0);

  auto badDate = raw;
  strcpy(badDate.rows[7].d, "20260902");
  assert(qlogic::validateBatch(badDate, &out) == qlogic::V_OK);
  assert(!out.rows[7].valid);

  auto newerInvalid = raw;
  strcpy(newerInvalid.rows[8].t, "13:34:00");
  strcpy(newerInvalid.rows[8].name, "");
  assert(qlogic::validateBatch(newerInvalid, &out) == qlogic::V_OK);
  assert(strcmp(out.quoteTime, "13:33:00") == 0);

  auto assertOnlyStockInvalid = [&](qlogic::RawBatch candidate, int bad) {
    qlogic::MarketBatch checked;
    assert(qlogic::validateBatch(candidate, &checked) == qlogic::V_OK);
    for (int i = 0; i < QUOTE_TOTAL; ++i) assert(checked.rows[i].valid == (i != bad));
    assert(strcmp(checked.rows[bad].code, WATCHLIST[bad].code) == 0);
    assert(checked.rows[bad].z == 0 && checked.rows[bad].y == 0);
    assert(checked.rows[bad].t[0] == '\0');
  };

  auto missing = raw; missing.count[5] = 0; assertOnlyStockInvalid(missing, 5);
  auto duplicate = raw; duplicate.count[5] = 2; assertOnlyStockInvalid(duplicate, 5);
  auto noName = raw; noName.rows[5].name[0] = '\0'; assertOnlyStockInvalid(noName, 5);
  auto badZ = raw; strcpy(badZ.rows[5].z, "bad"); assertOnlyStockInvalid(badZ, 5);
  auto badTime = raw; strcpy(badTime.rows[5].t, "9:30"); assertOnlyStockInvalid(badTime, 5);
  auto otherDate = raw; strcpy(otherDate.rows[5].d, "20260902");
  assertOnlyStockInvalid(otherDate, 5);

  auto indexNumeric = raw; strcpy(indexNumeric.rows[0].y, "-");
  assert(qlogic::validateBatch(indexNumeric, &out) == qlogic::V_NUMERIC);
  auto indexFormat = raw; indexFormat.rows[0].name[0] = '\0';
  assert(qlogic::validateBatch(indexFormat, &out) == qlogic::V_FORMAT);
  auto indexMissing = raw; indexMissing.count[0] = 0;
  assert(qlogic::validateBatch(indexMissing, &out) == qlogic::V_STRUCT);
  auto indexDuplicate = raw; indexDuplicate.count[0] = 2;
  assert(qlogic::validateBatch(indexDuplicate, &out) == qlogic::V_STRUCT);
  printf("nine-row validation ok\n");
}

static void testNineRowJsonCollection() {
  qlogic::MarketBatch out;
  const char* all[] = {"t00","2330","2317","0050","006208",
                       "1513","2412","2881","2002"};
  std::string complete = jsonRows({all[0],all[1],all[2],all[3],all[4],
                                   all[5],all[6],all[7],all[8]});
  qlogic::RawBatch collected = {};
  assert(qlogic::parseJsonToRaw(complete.c_str(), complete.size(), &collected) == qlogic::V_OK);
  for (int i = 0; i < QUOTE_TOTAL; ++i) assert(collected.count[i] == 1);

  std::string unknown = jsonRows({all[0],all[1],all[2],all[3],all[4],
                                  all[5],all[6],all[7],all[8],"9999"});
  assert(qlogic::parseJsonToRaw(unknown.c_str(), unknown.size(), &collected) == qlogic::V_OK);
  for (int i = 0; i < QUOTE_TOTAL; ++i) assert(collected.count[i] == 1);

  std::string missingStock = jsonRows({all[0],all[1],all[2],all[3],all[4],
                                       all[5],all[7],all[8]});
  assert(qlogic::parseJsonToRaw(missingStock.c_str(), missingStock.size(), &collected) == qlogic::V_OK);
  assert(collected.count[6] == 0);

  std::string duplicateStock = jsonRows({all[0],all[1],all[2],all[3],all[4],
                                         all[5],all[6],all[6],all[7],all[8]});
  assert(qlogic::parseJsonToRaw(duplicateStock.c_str(), duplicateStock.size(), &collected) == qlogic::V_OK);
  assert(collected.count[6] == 2);

  std::string missingIndex = jsonRows({all[1],all[2],all[3],all[4],all[5],all[6],all[7],all[8]});
  assert(qlogic::parseJsonToRaw(missingIndex.c_str(), missingIndex.size(), &collected) == qlogic::V_OK);
  assert(qlogic::validateBatch(collected, &out) == qlogic::V_STRUCT);
  std::string duplicateIndex = jsonRows({all[0],all[0],all[1],all[2],all[3],all[4],
                                         all[5],all[6],all[7],all[8]});
  assert(qlogic::parseJsonToRaw(duplicateIndex.c_str(), duplicateIndex.size(), &collected) == qlogic::V_OK);
  assert(qlogic::validateBatch(collected, &out) == qlogic::V_STRUCT);

  std::string many = "{\"msgArray\":[";
  for (int i = 0; i < 300; ++i) {
    if (i) many += ',';
    many += "{\"c\":\"t00\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",";
    many += "\"t\":\"13:30:00\",\"d\":\"20260903\"}";
  }
  many += "]}";
  qlogic::RawBatch repeated = {};
  assert(qlogic::parseJsonToRaw(many.c_str(), many.size(), &repeated) == qlogic::V_OK);
  assert(repeated.count[0] == 2);
  assert(qlogic::validateBatch(repeated, &out) == qlogic::V_STRUCT);
  printf("nine-row json ok\n");
}

static void testViewStatus() {
  using qlogic::ViewStatus;
  assert(qlogic::resolvedStatus(ViewStatus::None, false) == ViewStatus::None);
  assert(qlogic::resolvedStatus(ViewStatus::None, true) == ViewStatus::PartialFailure);
  assert(qlogic::resolvedStatus(ViewStatus::PartialFailure, false) == ViewStatus::None);
  assert(qlogic::resolvedStatus(ViewStatus::UpdateFailure, false) == ViewStatus::UpdateFailure);
  assert(qlogic::resolvedStatus(ViewStatus::UpdateFailure, true) == ViewStatus::UpdateFailure);
  assert(qlogic::resolvedStatus(ViewStatus::TimeUnsynced, false) == ViewStatus::TimeUnsynced);
  assert(qlogic::resolvedStatus(ViewStatus::TimeUnsynced, true) == ViewStatus::TimeUnsynced);

  auto raw = validRawBatch();
  qlogic::MarketBatch mb;
  assert(qlogic::validateBatch(raw, &mb) == qlogic::V_OK);
  qlogic::QuoteRecord record = {};
  record.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < QUOTE_TOTAL; ++i) record.rows[i] = mb.rows[i];
  qlogic::invalidateRow(&record.rows[6], WATCHLIST[6].code);
  auto pageHasInvalid = [&](uint8_t page) {
    for (int row = 0; row < PAGE_ROWS; ++row) {
      int idx = qlogic::quoteIndexForPageRow(page, row);
      if (!record.rows[idx].valid) return true;
    }
    return false;
  };
  assert(!pageHasInvalid(0));
  assert(pageHasInvalid(1));
  printf("view status ok\n");
}

static void testHardening() {
  const uint32_t now = 1000000;
  assert(qlogic::changedPage(9, qlogic::WakeAction::NextPage) == 0);  // OOB page resets to 0
  qlogic::QuoteRtcState keep{1, 1234};
  qlogic::normalizeRtcState(&keep, true);
  assert(keep.pageIndex == 1 && keep.targetEpoch == 1234);  // valid page preserved
  qlogic::normalizeRtcState(nullptr, true);  // must not crash
  qlogic::normalizeRtcState(nullptr, false);  // must not crash
  assert(qlogic::resumeTarget(now, now + 7 * 86400) == now + 7 * 86400);  // exact +7d valid
  assert(qlogic::chooseWakeAction(false, true, false) == qlogic::WakeAction::PrevPage);
  assert(qlogic::chooseWakeAction(false, false, true) == qlogic::WakeAction::NextPage);
  assert(qlogic::chooseWakeAction(false, false, false) == qlogic::WakeAction::None);
  assert(qlogic::changedPage(1, qlogic::WakeAction::None) == 1);
  assert(qlogic::changedPage(0, qlogic::WakeAction::Menu) == 0);
  printf("hardening ok\n");
}

int main() {
  testWatchlistAndExCh();
  testParseNum();
  testValidDate();
  testParseJson();
  testParseJsonTruncation();
  testValidateBatch();
  testNineRowValidation();
  testNineRowJsonCollection();
  testCalc();
  testFormatPrice();
  testCivil();
  testFormatDateInvalid();
  testSchedule();
  testBlob();
  testPageRtc();
  testViewStatus();
  testHardening();
  printf("ALL PASS\n");
  return 0;
}
