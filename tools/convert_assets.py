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

try:
    from PIL import Image
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
    # 地图“一路邮你”：北邮校徽收集物（方形）+ 北邮校门背景（全屏）
    ("nailong_bupt.png",  160, 160, "nailong_bupt.raw"),
    ("bg_bupt.jpg",       480, 320, "bg_bupt.raw"),
]


def write_raw(src_png, w, h, out_path):
    im = Image.open(src_png).convert("RGBA").resize((w, h), Image.LANCZOS)
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
    for name, w, h, out_name in TARGETS:
        write_raw(os.path.join(src_dir, name), w, h,
                  os.path.join(out_dir, out_name))
    print("done")


if __name__ == "__main__":
    main()
