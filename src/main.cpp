#include <Arduino.h>
#include <esp_sleep.h>
#include <time.h>
#include "driver/rtc_io.h"
#include "log.h"
#include "quote_logic.h"
#include "quote_store.h"
#include "ui.h"
#include "watchlist.h"

// 按鍵（active-low，docs/device-research.md）；MENU/UP/DOWN 可 EXT1 喚醒
#define BTN_MENU 2
#define BTN_UP   6
#define BTN_DOWN 4
#define BTN_PRESS 5

#define WAIT_RELEASE_MS 2000

static esp_sleep_wakeup_cause_t g_wake;
static uint64_t g_wakeMask = 0;
static bool stuckGuard = false;
RTC_DATA_ATTR qlogic::QuoteRtcState g_rtc = {0, 0};

constexpr uint64_t BUTTON_WAKE_MASK =
    (1ULL << BTN_MENU) | (1ULL << BTN_UP) | (1ULL << BTN_DOWN);

static qlogic::WakeAction wakeAction(uint64_t mask) {
  return qlogic::chooseWakeAction(
      (mask & (1ULL << BTN_MENU)) != 0,
      (mask & (1ULL << BTN_UP)) != 0,
      (mask & (1ULL << BTN_DOWN)) != 0);
}

// ---------- NVS ----------（快取載入於 quote_store；此處僅讀取輔助）
static bool loadCache(qlogic::QuoteRecord* rec) {
  return quoteRecordLoad(rec);
}

// ---------- 視圖組裝 ----------
static qlogic::ViewStatus toViewStatus(const char* status) {
  if (status == nullptr) return qlogic::ViewStatus::None;
  if (strcmp(status, "時間未同步") == 0) return qlogic::ViewStatus::TimeUnsynced;
  if (strcmp(status, "更新失敗") == 0) return qlogic::ViewStatus::UpdateFailure;
  return qlogic::ViewStatus::PartialFailure;
}

static const char* toStatusLiteral(qlogic::ViewStatus s) {
  switch (s) {
    case qlogic::ViewStatus::PartialFailure: return "部分失敗";
    case qlogic::ViewStatus::UpdateFailure: return "更新失敗";
    case qlogic::ViewStatus::TimeUnsynced: return "時間未同步";
    default: return nullptr;
  }
}

static void viewFromRecord(QuoteView* v, const qlogic::QuoteRecord& rec,
                           const char* timeStr, const char* status, uint8_t pageIndex) {
  if (!timeStr) return;
  bool visibleInvalid = false;
  for (int row = 0; row < QUOTE_ROWS; row++) {
    int idx = qlogic::quoteIndexForPageRow(pageIndex, row);
    if (idx < 0 || idx >= QUOTE_TOTAL) {
      v->names[row] = nullptr;
      v->valid[row] = false;
      v->z[row] = 0;
      v->chg[row] = 0;
      v->pct[row] = 0;
      visibleInvalid = true;
      continue;
    }
    v->names[row] = WATCHLIST[idx].name;
    v->valid[row] = rec.rows[idx].valid;
    if (!rec.rows[idx].valid) {
      v->z[row] = 0;
      v->chg[row] = 0;
      v->pct[row] = 0;
      visibleInvalid = true;
      continue;
    }
    v->z[row] = rec.rows[idx].z;
    qlogic::QuoteCalc c = qlogic::calcQuote(rec.rows[idx].z, rec.rows[idx].y);
    v->chg[row] = c.chg;
    v->pct[row] = c.pct;
  }
  qlogic::formatDateTW(rec.quoteDate, v->dateStr, sizeof v->dateStr);
  strncpy(v->timeStr, timeStr, sizeof v->timeStr - 1);
  v->timeStr[sizeof v->timeStr - 1] = '\0';
  v->pageIndex = pageIndex;
  v->pageCount = PAGE_COUNT;
  v->status = toStatusLiteral(qlogic::resolvedStatus(toViewStatus(status), visibleInvalid));
}

static void viewFromBatch(QuoteView* v, const qlogic::MarketBatch& mb,
                          const char* timeStr, const char* status, uint8_t pageIndex) {
  qlogic::QuoteRecord rec = {};
  rec.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < QUOTE_TOTAL; i++) rec.rows[i] = mb.rows[i];
  strcpy(rec.quoteDate, mb.date);
  strcpy(rec.quoteTime, mb.quoteTime);
  viewFromRecord(v, rec, timeStr, status, pageIndex);
}

