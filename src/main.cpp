#include <Arduino.h>
#include "log.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("photo frame skeleton\n");
}

void loop() {
  delay(100);
}
