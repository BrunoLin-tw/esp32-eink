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

static const int TZ_TW = 8 * 3600;
static const char* EXPECT_CODES[5] = {"t00", "2330", "2317", "0050", "006208"};

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
  civilFromDays(days, &c.y, reinterpret_cast<unsigned*>(&c.m),
                reinterpret_cast<unsigned*>(&c.d));
  c.hh = static_cast<int>(rem / 3600);
  c.mm2 = static_cast<int>((rem % 3600) / 60);
  c.ss = static_cast<int>(rem % 60);
  c.wday = static_cast<int>((days + 4) % 7);
  return c;
}

inline const char* weekdayHan(int wday) {
  static const char* W[7] = {"週日", "週一", "週二", "週三", "週四", "週五", "週六"};
  return W[wday % 7];
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
  int wday = static_cast<int>((days + 4) % 7);
  snprintf(buf, cap, "%c%c-%c%c %s", date[4], date[5], date[6], date[7], weekdayHan(wday));
}

inline void dateOfEpoch(uint32_t utc, int tz, char* buf, int cap) {
  Civil c = civilFromEpoch(utc, tz);
  snprintf(buf, cap, "%04d%02d%02d", c.y, c.m, c.d);
}

}  // namespace qlogic