static void localHHMM(uint32_t utc, char* buf, int cap) {
  qlogic::Civil c = qlogic::civilFromEpoch(utc, qlogic::TZ_TW);
  snprintf(buf, cap, "%02d:%02d", c.hh, c.mm2);
}

// "HH:MM:SS" → "HH:MM"（header 時間用）
static void hhmm(const char* t8, char* out) {
  memcpy(out, t8, 5);
  out[5] = '\0';
}

// ---------- 抓取＋持久化 ----------
struct FetchResult {
  bool ok;         // transport/JSON/驗證成功
  bool isToday;    // d == 今日（僅 ok 時有意義）
  qlogic::MarketBatch mb;
};

static FetchResult fetchUpdate(uint32_t nowUtc, const char* todayStr) {
  FetchResult fr;
  fr.ok = false;
  fr.isToday = false;
  int r = quoteFetch(&fr.mb);
  if (r != 0) return fr;
  fr.ok = true;
  fr.isToday = (strcmp(fr.mb.date, todayStr) == 0);
  // write-on-change：先載入舊 record、補回可保留旗標，再單次比較
  // （quoteTime/savedEpoch 不參與比較——spec 修訂四版）
  qlogic::QuoteRecord old;
  bool have = quoteRecordLoad(&old);
  qlogic::QuoteRecord rec = {};
  rec.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < QUOTE_TOTAL; i++) rec.rows[i] = fr.mb.rows[i];
  strcpy(rec.quoteDate, fr.mb.date);
  strcpy(rec.quoteTime, fr.mb.quoteTime);
  if (have && strcmp(old.lastCloseDate, rec.quoteDate) == 0) {
    strcpy(rec.lastCloseDate, old.lastCloseDate);   // 定格日與本次交易日相同才保留
  }
  if (!have || qlogic::recordDiffers(old, rec)) {
    quoteRecordSave(&rec, nowUtc);
    LOGF("nvs save\n");
  }
  return fr;
}

// 收盤定格寫入（PostClose 與 MENU 於收盤後成功共用）——write-on-change：
// 候選 record（含新定格旗標）與現存快取真正不同才寫入；savedEpoch
// 語意保持「最後持久化時間」
static void finalizeClose(const qlogic::MarketBatch& mb, const char* today,
                          uint32_t nowUtc) {
  qlogic::QuoteRecord old;
  bool have = quoteRecordLoad(&old);
  qlogic::QuoteRecord rec = {};
  rec.version = qlogic::BLOB_VERSION;
  for (int i = 0; i < QUOTE_TOTAL; i++) rec.rows[i] = mb.rows[i];
  strcpy(rec.quoteDate, mb.date);
  strcpy(rec.quoteTime, mb.quoteTime);
  strcpy(rec.lastCloseDate, today);
  if (!have || qlogic::recordDiffers(old, rec)) {
    quoteRecordSave(&rec, nowUtc);
    LOGF("nvs save (close)\n");
  }
}

// ---------- 睡眠 ----------
static void goToDeepSleep(uint32_t targetUtc, bool enableExt1) {
  uint32_t now = (uint32_t)time(nullptr);
  uint32_t delta;
  uint32_t finalTarget;
  if (stuckGuard) {
    // 卡鍵（spec R4）：一律 5 分鐘 timer-only——停用 EXT1 且覆寫狀態路徑
    // 目標，避免 Weekend/PostClose 長睡讓板子長時間不可達
    delta = 300;
    enableExt1 = false;
    finalTarget = now + 300;
    g_rtc.targetEpoch = finalTarget;
    LOGF("[warn] stuck: 5 min timer-only retry\n");
  } else {
    finalTarget = targetUtc;
    g_rtc.targetEpoch = targetUtc;
    delta = qlogic::capSleep(now, targetUtc);
  }
  uint64_t us = (uint64_t)delta * 1000000ULL;
  uiHibernate();
  uiSleepHoldPins();
  Serial.flush();
  delay(500);
  // s_config 開機歸零；零遮罩覆寫清除 RTC EXT1（相框已驗證做法）
  esp_sleep_enable_ext1_wakeup(0, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup(us);
  if (enableExt1) {
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    rtc_gpio_init((gpio_num_t)BTN_MENU);
    rtc_gpio_set_direction((gpio_num_t)BTN_MENU, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)BTN_MENU);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_MENU);
    rtc_gpio_init((gpio_num_t)BTN_UP);
    rtc_gpio_set_direction((gpio_num_t)BTN_UP, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)BTN_UP);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_UP);
    rtc_gpio_init((gpio_num_t)BTN_DOWN);
    rtc_gpio_set_direction((gpio_num_t)BTN_DOWN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)BTN_DOWN);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_DOWN);
  }
  LOGF("target=%lu\n", (unsigned long)finalTarget);
  LOGF("sleep %lus ext1=%d\n", (unsigned long)delta, enableExt1 ? 1 : 0);
  esp_deep_sleep_start();
}

