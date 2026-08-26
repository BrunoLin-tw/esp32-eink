#pragma once
#include <Arduino.h>

#define MAX_FILES 128
#define MAX_NAME_LEN 64
#define BITMAP_BYTES 26928   // 792*272/8

// SD 上電＋掛載。回傳是否成功。
bool photoBegin();

// 掃描 /raw_photos/ 並排序（契約見 spec）：
//   >=0 張數；-1 = SD 層級錯誤；-2 = 超過 128 張；-3 = 資料夾不存在。
int photoScan();

// 排序後第 i 個檔名；越界回傳 nullptr。
const char* photoName(int i);

// 讀取第 i 檔：驗頭（magic/version/flags/w/h/reserved/檔案大小）後
// 讀入 g_bitmap。回傳 0 成功；-1 開檔失敗；-2 驗頭失敗；-3 讀取長度不符。
int photoLoad(int i);

// cleanup：關閉 handles、SD.end()、GPIO42 斷電。
void photoEnd();

extern uint8_t g_bitmap[BITMAP_BYTES];   // logical bitmap buffer（禁 stack）
extern int g_photoCount;
