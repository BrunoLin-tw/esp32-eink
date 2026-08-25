#pragma once
#include <pgmspace.h>

struct Location {
  const char* name;
  float lat;
  float lon;
};

const Location LOCATIONS[] = {
  {"Banqiao",       25.0133f, 121.4619f},
  {"Dali Taichung", 24.1016f, 120.6825f},
  {"Sapporo",       43.0618f, 141.3545f},
  {"San Francisco", 37.7749f, -122.4194f},
};
const int LOCATION_COUNT = sizeof(LOCATIONS) / sizeof(LOCATIONS[0]);
const int DEFAULT_LOCATION = 0;
