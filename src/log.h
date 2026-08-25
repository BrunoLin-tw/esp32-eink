#pragma once
#include <Arduino.h>

#define LOGF(...) do { \
  Serial.printf("[%010lu] ", (unsigned long)millis()); \
  Serial.printf(__VA_ARGS__); \
} while (0)
