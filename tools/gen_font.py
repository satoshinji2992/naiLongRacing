#!/usr/bin/env python3
"""
从 assets/fonts/Arial.ttf 生成点阵字体,供 render_fb 的 draw_text 使用。
输出 src/font.c + include/font.h(灰度 alpha 数组,支持抗锯齿)。
"""
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("需要 Pillow", file=sys.stderr)
    raise

CELL_W = 12
CELL_H = 18
FONT_SIZE = 16
FIRST = 32        # 空格
LAST = 126       # '~'
COUNT = LAST - FIRST + 1


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ttf = os.path.join(here, "..", "assets", "fonts", "Arial.ttf")
    src_out = os.path.join(here, "..", "src", "font.c")
    hdr_out = os.path.join(here, "..", "include", "font.h")

    font = ImageFont.truetype(ttf, FONT_SIZE)
    glyphs = []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        img = Image.new("L", (CELL_W, CELL_H), 0)
        d = ImageDraw.Draw(img)
        bbox = d.textbbox((0, 0), ch, font=font)
        gw = bbox[2] - bbox[0]
        gh = bbox[3] - bbox[1]
        ox = (CELL_W - gw) // 2 - bbox[0]
        oy = (CELL_H - gh) // 2 - bbox[1] - 1
        d.text((ox, oy), ch, font=font, fill=255)
        glyphs.append(list(img.getdata()))

    with open(hdr_out, "w") as f:
        f.write("#ifndef RACING_FONT_H\n#define RACING_FONT_H\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define FONT_CELL_W {CELL_W}\n")
        f.write(f"#define FONT_CELL_H {CELL_H}\n")
        f.write(f"#define FONT_FIRST  {FIRST}\n")
        f.write(f"#define FONT_LAST   {LAST}\n\n")
        f.write("/* 返回该字符 CELL_W*CELL_H 的 alpha 字节(行优先);不可见字符返回 NULL。 */\n")
        f.write("const uint8_t *font_glyph(int c);\n\n")
        f.write("#endif\n")

    with open(src_out, "w") as f:
        f.write('/* 由 tools/gen_font.py 从 Arial.ttf 自动生成,勿手改。 */\n')
        f.write('#include "font.h"\n\n')
        f.write(f"static const uint8_t g_font[{COUNT}][{CELL_H * CELL_W}] = {{\n")
        for g in glyphs:
            f.write("  {")
            f.write(",".join(str(b) for b in g))
            f.write("},\n")
        f.write("};\n\n")
        f.write("const uint8_t *font_glyph(int c)\n{\n")
        f.write("  if (c < FONT_FIRST || c > FONT_LAST) {\n    return 0;\n  }\n")
        f.write("  return g_font[c - FONT_FIRST];\n}\n")

    print(f"wrote {hdr_out}")
    print(f"wrote {src_out}  ({COUNT} glyphs, {CELL_W}x{CELL_H})")


if __name__ == "__main__":
    main()
