#!/usr/bin/env python3
"""產生天氣圖示點陣標頭 src/icons_bitmaps.h。

用法：/tmp/opencode/pio-venv/bin/python tools/gen_icons.py
輸出：src/icons_bitmaps.h（勿手改；調整樣式請改本檔再重跑）
"""
from PIL import Image, ImageDraw
import os

SIZES = [64, 32]
ORDER = ["sun", "partly", "cloudy", "fog", "drizzle",
         "rain", "snow", "showers", "sleet", "thunder"]
OUT = os.path.join(os.path.dirname(__file__), "..", "src", "icons_bitmaps.h")


def new_canvas(s):
    img = Image.new("L", (s, s), 0)
    return img, ImageDraw.Draw(img)


def cloud_mask(img, d, s):
    """以聯集橢圓畫實心雲剪影。"""
    m = Image.new("L", (s, s), 0)
    md = ImageDraw.Draw(m)
    md.ellipse([0.14*s, 0.40*s, 0.46*s, 0.68*s], fill=255)
    md.ellipse([0.30*s, 0.26*s, 0.74*s, 0.66*s], fill=255)
    md.ellipse([0.56*s, 0.42*s, 0.86*s, 0.68*s], fill=255)
    md.rectangle([0.14*s, 0.52*s, 0.86*s, 0.62*s], fill=255)
    black = Image.new("L", (s, s), 255)
    img.paste(black, (0, 0), m)


def draw_sun(d, cx, cy, r, s):
    w = max(2, s // 28)
    d.ellipse([cx-r, cy-r, cx+r, cy+r], outline=255, width=w)
    import math
    for i in range(8):
        ang = i * math.pi / 4
        r1, r2 = r + s*0.06, r + s*0.14
        d.line([cx + math.cos(ang)*r1, cy + math.sin(ang)*r1,
                cx + math.cos(ang)*r2, cy + math.sin(ang)*r2],
               fill=255, width=w)


def drop(d, x, y, ln, s):
    w = max(2, s // 26)
    d.line([x, y, x - ln*0.25, y + ln], fill=255, width=w)


def gen_sun(s):
    img, d = new_canvas(s)
    draw_sun(d, 0.5*s, 0.5*s, 0.17*s, s)
    return img


def gen_partly(s):
    img, d = new_canvas(s)
    draw_sun(d, 0.36*s, 0.30*s, 0.11*s, s)
    # 雲右下偏移並縮小，讓太陽露出
    sub = Image.new("L", (s, s), 0)
    sd, _ = new_canvas(s)
    cloud_mask(sd, None, s)
    sub = sd.crop([int(0.05*s), int(0.18*s), s, s])
    img.paste(255, (int(0.02*s), int(0.16*s)), sub)
    return img


def gen_cloudy(s):
    img, d = new_canvas(s)
    cloud_mask(img, d, s)
    return img


def gen_fog(s):
    img, d = new_canvas(s)
    cloud_mask(img, d, s)
    w = max(2, s // 28)
    for i, yy in enumerate([0.76, 0.84, 0.92]):
        x0 = 0.18*s if i == 1 else 0.26*s
        x1 = 0.82*s if i == 1 else 0.74*s
        d.line([x0, yy*s, x1, yy*s], fill=255, width=w)
    return img


def _rain_base(drops_n, dash_len):
    def f(s):
        img, d = new_canvas(s)
        cloud_mask(img, d, s)
        for i in range(drops_n):
            x = (0.30 + i * (0.42 / max(1, drops_n - 1))) * s
            drop(d, x, 0.70*s, dash_len*s, s)
        return img
    return f


def gen_snow(s):
    img, d = new_canvas(s)
    cloud_mask(img, d, s)
    r = 0.035*s
    for i, xx in enumerate([0.34, 0.48, 0.62]):
        yy = 0.74 if i % 2 == 0 else 0.84
        d.ellipse([xx*s - r, yy*s - r, xx*s + r, yy*s + r], fill=255)
    return img


def gen_sleet(s):
    img, d = new_canvas(s)
    cloud_mask(img, d, s)
    drop(d, 0.36*s, 0.70*s, 0.12*s, s)
    drop(d, 0.60*s, 0.70*s, 0.12*s, s)
    r = 0.035*s
    for xx, yy in [(0.48, 0.86), (0.68, 0.84)]:
        d.ellipse([xx*s - r, yy*s - r, xx*s + r, yy*s + r], fill=255)
    return img


def gen_thunder(s):
    img, d = new_canvas(s)
    cloud_mask(img, d, s)
    pts = [(0.54, 0.52), (0.40, 0.72), (0.50, 0.72),
           (0.42, 0.94), (0.64, 0.66), (0.53, 0.66), (0.62, 0.52)]
    d.polygon([(x*s, y*s) for x, y in pts], fill=255)
    return img


GENS = {
    "sun": gen_sun,
    "partly": gen_partly,
    "cloudy": gen_cloudy,
    "fog": gen_fog,
    "drizzle": _rain_base(3, 0.10),
    "rain": _rain_base(4, 0.16),
    "showers": _rain_base(6, 0.18),
    "snow": gen_snow,
    "sleet": gen_sleet,
    "thunder": gen_thunder,
}


def to_bytes(img):
    """列優先、每 byte 8 像素 MSB 在前（相容 Adafruit GFX drawBitmap）。"""
    w, h = img.size
    px = img.load()
    out = []
    for y in range(h):
        for xb in range((w + 7) // 8):
            b = 0
            for bit in range(8):
                x = xb * 8 + bit
                if x < w and px[x, y] > 127:
                    b |= 0x80 >> bit
            out.append(b)
    return out


def main():
    lines = [
        "// 自動產生，勿手改：tools/gen_icons.py",
        "#pragma once",
        "#include <pgmspace.h>",
        "",
        "struct IconBmp {",
        "  uint8_t w;",
        "  uint8_t h;",
        "  const uint8_t* data;",
        "};",
        "",
    ]
    for name in ORDER:
        for s in SIZES:
            data = to_bytes(GENS[name](s))
            lines.append(f"const uint8_t icon_{name}_{s}[] PROGMEM = {{")
            for i in range(0, len(data), 12):
                seg = ",".join(f"0x{b:02X}" for b in data[i:i+12])
                lines.append(f"  {seg},")
            lines.append("};")
            lines.append("")
    for s in SIZES:
        entries = ", ".join(f"icon_{n}_{s}" for n in ORDER)
        lines.append(f"const IconBmp ICON_SET_{s}[10] = {{")
        for n in ORDER:
            lines.append(f"  {{{s}, {s}, icon_{n}_{s}}},")
        lines.append("};")
        lines.append("")
    with open(os.path.abspath(OUT), "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {os.path.abspath(OUT)}")


if __name__ == "__main__":
    main()
