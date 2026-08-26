#!/usr/bin/env python3
"""RAW 轉檔工具：JPG/PNG -> EPFR .raw（792x272 1bpp）。

契約見 docs/superpowers/specs/2026-08-26-photo-frame-design.md：
檔頭 12B = magic EPFR + version(1) + flags(0) + w u16LE + h u16LE +
reserved(0)；payload 26928B，bit1=黑 bit0=白，99B/列 MSB first。

用法：
  raw_convert.py 圖片... --out DIR [--mode contain|cover] [--force]
  raw_convert.py --selftest --out DIR   # 產生測試樣本
"""
import argparse
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageOps

W, H = 792, 272
PAYLOAD = W * H // 8
TOTAL = 12 + PAYLOAD
MAGIC = b"EPFR"
VERSION = 1
FLAGS = 0
MODES = ("contain", "cover")
IN_EXT = (".jpg", ".jpeg", ".png")


def _normalize(img: Image.Image) -> Image.Image:
    img = ImageOps.exif_transpose(img)          # EXIF orientation
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
        img = Image.alpha_composite(bg, img)    # 透明合成白底
    return img.convert("L")


def _resize(img: Image.Image, mode: str) -> Image.Image:
    scale_w, scale_h = W / img.width, H / img.height
    if mode == "contain":
        scale = min(scale_w, scale_h)
        tw, th = round(img.width * scale), round(img.height * scale)
        img = img.resize((tw, th), Image.Resampling.LANCZOS)
        canvas = Image.new("L", (W, H), 255)    # 255 = 白（pack 後 bit0）
        canvas.paste(img, ((W - tw) // 2, (H - th) // 2))
        return canvas
    scale = max(scale_w, scale_h)
    tw, th = round(img.width * scale), round(img.height * scale)
    img = img.resize((tw, th), Image.Resampling.LANCZOS)
    left, top = (tw - W) // 2, (th - H) // 2
    return img.crop((left, top, left + W, top + H))


def _dither(img: Image.Image) -> Image.Image:
    return img.convert("1", dither=Image.Dither.FLOYDSTEINBERG)


def _pack(img: Image.Image) -> bytes:
    """1bpp：暗(<128) 設 bit=1(黑)；列優先、MSB first。輸入已為 WxH。"""
    px = img.load()
    out = bytearray()
    for y in range(H):
        for xb in range(W // 8):
            b = 0
            for bit in range(8):
                x = xb * 8 + bit
                if px[x, y] < 128:
                    b |= 0x80 >> bit
            out.append(b)
    return bytes(out)


def convert(src: str, dst: str, mode: str, force: bool) -> None:
    if os.path.exists(dst) and not force:
        raise FileExistsError(f"輸出已存在（需 --force）：{dst}")
    img = Image.open(src)
    img = _normalize(img)
    img = _resize(img, mode)
    img = _dither(img)
    payload = _pack(img)
    assert len(payload) == PAYLOAD
    header = MAGIC + bytes([VERSION, FLAGS]) + struct.pack("<HH", W, H) \
        + struct.pack("<H", 0)
    with open(dst, "wb") as f:
        f.write(header)
        f.write(payload)
    print(f"wrote {dst} ({len(header) + len(payload)} bytes, mode={mode})")


def selftest(out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    cases = [
        ("white",  Image.new("L", (W, H), 255)),   # 全幅，純反相檢查
        ("black",  Image.new("L", (W, H), 0)),     # 全幅，純反相檢查
        ("grad",   _gradient(1200, 800)),
        ("grid",   _grid(1200, 800)),
        ("label",  _label(1200, 800)),
    ]
    for name, img in cases:
        for mode in MODES:
            p = os.path.join(out_dir, f"{name}_{mode}.png")
            img.save(p)
            convert(p, p[:-4] + ".raw", mode, force=True)
    print(f"selftest wrote {len(cases) * len(MODES)} 組（PNG＋RAW）至 {out_dir}")


def _gradient(w: int, h: int) -> Image.Image:
    img = Image.new("L", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = (x * 255) // w if x < w // 2 else 255 - ((x - w // 2) * 255) // w
    return img


def _grid(w: int, h: int) -> Image.Image:
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    for x in range(0, w, 80):
        d.line([(x, 0), (x, h)], fill=0, width=2)
    for y in range(0, h, 40):
        d.line([(0, y), (w, y)], fill=0, width=2)
    return img


def _label(w: int, h: int) -> Image.Image:
    from PIL import ImageFont
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 90)
    except Exception:
        font = ImageFont.load_default()
    d.text((40, 60), "EPFR TEST 792x272", fill=0, font=font)
    d.rectangle([10, 10, w - 10, h - 10], outline=0, width=4)
    return img


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("images", nargs="*", help="輸入圖片（批次不遞迴）")
    ap.add_argument("--out", required=True)
    ap.add_argument("--mode", choices=MODES, default="contain")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        selftest(args.out)
        return
    if not args.images:
        ap.error("需要輸入圖片或 --selftest")
    os.makedirs(args.out, exist_ok=True)
    for src in args.images:
        if not os.path.isfile(src):
            print(f"[skip] 非一般檔案：{src}", file=sys.stderr)
            continue
        if os.path.splitext(src)[1].lower() not in IN_EXT:
            print(f"[skip] 副檔名不接受：{src}", file=sys.stderr)
            continue
        convert(src, os.path.join(args.out, os.path.splitext(os.path.basename(src))[0] + ".raw"),
                args.mode, args.force)


if __name__ == "__main__":
    main()
