#include "photo_store.h"
#include "log.h"
#include <SD.h>
#include <string.h>

#define SD_CS   10
#define SD_SCK  39
#define SD_MISO 13
#define SD_MOSI 40
#define SD_PWR  42
#define DIR_PATH "/raw_photos"

static SPIClass sdSPI(HSPI);
uint8_t g_bitmap[BITMAP_BYTES];
int g_photoCount = 0;
static char g_names[MAX_FILES][MAX_NAME_LEN];

static bool isRawName(const char* n) {
  if (n[0] == '.') return false;                       // 隱藏 metadata
  const char* dot = strrchr(n, '.');
  if (!dot) return false;
  return strcasecmp(dot, ".raw") == 0;
}

static int nameCmp(const void* a, const void* b) {
  return strcmp((const char*)a, (const char*)b);       // byte-wise ASCII
}

bool photoBegin() {
  pinMode(SD_PWR, OUTPUT);
  digitalWrite(SD_PWR, LOW);
  digitalWrite(SD_PWR, HIGH);                          // 使用卡片前先拉高
  delay(10);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    LOGF("[fail] SD begin\n");
    digitalWrite(SD_PWR, LOW);
    return false;
  }
  return true;
}

int photoScan() {
  File dir = SD.open(DIR_PATH);
  if (!dir || !dir.isDirectory()) {
    LOGF("[fail] open dir %s\n", DIR_PATH);
    if (dir) dir.close();
    return -3;
  }
  int n = 0;
  File f;
  while ((f = dir.openNextFile())) {
    if (f.isDirectory()) { f.close(); continue; }
    const char* name = f.name();
    if (!isRawName(name)) { f.close(); continue; }
    size_t len = strlen(name);
    if (len >= MAX_NAME_LEN) {
      LOGF("[warn] name too long, skip: %s\n", name);
      f.close();
      continue;
    }
    if (n >= MAX_FILES) {
      LOGF("[fail] too many photos\n");
      f.close();
      return -2;
    }
    memcpy(g_names[n], name, len + 1);
    n++;
    f.close();
  }
  dir.close();
  qsort(g_names, n, MAX_NAME_LEN, nameCmp);
  g_photoCount = n;
  LOGF("scan ok: %d photo(s)\n", n);
  for (int i = 0; i < n; i++) LOGF("  [%d] %s\n", i, g_names[i]);
  return n;
}

const char* photoName(int i) {
  if (i < 0 || i >= g_photoCount) return nullptr;
  return g_names[i];
}

int photoLoad(int i) {
  const char* name = photoName(i);
  if (!name) return -1;
  char path[16 + MAX_NAME_LEN];
  snprintf(path, sizeof(path), "/raw_photos/%s", name);
  File f = SD.open(path);
  if (!f) {
    LOGF("[fail] open %s\n", path);
    return -1;
  }
  if (f.size() != 12 + BITMAP_BYTES) {
    LOGF("[fail] size %s: %llu\n", path, (unsigned long long)f.size());
    f.close();
    return -2;
  }
  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12) {
    LOGF("[fail] read hdr %s\n", path);
    f.close();
    return -3;
  }
  bool hdrOk = memcmp(hdr, "EPFR", 4) == 0 && hdr[4] == 1 && hdr[5] == 0 &&
               hdr[6] == 792 % 256 && hdr[7] == 792 / 256 &&
               hdr[8] == 272 % 256 && hdr[9] == 272 / 256 &&
               hdr[10] == 0 && hdr[11] == 0;
  if (!hdrOk) {
    LOGF("[fail] header %s\n", path);
    f.close();
    return -2;
  }
  size_t got = 0;
  while (got < BITMAP_BYTES) {
    int r = f.read(g_bitmap + got, BITMAP_BYTES - got);
    if (r <= 0) break;
    got += r;
  }
  if (got != BITMAP_BYTES) {
    LOGF("[fail] read payload %s got=%u\n", path, (unsigned)got);
    f.close();
    return -3;
  }
  f.close();
  LOGF("loaded %s\n", name);
  return 0;
}

void photoEnd() {
  SD.end();
  digitalWrite(SD_PWR, LOW);   // GPIO42 斷電
  LOGF("SD ended, GPIO42 low\n");
}
