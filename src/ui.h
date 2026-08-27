#pragma once
#include <Arduino.h>

// GPIO7 上電、SPI/display init、解除深睡 hold。
void uiPowerOnInit();

// 全幅顯示 g_bitmap（白底＋黑色 drawBitmap）。
void uiShowPhoto();

// 全屏提示（NO PHOTOS / NO VALID PHOTOS / TOO MANY PHOTOS）。
void uiShowMessage(const char* title, const char* detail);

// 設定選單：title 為「SLIDESHOW」，options 為 OFF/1/5/15/30 MIN 字串陣列。
void uiMenuScreen(int cursor);

// 選單選項列 partial 更新（上下移動游標時呼叫）。
void uiMenuUpdate(int cursor);

// hibernate + GPIO7 低。
void uiHibernate();

// 深睡前控制線固定 LOW＋hold。
void uiSleepHoldPins();
