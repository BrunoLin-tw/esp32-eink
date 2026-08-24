#include <Arduino.h>
#include <esp_sleep.h>

// ---------- 日誌 ----------
#define LOGF(...) do { \
  Serial.printf("[%010lu] ", (unsigned long)millis()); \
  Serial.printf(__VA_ARGS__); \
} while (0)

// ---------- 前向宣告 ----------
void cmdInfo();

// ---------- 指令分派表 ----------
struct TestCmd {
  char key;
  const char* name;
  void (*fn)();
};

TestCmd commands[] = {
  {'i', "info", cmdInfo},
};

void printMenu() {
  Serial.println();
  Serial.println("==== bring-up test menu ====");
  for (auto& c : commands) {
    Serial.printf(" %c  %s\n", c.key, c.name);
  }
}

// ---------- 測試 ----------
void cmdInfo() {
  LOGF("chip=%s rev=%d cores=%d\n",
       ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  LOGF("flash size=%u KB speed=%u MHz\n",
       (unsigned)(ESP.getFlashChipSize() / 1024),
       (unsigned)(ESP.getFlashChipSpeed() / 1000000));
  if (psramFound()) {
    LOGF("psram size=%u bytes (OK)\n", (unsigned)ESP.getPsramSize());
  } else {
    LOGF("[warn] PSRAM NOT FOUND\n");
  }
}

// ---------- 主程式 ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    LOGF("wake from deep sleep (timer)\n");
  } else {
    LOGF("power-on/reset (cause=%d)\n", (int)cause);
  }
  printMenu();
}

void loop() {
  if (Serial.available()) {
    char c = (char)Serial.read();
    for (auto& cmd : commands) {
      if (cmd.key == c) {
        LOGF("== start '%c' (%s)\n", c, cmd.name);
        cmd.fn();
        LOGF("== end '%c'\n", c);
        printMenu();
        break;
      }
    }
  }
  delay(10);
}
