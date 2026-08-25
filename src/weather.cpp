#include "weather.h"
#include "log.h"
#include "secrets.h"
#include <WiFi.h>

bool wifiConnect(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  LOGF("wifi connecting...\n");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(100);
    yield();
  }
  if (WiFi.status() == WL_CONNECTED) {
    LOGF("wifi connected, ip=%s rssi=%d %lu ms\n",
         WiFi.localIP().toString().c_str(), WiFi.RSSI(),
         (unsigned long)(millis() - t0));
    return true;
  }
  LOGF("[fail] wifi connect timeout (%lu ms)\n",
       (unsigned long)(millis() - t0));
  return false;
}

void wifiOff() {
  WiFi.mode(WIFI_OFF);
  LOGF("wifi off\n");
}
