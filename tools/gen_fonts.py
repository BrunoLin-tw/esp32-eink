#!/usr/bin/env python3
"""產生報價看板 U8g2 中文子集字型（PIL 渲染 → BDF → bdfconv → .c）。

可重現性契約（spec R5）：
- 字型來源：限 OFL 授權之 Noto Sans CJK Bold（禁用 PingFang 等系統字）
  探測路徑見 CANDIDATES；--font 可覆寫；產出檔頭記錄路徑＋sha256＋PIL/bdfconv 版本
- bdfconv 來源：u8g2 tag 2.37.1（clone --branch 2.37.1）
- glyph manifest 明列於下方常數（禁止隱式掃描）
- 產出 .c 提交 repo；僅 manifest 變更時重跑本工具
- 自檢：每 glyph 的 hex row 數必等於 BBX h；bdfconv 非零回傳即中止
"""
import argparse
import hashlib
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont
import PIL

CANDIDATES = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",       # Linux
    "/opt/homebrew/share/fonts/noto/NotoSansCJK-Bold.ttc",       # macOS Homebrew
    "/usr/local/share/fonts/noto/NotoSansCJK-Bold.ttc",          # macOS 手動安裝
]
U8G2_DIR = "/tmp/opencode/u8g2"
BDFCONV = os.path.join(U8G2_DIR, "tools/font/bdfconv/bdfconv")
OUT_C = "src/fonts_quote.c"
OUT_H = "src/fonts_quote.h"

# glyph manifest（明列；新增字元 = 修改這裡後重跑）
# 各組補 A/1(/g：bdfconv 以 glyph 'A' 定 ascent_A、'g' 定 descent_g，
# 缺這些參照字會讓 u8g2_GetAscent()/GetDescent() 回傳錯誤值（quote20/28 = 0）
G16 = "週日一二三四五六更新失敗時間未同步部分0123456789:-/ " + "A1(g"
G20 = "台積電鴻海元大灣富邦中興華金鋼05" + "A1(g"
G28 = "加權指數" + "A1(g"
MANIFEST = {16: G16, 20: G20, 28: G28}

EXPECTED_FONT_SHA = "faa5f3656a78b2e2d450d27fe8382c778bc2b6bb5ea29c986664a6a435056ceb"
EXPECTED_PILLOW = "12.3.0"
EXPECTED_U8G2 = "2.37.1"


def validate_environment(font_sha: str, pillow: str, u8g2: str) -> None:
    if font_sha != EXPECTED_FONT_SHA:
        sys.exit(f"字型 SHA256 不符：{font_sha}")
    if pillow != EXPECTED_PILLOW:
        sys.exit(f"Pillow 版本不符：{pillow}")
    if u8g2 != EXPECTED_U8G2:
        sys.exit(f"bdfconv u8g2 tag 不符：{u8g2}")


def output_banner(font_sha: str) -> str:
    return (
        "// 自動產生：tools/gen_fonts.py（勿手改）\n"
        f"// font: NotoSansCJK-Bold.ttc sha256={font_sha} (SIL OFL 1.0)\n"
        f"// PIL {EXPECTED_PILLOW}, bdfconv u8g2@{EXPECTED_U8G2}\n"
    )


def detect_u8g2_version() -> str:
    try:
        result = subprocess.run(
            ["git", "-C", U8G2_DIR, "describe", "--tags", "--exact-match", "--dirty"],
            capture_output=True, text=True,
        )
    except OSError:
        return ""
    return result.stdout.strip() if result.returncode == 0 else ""


def resolve_font(cli_font: str | None) -> str:
    if cli_font:
        if not os.path.exists(cli_font):
            sys.exit(f"--font 指定路徑不存在：{cli_font}")
        return cli_font
    for p in CANDIDATES:
        if os.path.exists(p):
            return p
    sys.exit("找不到 Noto Sans CJK Bold；請以 --font 指定 OFL 授權之字型路徑")


