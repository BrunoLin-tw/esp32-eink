#include <Arduino.h>
#include "log.h"
#include "photo_store.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("photo frame boot\n");
  if (!photoBegin()) {
    LOGF("no sd\n");
    return;
  }
  int n = photoScan();
  if (n > 0) {
    LOGF("first = %s\n", photoName(0));
    photoEnd();
  } else {
    photoEnd();
    LOGF("scan failed code=%d\n", n);
  }
}

void loop() {
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat >= 2000) {
    lastBeat = millis();
    LOGF("alive heap=%u\n", (unsigned)ESP.getFreeHeap());
  }
  delay(100);
}