// 依市場狀態取得長睡目標（PostClose/Weekend → 次平日 09:00；PreMarket → 當日 09:00）
static uint32_t longSleepTarget(qlogic::MarketState st, uint32_t now) {
  switch (st) {
    case qlogic::MarketState::PreMarket: return qlogic::todayAt9(now);
    case qlogic::MarketState::Trading:   return qlogic::nextTradingBoundary(now);
    default:                             return qlogic::nextWeekdayAt9(now);
  }
}

// ---------- 主流程 ----------
static bool waitButtonsReleased(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (qlogic::wakeButtonsReleased(digitalRead(BTN_MENU), digitalRead(BTN_UP),
                                    digitalRead(BTN_DOWN))) {
      return true;
    }
    delay(10);
    yield();
  }
  LOGF("[warn] buttons still held after %lu ms\n", (unsigned long)timeoutMs);
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  g_wake = esp_sleep_get_wakeup_cause();
  g_wakeMask = (g_wake == ESP_SLEEP_WAKEUP_EXT1)
                   ? esp_sleep_get_ext1_wakeup_status()
                   : 0;
  bool deepSleepWake =
      (g_wake == ESP_SLEEP_WAKEUP_TIMER || g_wake == ESP_SLEEP_WAKEUP_EXT1);
  qlogic::normalizeRtcState(&g_rtc, deepSleepWake);
  bool menuWake = (g_wakeMask & (1ULL << BTN_MENU)) != 0;
  LOGF("boot wake=%d mask=%lu page=%u target=%lu\n", (int)g_wake,
       (unsigned long)g_wakeMask, g_rtc.pageIndex,
       (unsigned long)g_rtc.targetEpoch);

  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_PRESS, INPUT_PULLUP);

  // 卡鍵防護（R4）：等待釋放；stuck → 該輪 timer-only 5 分
  // （action 強制視為 None：menuWake=false 且 cache-only 翻頁以 !stuckGuard 閘門）
  bool released = waitButtonsReleased(WAIT_RELEASE_MS);
  stuckGuard = !released;
  if (stuckGuard) {
    LOGF("[warn] stuck button\n");
    menuWake = false;
  }

  uiInit();

  // 快取翻頁（UP/DOWN EXT1 wake）：只重畫目前快取頁，不得連 Wi-Fi；
  // MENU 與純 timer wake 繼續走既有網路路徑
  if (!stuckGuard &&
      qlogic::pageWakeRequested(g_wake == ESP_SLEEP_WAKEUP_EXT1,
                                (g_wakeMask & (1ULL << BTN_MENU)) != 0,
                                (g_wakeMask & (1ULL << BTN_UP)) != 0,
                                (g_wakeMask & (1ULL << BTN_DOWN)) != 0)) {
    qlogic::WakeAction action = wakeAction(g_wakeMask);
    uint8_t prevPage = g_rtc.pageIndex;
    g_rtc.pageIndex = qlogic::changedPage(g_rtc.pageIndex, action);  // None 保持原頁
    LOGF("page cache %u->%u\n", prevPage, g_rtc.pageIndex);
    qlogic::QuoteRecord cache;
    if (loadCache(&cache)) {
      char ts[8];
      localHHMM(cache.savedEpoch, ts, sizeof ts);
      QuoteView view;
      viewFromRecord(&view, cache, ts, nullptr, g_rtc.pageIndex);
      uiShowQuotes(view);
      goToDeepSleep(qlogic::resumeTarget((uint32_t)time(nullptr), g_rtc.targetEpoch), true);
    } else {
      uiShowMessage("NO DATA", "cache unavailable");
      uint32_t retry = qlogic::noCacheRetryTarget((uint32_t)time(nullptr),
                                                   g_rtc.targetEpoch);
      goToDeepSleep(retry, true);
    }
    return;
  }

  // Wi-Fi／NTP 失敗路徑（spec 修訂三版）
  if (!quoteWifiBegin(15000)) {
    qlogic::QuoteRecord cache;
    if (loadCache(&cache)) {
      char ts[8];
      localHHMM(cache.savedEpoch, ts, sizeof ts);
      QuoteView v;
      viewFromRecord(&v, cache, ts, "更新失敗", g_rtc.pageIndex);
      uiShowQuotes(v);
    } else {
      uiShowMessage("NO WIFI", "check network");
    }
    goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
    return;
  }
  if (!quoteNtpSync(10000)) {
    qlogic::QuoteRecord cache;
    if (loadCache(&cache)) {
      char ts[8];
      localHHMM(cache.savedEpoch, ts, sizeof ts);
      QuoteView v;
      viewFromRecord(&v, cache, ts, "時間未同步", g_rtc.pageIndex);
      uiShowQuotes(v);
    } else {
      uiShowMessage("TIME NOT SYNC", "retry in 5 min");
    }
    goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
    return;
  }

  uint32_t now = (uint32_t)time(nullptr);
  char today[16];
  qlogic::dateOfEpoch(now, qlogic::TZ_TW, today, sizeof today);
  qlogic::MarketState st = qlogic::marketState(now);
  char lt[8];
  localHHMM(now, lt, sizeof lt);
  LOGF("state=%d today=%s local=%s\n", (int)st, today, lt);

  // MENU 立即更新（任何狀態；spec 修訂六版：休市日/定格結果與一般路徑同規則）
  if (menuWake) {
    FetchResult fr = fetchUpdate(now, today);
    QuoteView v;
    if (fr.ok) {
      char qt[8];
      hhmm(fr.mb.quoteTime, qt);
      viewFromBatch(&v, fr.mb, qt, nullptr, g_rtc.pageIndex);
      uiShowQuotes(v);
      if (!fr.isToday &&
          (st == qlogic::MarketState::Trading || st == qlogic::MarketState::PostClose)) {
        // 休市日手動更新 → 與一般路徑同規則：睡至隔日 09:00（避免短週期）
        goToDeepSleep(qlogic::nextDayAt9((uint32_t)time(nullptr)), !stuckGuard);
        return;
      }
      if (st == qlogic::MarketState::PostClose && fr.isToday) {
        if (qlogic::closeFinalReady((uint32_t)time(nullptr))) {
          // 順帶完成收盤定格，避免次輪重抓（spec 修訂八版：僅 13:35 後定格）
          finalizeClose(fr.mb, today, (uint32_t)time(nullptr));
        } else {
          // 13:30-13:34 手動更新：只渲染，睡至 13:35 讓一般路徑定格
          goToDeepSleep(qlogic::closeFinalAt((uint32_t)time(nullptr)), !stuckGuard);
          return;
        }
      }
      goToDeepSleep(longSleepTarget(st, (uint32_t)time(nullptr)), !stuckGuard);
      return;
    }
    qlogic::QuoteRecord cache;
    if (loadCache(&cache)) {
      char ts[8];
      localHHMM(cache.savedEpoch, ts, sizeof ts);
      viewFromRecord(&v, cache, ts, "更新失敗", g_rtc.pageIndex);
      uiShowQuotes(v);
    } else {
      uiShowMessage("FETCH FAIL", "retry in 5 min");
    }
    if (st == qlogic::MarketState::PreMarket || st == qlogic::MarketState::Weekend) {
      goToDeepSleep(longSleepTarget(st, (uint32_t)time(nullptr)), !stuckGuard);
    } else {
      goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
    }
    return;
  }

  switch (st) {
    case qlogic::MarketState::PreMarket: {
      goToDeepSleep(qlogic::todayAt9(now), !stuckGuard);
      return;
    }
    case qlogic::MarketState::Trading: {
      FetchResult fr = fetchUpdate(now, today);
      QuoteView v;
      if (fr.ok) {
        if (!fr.isToday) {
          // 假日：渲染本次回應（舊交易日資料），睡到隔日 09:00
          char qt[8];
          hhmm(fr.mb.quoteTime, qt);
          viewFromBatch(&v, fr.mb, qt, nullptr, g_rtc.pageIndex);
          uiShowQuotes(v);
          goToDeepSleep(qlogic::nextDayAt9((uint32_t)time(nullptr)), !stuckGuard);
          return;
        }
        char qt[8];
        hhmm(fr.mb.quoteTime, qt);
        viewFromBatch(&v, fr.mb, qt, nullptr, g_rtc.pageIndex);
        uiShowQuotes(v);
        goToDeepSleep(qlogic::nextTradingBoundary((uint32_t)time(nullptr)), !stuckGuard);
        return;
      }
      // 失敗：快取＋更新失敗 → 5 分重試
      qlogic::QuoteRecord cache;
      if (loadCache(&cache)) {
        char ts[8];
        localHHMM(cache.savedEpoch, ts, sizeof ts);
        viewFromRecord(&v, cache, ts, "更新失敗", g_rtc.pageIndex);
        uiShowQuotes(v);
      } else {
        uiShowMessage("FETCH FAIL", "retry in 5 min");
      }
      goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
      return;
    }
    case qlogic::MarketState::PostClose: {
      qlogic::QuoteRecord cache;
      bool have = loadCache(&cache);
      if (have && strcmp(cache.lastCloseDate, today) == 0) {
        // 已定格
        goToDeepSleep(qlogic::nextWeekdayAt9(now), !stuckGuard);
        return;
      }
      if (!qlogic::closeFinalReady(now)) {
        // 13:30-13:34（spec 修訂八版）：收盤資料同步緩衝，不抓取、不定格；
        // 畫面維持盤中最後一輪（e-ink 持續顯示），睡至 13:35 定格
        goToDeepSleep(qlogic::closeFinalAt(now), !stuckGuard);
        return;
      }
      FetchResult fr = fetchUpdate(now, today);
      QuoteView v;
      if (fr.ok && fr.isToday) {
        // 收盤定格：寫 lastCloseDate（共用 helper）
        finalizeClose(fr.mb, today, (uint32_t)time(nullptr));
        char ts[8];
        localHHMM((uint32_t)time(nullptr), ts, sizeof ts);
        viewFromBatch(&v, fr.mb, ts, nullptr, g_rtc.pageIndex);
        uiShowQuotes(v);
        goToDeepSleep(qlogic::nextWeekdayAt9((uint32_t)time(nullptr)), !stuckGuard);
        return;
      }
      if (fr.ok && !fr.isToday) {
        // 休市日（d != today）：顯示後睡至隔日 09:00（不得 5 分重試迴圈）
        if (have) {
          char ts[8];
          localHHMM(cache.savedEpoch, ts, sizeof ts);
          viewFromRecord(&v, cache, ts, nullptr, g_rtc.pageIndex);
          uiShowQuotes(v);
        } else {
          char qt[8];
          hhmm(fr.mb.quoteTime, qt);
          viewFromBatch(&v, fr.mb, qt, nullptr, g_rtc.pageIndex);
          uiShowQuotes(v);
        }
        goToDeepSleep(qlogic::nextDayAt9(now), !stuckGuard);
        return;
      }
      // 失敗 → 5 分重試
      if (have) {
        char ts[8];
        localHHMM(cache.savedEpoch, ts, sizeof ts);
        viewFromRecord(&v, cache, ts, "更新失敗", g_rtc.pageIndex);
        uiShowQuotes(v);
      } else {
        uiShowMessage("FETCH FAIL", "retry in 5 min");
      }
      goToDeepSleep((uint32_t)time(nullptr) + 300, !stuckGuard);
      return;
    }
    case qlogic::MarketState::Weekend: {
      goToDeepSleep(qlogic::nextWeekdayAt9(now), !stuckGuard);
      return;
    }
    default: {
      // 防禦：marketState 異常值不墜入 loop()（帶 Wi-Fi 空轉）
      goToDeepSleep(now + 300, !stuckGuard);
      return;
    }
  }
}

void loop() { delay(100); }
