#pragma once
// 純邏輯（無 Arduino 依賴）：欄位驗證、漲跌計算、時間/星期、價格格式、JSON 解析
// host 測試：tests/host/test_quote_logic.cpp
//   g++ -std=c++17 -I src -I .pio/libdeps/esp32eink/ArduinoJson/src ...
#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace qlogic {

static constexpr int TZ_TW = 8 * 3600;
static const char* const EXPECT_CODES[5] = {"t00", "2330", "2317", "0050", "006208"};

enum {
  V_OK = 0,
  V_STRUCT = -1,    // 缺列/重複/代碼不符
  V_NUMERIC = -2,   // z/y 非有限數字或 y==0
  V_FORMAT = -3,    // d/t/n 格式錯誤
  V_DATE_DIFF = -4, // 五列 d 不一致
  V_JSON = -5,      // JSON 結構無效
};

struct QuoteRow {
  char code[12];
  double z;
  double y;
  char t[9];
};

struct RawQuote {
  char code[12];
  char name[32];
  char z[16];
  char y[16];
  char t[9];
  char d[9];
};

struct RawBatch {
  RawQuote rows[5];
};

struct MarketBatch {
  QuoteRow rows[5];
  char date[9];
  char quoteTime[9];
};

// Howard Hinnant civil_from_days / days_from_civil（公有領域）
inline void civilFromDays(int64_t z, int* y, unsigned* m, unsigned* d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = static_cast<unsigned>(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t yy = static_cast<int64_t>(yoe) + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned dd = doy - (153 * mp + 2) / 5 + 1;
  unsigned mm = mp < 10 ? mp + 3 : mp - 9;
  *y = static_cast<int>(yy + (mm <= 2));
  *m = mm;
  *d = dd;
}

inline int64_t daysFromCivil(int y, int m, int d) {
  y -= (m <= 2);
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = static_cast<unsigned>(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// 1970-01-01 為週四（wday=4）：days 自 epoch 起算
inline int wdayFromDays(int64_t days) { return static_cast<int>(((days + 4) % 7 + 7) % 7); }

// 真日曆驗證（round-trip）：拒絕 20260231 這類不存在的日期
inline bool validDate(const char* s) {
  if (strlen(s) != 8) return false;
  for (int i = 0; i < 8; i++)
    if (s[i] < '0' || s[i] > '9') return false;
  int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
  int mo = (s[4] - '0') * 10 + (s[5] - '0');
  int da = (s[6] - '0') * 10 + (s[7] - '0');
  if (mo < 1 || mo > 12 || da < 1) return false;
  int y2;
  unsigned m2, d2;
  civilFromDays(daysFromCivil(y, mo, da), &y2, &m2, &d2);
  return y2 == y && m2 == static_cast<unsigned>(mo) && d2 == static_cast<unsigned>(da);
}

inline bool validTime(const char* s) {
  if (strlen(s) != 8) return false;
  for (int i = 0; i < 8; i++) {
    if (i == 2 || i == 5) {
      if (s[i] != ':') return false;
    } else if (s[i] < '0' || s[i] > '9') {
      return false;
    }
  }
  int hh = (s[0] - '0') * 10 + (s[1] - '0');
  int mm = (s[3] - '0') * 10 + (s[4] - '0');
  int ss = (s[6] - '0') * 10 + (s[7] - '0');
  return hh < 24 && mm < 60 && ss < 60;
}

inline bool parseNum(const char* s, double* out) {
  if (s == nullptr || *s == '\0') return false;
  if (strcmp(s, "-") == 0) return false;
  char* end = nullptr;
  double v = strtod(s, &end);
  if (end == s || *end != '\0') return false;
  if (!std::isfinite(v)) return false;
  *out = v;
  return true;
}

// 非 V_OK 時 *out 內容不可用（可能部分寫入）
inline int validateBatch(const RawBatch& in, MarketBatch* out) {
  bool seen[5] = {false, false, false, false, false};
  char date[9] = {0};
  char latestT[9] = {0};
  for (int i = 0; i < 5; i++) {
    const RawQuote& r = in.rows[i];
    int idx = -1;
    for (int j = 0; j < 5; j++) {
      if (strcmp(r.code, EXPECT_CODES[j]) == 0) {
        idx = j;
        break;
      }
    }
    if (idx < 0 || seen[idx]) return V_STRUCT;
    seen[idx] = true;
    if (r.name[0] == '\0') return V_FORMAT;
    double z, y;
    if (!parseNum(r.z, &z)) return V_NUMERIC;
    if (!parseNum(r.y, &y)) return V_NUMERIC;
    if (y == 0.0) return V_NUMERIC;
    if (!validTime(r.t)) return V_FORMAT;
    if (!validDate(r.d)) return V_FORMAT;
    if (date[0] == '\0') {
      strncpy(date, r.d, 8);
      date[8] = '\0';
    } else if (strncmp(date, r.d, 8) != 0) {
      return V_DATE_DIFF;
    }
    if (strcmp(r.t, latestT) > 0) {
      strncpy(latestT, r.t, 8);
      latestT[8] = '\0';
    }
    strncpy(out->rows[idx].code, EXPECT_CODES[idx], 11);
    out->rows[idx].code[11] = '\0';
    out->rows[idx].z = z;
    out->rows[idx].y = y;
    strncpy(out->rows[idx].t, r.t, 8);
    out->rows[idx].t[8] = '\0';
  }
  for (int j = 0; j < 5; j++)
    if (!seen[j]) return V_STRUCT;
  strncpy(out->date, date, 8);
  out->date[8] = '\0';
  strncpy(out->quoteTime, latestT, 8);
  out->quoteTime[8] = '\0';
  return V_OK;
}

// API JSON → RawBatch（ArduinoJson v7；host 可測）。
// 契約：msgArray 恰 5 列、每列 c 必為預期代碼（未知代碼→V_STRUCT）。
// 回傳 V_OK（填入完成，順序＝API 回傳順序）或 V_JSON/V_STRUCT。
inline int parseJsonToRaw(const char* body, size_t len, RawBatch* out) {
  JsonDocument doc;
  if (deserializeJson(doc, body, len)) return V_JSON;
  JsonArray arr = doc["msgArray"];
  if (arr.isNull() || arr.size() != 5) return V_JSON;
  *out = RawBatch{};
  int k = 0;
  for (JsonObject row : arr) {
    const char* c = row["c"] | "";
    int j = -1;
    for (int i = 0; i < 5; i++) {
      if (strcmp(c, EXPECT_CODES[i]) == 0) {
        j = i;
        break;
      }
    }
    if (j < 0) return V_STRUCT;
    strncpy(out->rows[k].code, c, sizeof(out->rows[k].code) - 1);
    strncpy(out->rows[k].name, row["n"] | "", sizeof(out->rows[k].name) - 1);
    strncpy(out->rows[k].z, row["z"] | "-", sizeof(out->rows[k].z) - 1);
    strncpy(out->rows[k].y, row["y"] | "-", sizeof(out->rows[k].y) - 1);
    strncpy(out->rows[k].t, row["t"] | "", sizeof(out->rows[k].t) - 1);
    strncpy(out->rows[k].d, row["d"] | "", sizeof(out->rows[k].d) - 1);
    k++;
  }
  return V_OK;
}

struct QuoteCalc {
  double chg;
  double pct;
};

inline QuoteCalc calcQuote(double z, double y) {
  QuoteCalc c;
  c.chg = z - y;
  c.pct = (y != 0.0) ? (c.chg / y * 100.0) : 0.0;
  return c;
}

// 千分位價格：46331.45 → "46,331.45"
inline void formatPrice(double v, char* buf, int cap) {
  char raw[32];
  snprintf(raw, sizeof raw, "%.2f", v);
  const char* dot = strchr(raw, '.');
  if (!dot) {
    snprintf(buf, cap, "%.2f", v);
    return;
  }
  int intLen = (int)(dot - raw);
  int p = 0;
  for (int i = 0; i < intLen && p < cap - 1; i++) {
    if (i > 0 && (intLen - i) % 3 == 0) buf[p++] = ',';
    if (p >= cap - 1) break;
    buf[p++] = raw[i];
  }
  buf[p] = '\0';
  strncat(buf, dot, cap - 1 - p);
}

struct Civil {
  int y, m, d, hh, mm2, ss, wday;  // wday 0=Sun
};

inline Civil civilFromEpoch(uint32_t utc, int tzOffsetSec) {
  int64_t local = static_cast<int64_t>(utc) + tzOffsetSec;
  int64_t days = local / 86400;
  int64_t rem = local % 86400;
  Civil c;
  unsigned m, d;
  civilFromDays(days, &c.y, &m, &d);
  c.m = static_cast<int>(m);
  c.d = static_cast<int>(d);
  c.hh = static_cast<int>(rem / 3600);
  c.mm2 = static_cast<int>((rem % 3600) / 60);
  c.ss = static_cast<int>(rem % 60);
  c.wday = wdayFromDays(days);
  return c;
}

inline const char* weekdayHan(int wday) {
  static const char* W[7] = {"週日", "週一", "週二", "週三", "週四", "週五", "週六"};
  return W[((wday % 7) + 7) % 7];
}

inline void formatDateTW(const char* date, char* buf, int cap) {
  if (!validDate(date)) {
    snprintf(buf, cap, "\?\?-\?\?");   // \?\? 避免 trigraph
    return;
  }
  int y = (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + (date[3] - '0');
  int mo = (date[4] - '0') * 10 + (date[5] - '0');
  int da = (date[6] - '0') * 10 + (date[7] - '0');
  int64_t days = daysFromCivil(y, mo, da);
  int wday = wdayFromDays(days);
  snprintf(buf, cap, "%c%c-%c%c %s", date[4], date[5], date[6], date[7], weekdayHan(wday));
}

inline void dateOfEpoch(uint32_t utc, int tz, char* buf, int cap) {
  Civil c = civilFromEpoch(utc, tz);
  snprintf(buf, cap, "%04d%02d%02d", c.y, c.m, c.d);
}

// ---- 排程（spec 修訂三版狀態機）----
enum class MarketState { PreMarket, Trading, PostClose, Weekend };

inline MarketState marketState(uint32_t utc) {
  Civil c = civilFromEpoch(utc, TZ_TW);
  int secs = c.hh * 3600 + c.mm2 * 60 + c.ss;
  if (c.wday == 0 || c.wday == 6) return MarketState::Weekend;
  if (secs >= 9 * 3600 && secs < 13 * 3600 + 30 * 60) return MarketState::Trading;
  if (secs < 9 * 3600) return MarketState::PreMarket;
  return MarketState::PostClose;
}

inline uint32_t atLocalTime(uint32_t utcNow, int dayOffset, int hh, int minute) {
  Civil c = civilFromEpoch(utcNow, TZ_TW);
  int64_t days = daysFromCivil(c.y, c.m, c.d) + dayOffset;
  return static_cast<uint32_t>(days * 86400 + hh * 3600 + minute * 60 - TZ_TW);
}

inline uint32_t todayAt9(uint32_t utcNow) { return atLocalTime(utcNow, 0, 9, 0); }
inline uint32_t nextDayAt9(uint32_t utcNow) { return atLocalTime(utcNow, 1, 9, 0); }

inline uint32_t nextWeekdayAt9(uint32_t utcNow) {
  for (int off = 1; off <= 7; off++) {
    uint32_t t = atLocalTime(utcNow, off, 9, 0);
    Civil c = civilFromEpoch(t, TZ_TW);
    if (c.wday >= 1 && c.wday <= 5) return t;
  }
  return nextDayAt9(utcNow);  // 不可達
}

// 5 分邊界對齊＋最小安全等待 30s（spec R1）
inline uint32_t nextTradingBoundary(uint32_t utcNow) {
  uint32_t aligned = utcNow - (utcNow % 300) + 300;
  if (aligned - utcNow < 30) aligned += 300;
  return aligned;
}

// 單段睡眠上限 24h（超過由呼叫端喚醒後重算，分段）
inline uint32_t capSleep(uint32_t now, uint32_t target) {
  if (target <= now) return 1;
  uint32_t delta = target - now;
  return delta > 86400 ? 86400 : delta;
}

// ---- NVS blob（單一 versioned record，spec 修訂四版）----
static const uint32_t BLOB_VERSION = 1;

struct QuoteRecord {
  uint32_t version;
  QuoteRow rows[5];
  char quoteDate[9];      // 快取交易日（YYYYMMDD）
  char quoteTime[9];      // 5 列最新有效 t
  char lastCloseDate[9];  // 收盤定格旗標（""=未定格）
  uint32_t savedEpoch;    // 最後持久化時間（UTC）
};

inline bool recordSane(const QuoteRecord& r) {
  if (r.version != BLOB_VERSION) return false;
  if (!validDate(r.quoteDate) || !validTime(r.quoteTime)) return false;
  if (r.lastCloseDate[0] != '\0' && !validDate(r.lastCloseDate)) return false;
  for (int i = 0; i < 5; i++) {
    if (strcmp(r.rows[i].code, EXPECT_CODES[i]) != 0) return false;
    if (!std::isfinite(r.rows[i].z) || !std::isfinite(r.rows[i].y)) return false;
    if (r.rows[i].y == 0.0) return false;
    if (!validTime(r.rows[i].t)) return false;
  }
  return true;
}

// write-on-change：僅 rows/quoteDate/lastCloseDate 參與比較
// （quoteTime/savedEpoch 單獨變更不觸發寫入——spec 修訂四版）
// 逐欄位比較——QuoteRow 含 padding，禁止以 memcmp 做語意比較
// （兩側 doubles 皆由同一來源字串解析，== 比較成立）
// putBytes 二進位儲存保留；layout 變動時 BLOB_VERSION 必須遞增
inline bool recordDiffers(const QuoteRecord& a, const QuoteRecord& b) {
  for (int i = 0; i < 5; i++) {
    if (strcmp(a.rows[i].code, b.rows[i].code) != 0) return true;
    if (a.rows[i].z != b.rows[i].z) return true;
    if (a.rows[i].y != b.rows[i].y) return true;
    if (strcmp(a.rows[i].t, b.rows[i].t) != 0) return true;
  }
  if (strncmp(a.quoteDate, b.quoteDate, 9) != 0) return true;
  if (strncmp(a.lastCloseDate, b.lastCloseDate, 9) != 0) return true;
  return false;
}

}  // namespace qlogic
