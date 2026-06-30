#!/usr/bin/env python3
"""
把 assets/images/*.png 转成 render_fb.c 用的简单 raw 格式,写到板端数据分区。

格式(小端):
  uint32 magic = 0x52414742
  uint16 w
  uint16 h
  后接 w*h*4 字节,每像素 RGBA(R,G,B,A),逐行自上而下,无 padding。
"""
import os
import struct
import sys
import collections

try:
    from PIL import Image, ImageDraw
except ImportError:
    print("需要 Pillow: pip install Pillow", file=sys.stderr)
    raise

MAGIC = 0x52414742

# (源 PNG, 目标宽, 目标高, 输出名)
# 尺寸按 480x320 游戏屏调好;高度按原图宽高比或贴图用途定。
TARGETS = [
    ("nailong.png",       160, 177, "nailong.raw"),
    # 车是车内视角:全宽,保持宽高比,锚定屏幕底部(用户确认此尺寸 OK)
    ("car.png",           480, 300, "car.raw"),
    ("nailong_head.png",  480, 320, "nailong_head.raw"),
    # 背景按 SDL draw_texture_strip 当全屏高条带画,做成宽全景便于横向视差
    ("mountain_far.png",  640, 240, "mountain_far.raw"),
    ("mountain_near.png", 640, 240, "mountain_near.raw"),
    ("cloud_layer.png",   640, 240, "cloud.raw"),
    # 树:路边装饰,AI 导出的 PNG 没有真透明(背景是烤进 RGB 的棋盘格灰/白),
    # convert 时按“近灰且亮”抠掉背景(remove_bg=True)。高宽比与世界尺寸 2000:3200 对齐。
    ("tree.png",          200, 320, "tree.raw", True),
]


def keyout_background(im):
    """把烤进 RGB 的棋盘格灰/白背景抠成透明(保留饱和的树体)。
    判据：像素接近灰色(三通道差值小)且偏亮 -> 透明。"""
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            mx, mn = max(r, g, b), min(r, g, b)
            if mx - mn < 22 and mx > 165:
                px[x, y] = (r, g, b, 0)
    return im


def write_raw(src_png, w, h, out_path, remove_bg=False):
    im = Image.open(src_png).convert("RGBA")
    if remove_bg:
        im = keyout_background(im)
        bb = im.getbbox()              # 裁掉透明边距,让内容填满贴图底部(贴地不漂浮)
        if bb:
            im = im.crop(bb)
    im = im.resize((w, h), Image.LANCZOS)
    data = im.tobytes("raw", "RGBA")
    with open(out_path, "wb") as f:
        f.write(struct.pack("<IHH", MAGIC, w, h))
        f.write(data)
    print(f"  {os.path.basename(out_path):22} {w}x{h}  {len(data)+8} bytes")


def dom_color(im):
    """最常见的不透明像素颜色(用作墙色)。"""
    small = im.convert("RGBA").resize((48, 48))
    pts = [p[:3] for p in small.getdata() if p[3] > 200]
    return collections.Counter(pts).most_common(1)[0][0] if pts else (220, 200, 120)


def make_house_front(front_png, w=160, h=200):
    """房子只用正面:抠背景 + 裁掉透明边距(底部贴地不漂浮),缩放到 w×h。不再加侧墙。"""
    front = keyout_background(Image.open(front_png).convert("RGBA"))
    bb = front.getbbox()
    if bb:
        front = front.crop(bb)
    return front.resize((w, h), Image.LANCZOS)


def write_raw_image(im, out_path):
    """把一张 RGBA PIL 图写成 raw(8 字节头 + RGBA)。"""
    im = im.convert("RGBA")
    w, h = im.size
    data = im.tobytes("raw", "RGBA")
    with open(out_path, "wb") as f:
        f.write(struct.pack("<IHH", MAGIC, w, h))
        f.write(data)
    print(f"  {os.path.basename(out_path):22} {w}x{h}  {len(data)+8} bytes")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    src_dir = os.path.join(here, "..", "assets", "images")
    out_dir = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.normpath(os.path.join(
            here, "..", "..", "..", "..", "vendor", "allwinnertech",
            "lichee", "board", "common", "data", "UDISK", "res", "racing"))

    os.makedirs(out_dir, exist_ok=True)
    print(f"src: {src_dir}")
    print(f"out: {out_dir}")
    for entry in TARGETS:
        name, w, h, out_name = entry[0], entry[1], entry[2], entry[3]
        remove_bg = entry[4] if len(entry) > 4 else False
        write_raw(os.path.join(src_dir, name), w, h,
                  os.path.join(out_dir, out_name), remove_bg=remove_bg)

    # 房子:只用正面(抠背景 + 裁边距),两种建筑各一张。
    HOUSE_TARGETS = [("house_a.jpg", "house0"), ("house_b.jpg", "house1")]
    for src, base in HOUSE_TARGETS:
        write_raw_image(make_house_front(os.path.join(src_dir, src)),
                        os.path.join(out_dir, base + ".raw"))
    print("done")


if __name__ == "__main__":
    main()
