#pragma once
#include <cstddef>
#include <cstring>

struct WatchItem {
  const char* exchange;
  const char* code;
  const char* name;
};

static constexpr int QUOTE_TOTAL = 9;
static constexpr int STOCK_TOTAL = 8;
static constexpr int PAGE_COUNT = 2;
static constexpr int STOCKS_PER_PAGE = 4;
static constexpr int PAGE_ROWS = 5;

static constexpr WatchItem WATCHLIST[QUOTE_TOTAL] = {
    {"tse", "t00", "加權指數"},
    {"tse", "2330", "台積電"},
    {"tse", "2317", "鴻海"},
    {"tse", "0050", "元大台灣50"},
    {"tse", "006208", "富邦台50"},
    {"tse", "1513", "中興電"},
    {"tse", "2412", "中華電"},
    {"tse", "2881", "富邦金"},
    {"tse", "2002", "中鋼"},
};

static_assert(QUOTE_TOTAL == 1 + PAGE_COUNT * STOCKS_PER_PAGE,
              "watchlist/page count mismatch");
static_assert(PAGE_ROWS == 1 + STOCKS_PER_PAGE,
              "page row count mismatch");

inline bool buildQuoteExChPrefix(char* out, size_t cap, int count) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  if (count < 0 || count > QUOTE_TOTAL) return false;
  size_t used = 0;
  for (int i = 0; i < count; ++i) {
    const char* pieces[] = {i == 0 ? "" : "|", WATCHLIST[i].exchange,
                            "_", WATCHLIST[i].code, ".tw"};
    for (const char* piece : pieces) {
      const size_t n = strlen(piece);
      if (n >= cap - used) {
        out[0] = '\0';
        return false;
      }
      memcpy(out + used, piece, n);
      used += n;
      out[used] = '\0';
    }
  }
  return true;
}

inline bool buildQuoteExCh(char* out, size_t cap) {
  return buildQuoteExChPrefix(out, cap, QUOTE_TOTAL);
}

// 過渡 shim：Task 6 刪除。
static constexpr int WATCH_N = PAGE_ROWS;
inline const char* quoteExCh() {
  static char out[96];
  static bool built = buildQuoteExChPrefix(out, sizeof out, PAGE_ROWS);
  return built ? out : "";
}
