#!/usr/bin/env python3
"""
把 PNG 转成 LVGL 9 的 C 数组（等价于 SquareLine 导出的 ui_img_xxx_png.c）。

输出格式：LV_COLOR_FORMAT_RGB565A8 = 先 color 平面（每行 stride = w*2 字节，小端 RGB565），
紧跟 alpha 平面（每行 w 字节）。带 alpha 的图标放卡片上不会出现方框。

用法：
    python tools/png2lvgl.py tools/icons src/ui/images
生成的每个文件里符号名 = 文件名，例如 w_sunny.png -> ui_img_w_sunny。
"""

import os
import sys

from PIL import Image

HEADER = """/**
 * @file {file}
 * @brief 由 tools/png2lvgl.py 从 {png} 生成，请勿手改（改图请改 tools/gen_icons.py 后重跑）。
 */

#ifdef __cplusplus
extern "C" {{
#endif

#include "lvgl.h"

#ifndef LV_ATTRIBUTE_IMAGE_{guard}
#define LV_ATTRIBUTE_IMAGE_{guard}
#endif

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {sym}_map[] = {{
"""

FOOTER = """}};

const lv_image_dsc_t {sym} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565A8,
    .header.flags = 0,
    .header.w = {w},
    .header.h = {h},
    .header.stride = {stride},
    .data_size = sizeof({sym}_map),
    .data = {sym}_map,
}};

#ifdef __cplusplus
}}
#endif
"""


def to_rgb565a8(img):
    """返回 (bytes, w, h, stride)"""
    img = img.convert("RGBA")
    w, h = img.size
    px = img.load()
    stride = w * 2
    color = bytearray(stride * h)
    alpha = bytearray(w * h)

    for y in range(h):
        row = y * stride
        arow = y * w
        for x in range(w):
            r, g, b, a = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            color[row + x * 2] = v & 0xFF
            color[row + x * 2 + 1] = (v >> 8) & 0xFF
            alpha[arow + x] = a
    return bytes(color) + bytes(alpha), w, h, stride


def emit(png_path, out_dir):
    name = os.path.splitext(os.path.basename(png_path))[0]
    sym = "ui_img_" + name
    guard = name.upper()
    data, w, h, stride = to_rgb565a8(Image.open(png_path))
    out_file = os.path.join(out_dir, name + ".c")

    with open(out_file, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEADER.format(file=name + ".c", png=os.path.basename(png_path),
                              guard=guard, sym=sym))
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            f.write("    " + " ".join("0x%02x," % b for b in chunk) + "\n")
        f.write(FOOTER.format(sym=sym, w=w, h=h, stride=stride))

    print("  %-28s -> %-28s %dx%d %d bytes" % (os.path.basename(png_path),
                                               os.path.basename(out_file), w, h, len(data)))
    return sym, w, h


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    src_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    pngs = sorted(f for f in os.listdir(src_dir) if f.lower().endswith(".png"))
    if not pngs:
        print("没有找到 PNG：", src_dir)
        sys.exit(1)

    print("转换 %d 张图片：" % len(pngs))
    decls = []
    for p in pngs:
        sym, w, h = emit(os.path.join(src_dir, p), out_dir)
        decls.append("LV_IMAGE_DECLARE(%s);%s// %dx%d" %
                     (sym, " " * max(1, 34 - len(sym)), w, h))

    header = os.path.join(out_dir, "ui_images.h")
    with open(header, "w", encoding="utf-8", newline="\n") as f:
        f.write("/**\n * @file ui_images.h\n * @brief 由 tools/png2lvgl.py 生成：所有图片资源的声明。\n */\n\n")
        f.write("#pragma once\n\n#include \"lvgl.h\"\n\n")
        f.write("\n".join(decls))
        f.write("\n")
    print("声明已写入", header)


if __name__ == "__main__":
    main()
