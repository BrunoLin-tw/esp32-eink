#include <Arduino.h>
#include "log.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("quote-board stub\n");
  uiInit();
}

void loop() { delay(1000); }
