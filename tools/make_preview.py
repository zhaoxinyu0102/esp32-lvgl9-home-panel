#!/usr/bin/env python3
"""把 tools/icons 里的图标拼成一张预览图，方便人眼检查（深色底，模拟 UI 卡片）。"""

import os
from PIL import Image

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")

FILES = ["logo.png", "w_sunny.png", "w_partly.png", "w_cloudy.png",
         "w_rain.png", "w_snow.png", "w_thunder.png", "w_fog.png"]

BG = (24, 32, 58, 255)
PAD = 16

imgs = []
for f in FILES:
    p = os.path.join(D, f)
    if os.path.exists(p):
        imgs.append(Image.open(p).convert("RGBA"))

if not imgs:
    raise SystemExit("没有图标，先跑 tools/gen_icons.py")

cell_h = max(i.height for i in imgs) + PAD * 2
cell_w = sum(i.width + PAD for i in imgs) + PAD
sheet = Image.new("RGBA", (cell_w, cell_h), BG)

x = PAD
for im in imgs:
    sheet.paste(im, (x, (cell_h - im.height) // 2), im)
    x += im.width + PAD

out = os.path.join(D, "_preview.png")
sheet.save(out)
print("preview:", out, sheet.size)
