#include <Arduino.h>
#include "log.h"
#include "photo_store.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("photo frame boot\n");
  uiPowerOnInit();
  if (!photoBegin()) {
    uiShowMessage("NO SD", "insert card with /raw_photos");
    uiHibernate();
    return;
  }
  int n = photoScan();
  if (n < 0) {
    uiShowMessage("SCAN FAIL", "check card filesystem");
    photoEnd();
    uiHibernate();
    return;
  }
  if (n == 0) {
    uiShowMessage("NO PHOTOS", "put .raw files in /raw_photos");
    photoEnd();
    uiHibernate();
    return;
  }
  if (photoLoad(0) != 0) {
    uiShowMessage("LOAD FAIL", "photo 0 invalid");
    photoEnd();
    uiHibernate();
    return;
  }
  uiShowPhoto();
  photoEnd();
  LOGF("shown photo 0, sleeping\n");
  uiHibernate();
}

void loop() {
  delay(100);
}
