#include <Arduino.h>
#include <time.h>
#include "log.h"
#include "ui.h"
#include "quote_store.h"
#include "watchlist.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("quote fetch harness\n");
  uiInit();
  if (!quoteWifiBegin(15000)) {
    LOGF("wifi fail, halt\n");
    return;
  }
  if (!quoteNtpSync(10000)) {
    LOGF("ntp fail\n");
  }
  qlogic::MarketBatch mb;
  int r = quoteFetch(&mb);
  LOGF("fetch=%d\n", r);
  if (r == 0) {
    for (int i = 0; i < WATCH_N; i++) {
      qlogic::QuoteCalc c = qlogic::calcQuote(mb.rows[i].z, mb.rows[i].y);
      LOGF("  %s z=%.2f y=%.2f chg=%+.2f pct=%+.2f%% t=%s\n",
           mb.rows[i].code, mb.rows[i].z, mb.rows[i].y, c.chg, c.pct, mb.rows[i].t);
    }
    LOGF("date=%s quoteTime=%s\n", mb.date, mb.quoteTime);
    qlogic::QuoteRecord rec = {};
    rec.version = qlogic::BLOB_VERSION;
    for (int i = 0; i < WATCH_N; i++) rec.rows[i] = mb.rows[i];
    strcpy(rec.quoteDate, mb.date);
    strcpy(rec.quoteTime, mb.quoteTime);
    LOGF("save=%d\n", quoteRecordSave(&rec, (uint32_t)time(nullptr)));
    qlogic::QuoteRecord back;
    LOGF("load=%d\n", quoteRecordLoad(&back));

    QuoteView v = {};
    for (int i = 0; i < WATCH_N; i++) {
      v.names[i] = WATCHLIST[i].name;
      qlogic::QuoteCalc c = qlogic::calcQuote(mb.rows[i].z, mb.rows[i].y);
      v.z[i] = mb.rows[i].z;
      v.chg[i] = c.chg;
      v.pct[i] = c.pct;
    }
    qlogic::formatDateTW(mb.date, v.dateStr, sizeof v.dateStr);
    strcpy(v.timeStr, "13:33");   // harness 固定值；正式路徑於 Task 7
    uiShowQuotes(v);
  }
}

void loop() { delay(1000); }