def build_bdf(size: int, chars: str, path: str, font_path: str) -> None:
    font = ImageFont.truetype(font_path, size)
    ascent, descent = font.getmetrics()
    # 去除重複字元（備援參照字串接可能與既有字元重複，如 G16 的 '1'）；
    # BDF 重複 ENCODING 會被 bdfconv 全數保留，產出冗餘 glyph
    chars = "".join(dict.fromkeys(chars))
    # canvas 高度需含 descent 空間，否則 baseline 下方 glyph（CJK 底緣低於 baseline）
    # 會被 canvas 底緣裁切（如 28px 權/指/數 少 1px）
    canvas_h = size + descent + 10
    lines = [
        "STARTFONT 2.1",
        f"FONT u8g2_font_quote{size}",
        f"SIZE {size} 72 72",
        f"FONTBOUNDINGBOX {size + 8} {canvas_h} 0 -{descent}",
        f"CHARS {len(chars)}",
    ]
    for ch in chars:
        cp = ord(ch)
        img = Image.new("1", (size + 8, canvas_h), 0)
        d = ImageDraw.Draw(img)
        d.text((2, 2 + ascent), ch, font=font, fill=1, anchor="ls")
        bbox = img.getbbox()
        dw = max(1, round(font.getlength(ch)))
        if bbox is None:
            # 空白類 glyph（如空格）：BBX 0 0、BITMAP 後無 hex row
            lines += [
                f"STARTCHAR u{cp:04X}",
                f"ENCODING {cp}",
                "SWIDTH 0 0",
                f"DWIDTH {dw} 0",
                "BBX 0 0 0 0",
                "BITMAP",
                "ENDCHAR",
            ]
            continue
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        stride = (w + 7) // 8
        pix = img.crop((x0, y0, x1, y1)).load()
        hexrows = []
        for ry in range(h):
            bits = "".join("1" if pix[rx, ry] else "0" for rx in range(w))
            bits = bits.ljust(stride * 8, "0")
            hexrows.append("".join(f"{int(bits[i*8:(i+1)*8], 2):02X}" for i in range(stride)))
        assert len(hexrows) == h, f"{ch!r}: hex rows {len(hexrows)} != BBX h {h}"
        yoff = (2 + ascent) - y1  # BDF y 向上為正：bitmap 底緣距 baseline
        lines += [
            f"STARTCHAR u{cp:04X}",
            f"ENCODING {cp}",
            "SWIDTH 0 0",
            f"DWIDTH {dw} 0",
            f"BBX {w} {h} {x0 - 2} {yoff}",
            "BITMAP",
            *hexrows,
            "ENDCHAR",
        ]
    lines.append("ENDFONT")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def run_bdfconv(bdf: str, out_c: str, name: str, chars: str) -> None:
    # bdfconv 預設 map 僅 ASCII 32-127（CJK 會被丟掉）、預設格式為 ucglib；
    # -m 明列 manifest codepoints、-f 1 輸出 u8g2 格式（const uint8_t + U8G2_FONT_SECTION）
    codes = ",".join(str(cp) for cp in sorted({ord(c) for c in chars}))
    r = subprocess.run([BDFCONV, bdf, "-f", "1", "-m", codes, "-o", out_c, "-n", name],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"bdfconv failed (rc={r.returncode}): {r.stderr}")


def extract_array(c_path: str) -> str:
    with open(c_path) as f:
        text = f.read()
    i = text.index("const uint8_t")
    return text[i:]


def generate(font_path: str, out_c: str, out_h: str) -> None:
    with open(font_path, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    rev = detect_u8g2_version()
    validate_environment(sha, PIL.__version__, rev)
    if not os.path.exists(BDFCONV):
        sys.exit(
            "bdfconv 不存在；先執行："
            "git clone --depth 1 --branch 2.37.1 https://github.com/olikraus/u8g2 /tmp/opencode/u8g2"
            " && make -C /tmp/opencode/u8g2/tools/font/bdfconv"
        )
    for p in (out_c, out_h):
        d = os.path.dirname(p)
        if d:
            os.makedirs(d, exist_ok=True)
    parts = []
    for size, chars in MANIFEST.items():
        bdf = f"/tmp/opencode/quote{size}.bdf"
        tmp_c = f"/tmp/opencode/quote{size}.c"
        build_bdf(size, chars, bdf, font_path)
        # 自檢：BDF 含全部 manifest glyph
        with open(bdf) as f:
            body = f.read()
        missing = [c for c in chars if f"ENCODING {ord(c)}\n" not in body]
        if missing:
            sys.exit(f"size {size} 缺 glyph: {missing}")
        run_bdfconv(bdf, tmp_c, f"u8g2_font_quote{size}", chars)
        n = len(set(chars))
        with open(tmp_c) as f:
            head = f.read()[:200]
        if f"Glyphs: {n}/{n}" not in head:
            sys.exit(f"size {size}: bdfconv 輸出 glyph 數不符（manifest {n}）")
        parts.append(extract_array(tmp_c))
        print(f"size {size}: {len(set(chars))} glyphs ok (bdfconv rc=0)")
    banner = (
        output_banner(sha)
        + "#include <stdint.h>\n"
        + "#ifndef U8G2_FONT_SECTION\n"
        + "#define U8G2_FONT_SECTION(s)\n"
        + "#endif\n"
    )
    c_text = banner + "\n".join(parts)
    externs = "".join(
        f"extern const uint8_t u8g2_font_quote{size}[];\n" for size in MANIFEST
    )
    h_text = (
        "// 自動產生對應宣告：tools/gen_fonts.py（勿手改）\n#pragma once\n#include <stdint.h>\n"
        + externs
    )
    out_c_tmp = out_c + ".tmp"
    out_h_tmp = out_h + ".tmp"
    with open(out_c_tmp, "w") as f:
        f.write(c_text)
    with open(out_h_tmp, "w") as f:
        f.write(h_text)
    os.replace(out_c_tmp, out_c)
    os.replace(out_h_tmp, out_h)
    print(f"ok -> {out_c}, {out_h}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", help="覆寫字型路徑（限 OFL 授權之 Noto CJK）")
    args = ap.parse_args()
    font_path = resolve_font(args.font)
    generate(font_path, OUT_C, OUT_H)


if __name__ == "__main__":
    main()
