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

static void testParseJsonTruncation() {
  // n 欄 40 個 '名'（UTF-8 120 bytes）遠超 name[32]：
  // parse 仍成功，name 截斷至 sizeof(name)-1 = 31 bytes 且第 32 byte 為 NUL
  char name[3 * 40 + 1];
  name[0] = '\0';
  for (int i = 0; i < 40; i++) strcat(name, "名");
  char fix[1024];
  snprintf(fix, sizeof fix,
           "{\"msgArray\":["
           "{\"c\":\"t00\",\"n\":\"%s\",\"z\":\"46331.45\",\"y\":\"45975.22\",\"t\":\"13:33:00\",\"d\":\"20260828\"},"
           "{\"c\":\"2330\",\"n\":\"台積電\",\"z\":\"2420.0000\",\"y\":\"2410.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"2317\",\"n\":\"鴻海\",\"z\":\"253.0000\",\"y\":\"252.0000\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"0050\",\"n\":\"元大台灣50\",\"z\":\"106.9500\",\"y\":\"106.0500\",\"t\":\"13:30:00\",\"d\":\"20260828\"},"
           "{\"c\":\"006208\",\"n\":\"富邦台50\",\"z\":\"245.1000\",\"y\":\"243.1500\",\"t\":\"13:30:00\",\"d\":\"20260828\"}]}",
           name);
  qlogic::RawBatch raw;
  assert(qlogic::parseJsonToRaw(fix, strlen(fix), &raw) == qlogic::V_OK);
  assert(strlen(raw.rows[0].name) == sizeof(raw.rows[0].name) - 1);
  assert(raw.rows[0].name[sizeof(raw.rows[0].name) - 1] == '\0');
  printf("parseJson truncation ok\n");
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

  // z 為 "-"（盤中未成交，spec 修訂七版）：該列記 z=0，整批仍有效
  qlogic::RawBatch dash = raw;
  strcpy(dash.rows[2].z, "-");
  assert(qlogic::validateBatch(dash, &out) == qlogic::V_OK);
  assert(dash.rows[2].z[0] == '-');  // raw 不被改寫
  qlogic::MarketBatch dashOut;
  assert(qlogic::validateBatch(dash, &dashOut) == qlogic::V_OK);
  assert(dashOut.rows[2].z == 0.0);
  assert(dashOut.rows[2].y == 252.0000);  // y 欄不受影響

  // y 為 "-" 仍拒絕（修訂七版只放寬 z）
  qlogic::RawBatch ydash = raw;
  strcpy(ydash.rows[2].y, "-");
  assert(qlogic::validateBatch(ydash, &out) == qlogic::V_NUMERIC);

  // 全部五列都未成交：整批仍有效（開盤瞬間的合法狀態）
  qlogic::RawBatch allDash = raw;
  for (int i = 0; i < 5; i++) strcpy(allDash.rows[i].z, "-");
  qlogic::MarketBatch allDashOut;
  assert(qlogic::validateBatch(allDash, &allDashOut) == qlogic::V_OK);
  for (int i = 0; i < 5; i++) assert(allDashOut.rows[i].z == 0.0);

  // z 為空字串仍拒絕（只有 "-" 有未成交語意）
  qlogic::RawBatch emptyZ = raw;
  strcpy(emptyZ.rows[2].z, "");
  assert(qlogic::validateBatch(emptyZ, &out) == qlogic::V_NUMERIC);

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
  qlogic::QuoteRecord a = {};
  a.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < 5; i++) {
    strcpy(a.rows[i].code, qlogic::EXPECT_CODES[i]);
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

  assert(qlogic::recordSane(a));
  b = a; b.version = 2;
  assert(!qlogic::recordSane(b));
  b = a; b.rows[3].y = 0.0;
  assert(!qlogic::recordSane(b));
  b = a; strcpy(b.quoteDate, "2026-8-28");
  assert(!qlogic::recordSane(b));

  // 序列化 roundtrip（putBytes 語意）
  char bytes[sizeof(qlogic::QuoteRecord)];
  memcpy(bytes, &a, sizeof a);
  qlogic::QuoteRecord c;
  memcpy(&c, bytes, sizeof c);
  assert(memcmp(&a, &c, sizeof a) == 0);
  printf("blob ok\n");
}

int main() {
  testParseNum();
  testValidDate();
  testParseJson();
  testParseJsonTruncation();
  testValidateBatch();
  testCalc();
  testFormatPrice();
  testCivil();
  testFormatDateInvalid();
  testSchedule();
  testBlob();
  printf("ALL PASS\n");
  return 0;
}
