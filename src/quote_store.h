#pragma once
#include <cstdint>
#include "quote_logic.h"

// Wi-Fi 連線（spec：timeout 內未連上回 false）
bool quoteWifiBegin(uint32_t timeoutMs);
// NTP 對時（configTime + getLocalTime）；成功後 time(nullptr) 可用
bool quoteNtpSync(uint32_t timeoutMs);
// HTTPS 抓取＋解析＋欄位驗證（all-or-nothing）
// 回 0=成功（out 填入）；<0 transport 區間（不與 V_* 重疊）：
//   -10 begin 失敗、-11 非 200、-12 body 空/超 32KB 上限
//   或 quote_logic 之 V_STRUCT(-1)/V_NUMERIC(-2)/V_FORMAT(-3)/V_DATE_DIFF(-4)/V_JSON(-5)
int quoteFetch(qlogic::MarketBatch* out);
// NVS blob（單一 key quote:rec；putBytes）
bool quoteRecordLoad(qlogic::QuoteRecord* rec);   // false=無有效快取
bool quoteRecordSave(qlogic::QuoteRecord* rec, uint32_t nowUtc);
