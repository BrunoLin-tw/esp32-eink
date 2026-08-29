#include "quote_store.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "log.h"
#include "watchlist.h"
#include "secrets.h"
#include "twse_root_ca.h"

static Preferences g_prefs;

bool quoteWifiBegin(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(100);
    yield();
  }
  LOGF("wifi %s (%lu ms)\n", WiFi.status() == WL_CONNECTED ? "connected" : "fail",
       (unsigned long)(millis() - t0));
  return WiFi.status() == WL_CONNECTED;
}

bool quoteNtpSync(uint32_t timeoutMs) {
  configTime(qlogic::TZ_TW, 0, "pool.ntp.org", "time.nist.gov");
  struct tm tmInfo;
  bool ok = getLocalTime(&tmInfo, timeoutMs);
  LOGF("ntp %s\n", ok ? "synced" : "fail");
  return ok;
}

int quoteFetch(qlogic::MarketBatch* out) {
  WiFiClientSecure client;
  client.setCACert(TWSE_ROOT_CA_PEM);      // 釘選根憑證（禁 setInsecure）
  client.setTimeout(10);                   // core 2.x API 以秒為單位（無 setConnectTimeout）
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  String url = String("https://mis.twse.com.tw/stock/api/getStockInfo.jsp?ex_ch=") +
               QUOTE_EX_CH + "&json=1";
  if (!http.begin(client, url)) return -10;
  http.useHTTP10(true);                    // HTTP/1.0：無 chunked，原始位元組流
  http.addHeader("User-Agent", "esp32-eink-quote/1.0");
  int code = http.GET();
  if (code != 200) {
    LOGF("[fail] http %d\n", code);
    http.end();
    return -11;
  }
  int len = http.getSize();
  if (len > 32768) {
    LOGF("[fail] content-length %d over cap\n", len);
    http.end();
    return -12;
  }
  // 有界讀取（spec P0：上限必須在資料進入 String/JsonDocument 前生效）。
  // 上限語意＝payload ≤ 32768 bytes（含）：以第 32769 byte 作 probe——
  // 讀滿 32769 即確定超限→失敗；恰好 32768 合法。len==-1（無 Content-Length）亦受保護。
  static char body[32770];                 // 32769 probe 緩衝 + NUL（BSS，不佔 stack）
  int total = 0;
  WiFiClient* stream = http.getStreamPtr();
  uint32_t t0 = millis();
  while ((http.connected() || stream->available()) && total < 32769 &&
         millis() - t0 < 15000) {
    int avail = stream->available();
    if (avail > 0) {
      int want = (32769 - total) < avail ? (32769 - total) : avail;
      int n = stream->read((uint8_t*)(body + total), (size_t)want);
      if (n <= 0) break;
      total += n;
    } else {
      delay(2);
      yield();
    }
  }
  http.end();
  if (total <= 0) {
    LOGF("[fail] empty body\n");
    return -12;
  }
  if (total > 32768) {
    LOGF("[fail] body over 32KB cap\n");
    return -12;
  }
  body[total] = '\0';

  qlogic::RawBatch raw;
  int pr = qlogic::parseJsonToRaw(body, total, &raw);
  if (pr != qlogic::V_OK) {
    LOGF("[fail] parse/collect %d\n", pr);
    return pr;
  }
  int vr = qlogic::validateBatch(raw, out);
  if (vr != qlogic::V_OK) {
    LOGF("[fail] validate %d\n", vr);
    return vr;
  }
  return 0;
}

bool quoteRecordLoad(qlogic::QuoteRecord* rec) {
  g_prefs.begin("quote", true);
  size_t got = g_prefs.getBytes("rec", rec, sizeof(qlogic::QuoteRecord));
  g_prefs.end();
  if (got != sizeof(qlogic::QuoteRecord)) return false;
  if (!qlogic::recordSane(*rec)) return false;
  return true;
}

bool quoteRecordSave(qlogic::QuoteRecord* rec, uint32_t nowUtc) {
  rec->savedEpoch = nowUtc;
  g_prefs.begin("quote", false);
  size_t put = g_prefs.putBytes("rec", rec, sizeof(qlogic::QuoteRecord));
  g_prefs.end();
  return put == sizeof(qlogic::QuoteRecord);
}
