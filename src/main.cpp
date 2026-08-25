#include <Arduino.h>
#include "log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "缺少 src/secrets.h：複製 secrets.h.example 並填入 Wi-Fi 憑證"
#endif

void setup() {
  Serial.begin(115200);
  delay(500);
  LOGF("weather station skeleton, ssid set=%s\n",
       strlen(WIFI_SSID) > 0 ? "yes" : "no");
}

void loop() {
  delay(100);
}
