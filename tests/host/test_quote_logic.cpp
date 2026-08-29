// host 測試：g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src tests/host/test_quote_logic.cpp
#include "quote_logic.h"
#include <cassert>
#include <cstdio>
#include <cstring>

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
      "{\"c\":\"006208\",\"n\":\"富邦台50\",\"z\":\"245.1000\",\"y\":\"243.1500\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}";
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
  // 列數不等於 5
  static const char* FIX_4ROWS =
      "{\"msgArray\":[{\"c\":\"t00\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2330\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2317\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"0050\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}";
  assert(qlogic::parseJsonToRaw(FIX_4ROWS, strlen(FIX_4ROWS), &raw) == qlogic::V_JSON);
  // 未知代碼
  static const char* FIX_UNK =
      "{\"msgArray\":[{\"c\":\"t00\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2330\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"2317\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"0050\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
      "{\"c\":\"9999\",\"n\":\"x\",\"z\":\"1\",\"y\":\"1\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}";
  assert(qlogic::parseJsonToRaw(FIX_UNK, strlen(FIX_UNK), &raw) == qlogic::V_STRUCT);
  printf("parseJson ok\n");
}

static void testValidateBatch() {
  qlogic::RawBatch raw = {};
  const char* codes[5] = {"t00", "2330", "2317", "0050", "006208"};
  const char* z[5] = {"46331.45", "2420.0000", "253.0000", "106.9500", "245.1000"};
  const char* y[5] = {"45975.22", "2410.0000", "252.0000", "106.0500", "243.1500"};
  const char* t[5] = {"13:33:00", "13:30:00", "13:30:00", "13:30:00", "13:30:00"};
  for (int i = 0; i < 5; i++) {
    strcpy(raw.rows[i].code, codes[i]);
    strcpy(raw.rows[i].name, "x");
    strcpy(raw.rows[i].z, z[i]);
    strcpy(raw.rows[i].y, y[i]);
    strcpy(raw.rows[i].t, t[i]);
    strcpy(raw.rows[i].d, "20260828");
  }
  qlogic::MarketBatch out;
  assert(qlogic::validateBatch(raw, &out) == qlogic::V_OK);
  assert(strcmp(out.date, "20260828") == 0);
  assert(strcmp(out.quoteTime, "13:33:00") == 0);
  assert(out.rows[0].z == 46331.45 && out.rows[0].y == 45975.22);

  // 缺列（漏掉 006208）
  qlogic::RawBatch miss = raw;
  miss.rows[4].code[0] = '\0';
  assert(qlogic::validateBatch(miss, &out) == qlogic::V_STRUCT);

  // 重複代碼
  qlogic::RawBatch dup = raw;
  strcpy(dup.rows[4].code, "2330");
  assert(qlogic::validateBatch(dup, &out) == qlogic::V_STRUCT);

  // z 為 "-"
  qlogic::RawBatch dash = raw;
  strcpy(dash.rows[2].z, "-");
  assert(qlogic::validateBatch(dash, &out) == qlogic::V_NUMERIC);

  // y == 0
  qlogic::RawBatch zero = raw;
  strcpy(zero.rows[1].y, "0.0000");
  assert(qlogic::validateBatch(zero, &out) == qlogic::V_NUMERIC);

  // d 格式錯誤
  qlogic::RawBatch badd = raw;
  strcpy(badd.rows[3].d, "2026-8-2");
  assert(qlogic::validateBatch(badd, &out) == qlogic::V_FORMAT);

  // t 格式錯誤
  qlogic::RawBatch badt = raw;
  strcpy(badt.rows[3].t, "9:30:00");
  assert(qlogic::validateBatch(badt, &out) == qlogic::V_FORMAT);

  // 五列 d 不一致
  qlogic::RawBatch mixd = raw;
  strcpy(mixd.rows[2].d, "20260827");
  assert(qlogic::validateBatch(mixd, &out) == qlogic::V_DATE_DIFF);

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

int main() {
  testParseNum();
  testValidDate();
  testParseJson();
  testValidateBatch();
  testCalc();
  testFormatPrice();
  testCivil();
  printf("ALL PASS\n");
  return 0;
}
