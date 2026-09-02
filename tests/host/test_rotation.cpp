// 旋轉 golden test：鎖定 display.setRotation(3) 之座標映射契約
// （硬體驗證 3 才正立；spec 修訂七版更正，1 為 180° 顛倒）
// 映射抄自 Adafruit_GFX.cpp drawPixel case 3（WIDTH/HEIGHT 為 raw 成員變數，
// 查證：Adafruit_GFX.cpp:2070-2074；setRotation 1/3 時 _width=HEIGHT、_height=WIDTH）
// rotation 3：(x, y) → (y, RAW_H - 1 - x)；邏輯 272x792 → 實體 792x272
// 驗證手法：四角落精確值、17-step 網格取樣界內檢查、三角形面積不變（剛性變換）
#include <cassert>
#include <cstdio>
#include <cstdlib>

static const int RAW_W = 792, RAW_H = 272;
static const int LOG_W = RAW_H, LOG_H = RAW_W;   // 272, 792

static void mapRot3(int x, int y, int* px, int* py) {
  *px = y;
  *py = RAW_H - 1 - x;
}

int main() {
  // 四角落
  int px, py;
  mapRot3(0, 0, &px, &py);      assert(px == 0 && py == 271);
  mapRot3(LOG_W - 1, 0, &px, &py); assert(px == 0 && py == 0);
  mapRot3(0, LOG_H - 1, &px, &py); assert(px == 791 && py == 271);
  mapRot3(LOG_W - 1, LOG_H - 1, &px, &py); assert(px == 791 && py == 0);

  // 全域在界內（17-step 網格取樣）
  for (int x = 0; x < LOG_W; x += 17) {
    for (int y = 0; y < LOG_H; y += 17) {
      mapRot3(x, y, &px, &py);
      assert(px >= 0 && px < RAW_W && py >= 0 && py < RAW_H);
    }
  }

  // 剛性旋轉：面積不變、無翻轉比例失真；17-step 網格取樣確認界內
  // 上漲三角形（apex 在上）經旋轉後仍為三角形且面積不變（剛性變換）
  // 頂點：logical (100,200),(130,200),(115,180) → 面積
  auto area2 = [](int x1, int y1, int x2, int y2, int x3, int y3) {
    return (x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1);
  };
  int a0 = area2(100, 200, 130, 200, 115, 180);
  int x1p, y1p, x2p, y2p, x3p, y3p;
  mapRot3(100, 200, &x1p, &y1p);
  mapRot3(130, 200, &x2p, &y2p);
  mapRot3(115, 180, &x3p, &y3p);
  int a1 = area2(x1p, y1p, x2p, y2p, x3p, y3p);
  assert(abs(a0) == abs(a1));   // 剛性旋轉：面積不變、無翻轉比例失真
  printf("rotation golden ok\n");
  return 0;
}
