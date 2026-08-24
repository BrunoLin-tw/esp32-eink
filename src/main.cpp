#include <Arduino.h>
#include <esp_sleep.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("bring-up skeleton");
}

void loop() {
  delay(100);
}
