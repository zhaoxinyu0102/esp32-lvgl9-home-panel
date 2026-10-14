#!/usr/bin/env python3
"""
程序化生成智能家居 UI 的图标（不需要联网）。

做法：先在 4 倍尺寸上画，再用 LANCZOS 缩小 —— 这样边缘是抗锯齿的，比直接画小图干净得多。
输出：tools/icons/*.png（RGBA），随后由 tools/png2lvgl.py 转成 LVGL C 数组。

用法：
    python tools/gen_icons.py
"""

import math
import os

from PIL import Image, ImageDraw, ImageFilter

SS = 4                                  # 超采样倍数
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")

# 与 src/ui/ui_theme.h 里的调色板保持一致
C_ACCENT = (79, 140, 255)
C_ACCENT2 = (139, 92, 246)
C_SUN_A = (255, 231, 130)
C_SUN_B = (255, 159, 28)
C_CLOUD_A = (245, 248, 255)
C_CLOUD_B = (150, 166, 200)
C_RAIN = (79, 195, 247)
C_BOLT = (255, 215, 94)
C_WHITE = (255, 255, 255)


def canvas(size):
    img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def finish(img, size, name):
    out = img.resize((size, size), Image.LANCZOS)
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name + ".png")
    out.save(path)
    print("generated", path, out.size)


def radial_disc(draw, cx, cy, r, c_in, c_out, steps=48):
    """径向渐变圆盘（从内到外逐层画，alpha 叠加出渐变感）"""
    for i in range(steps, 0, -1):
        t = i / steps
        rr = r * t
        col = tuple(int(c_in[k] + (c_out[k] - c_in[k]) * (1 - t)) for k in range(3))
        draw.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=col + (255,))


def sun(img, cx, cy, r, rays=True):
    d = ImageDraw.Draw(img)

    if rays:
        n = 8
        for i in range(n):
            a = 2 * math.pi * i / n
            x1, y1 = cx + math.cos(a) * r * 1.28, cy + math.sin(a) * r * 1.28
            x2, y2 = cx + math.cos(a) * r * 1.62, cy + math.sin(a) * r * 1.62
            d.line([x1, y1, x2, y2], fill=C_SUN_B + (235,), width=int(r * 0.16))

    radial_disc(d, cx, cy, r, C_SUN_A, C_SUN_B)


def cloud_mask(size, blobs, base):
    """用几个椭圆 + 一个圆角矩形拼出云的形状，返回 L 通道遮罩"""
    m = Image.new("L", (size, size), 0)
    md = ImageDraw.Draw(m)
    for (cx, cy, rx, ry) in blobs:
        md.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=255)
    md.rounded_rectangle(base, radius=(base[3] - base[1]) / 2, fill=255)
    return m


def gradient_fill(size, c_top, c_bottom):
    g = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    gd = ImageDraw.Draw(g)
    for y in range(size):
        t = y / max(1, size - 1)
        col = tuple(int(c_top[k] + (c_bottom[k] - c_top[k]) * t) for k in range(3))
        gd.line([0, y, size, y], fill=col + (255,))
    return g


def cloud(img, size, blobs, base, c_top=C_CLOUD_A, c_bottom=C_CLOUD_B, alpha=255):
    m = cloud_mask(size, blobs, base)
    if alpha < 255:
        m = m.point(lambda v: int(v * alpha / 255))
    img.paste(gradient_fill(size, c_top, c_bottom), (0, 0), m)


def icon_sunny(size=96):
    img, _ = canvas(size)
    s = size * SS
    sun(img, s * 0.5, s * 0.5, s * 0.26)
    finish(img, size, "w_sunny")


def icon_partly(size=96):
    img, _ = canvas(size)
    s = size * SS
    sun(img, s * 0.36, s * 0.34, s * 0.18, rays=True)
    cloud(img, s,
          [(s * 0.46, s * 0.60, s * 0.20, s * 0.19), (s * 0.68, s * 0.64, s * 0.16, s * 0.15)],
          [s * 0.30, s * 0.72, s * 0.84, s * 0.86])
    finish(img, size, "w_partly")


def icon_cloudy(size=96):
    img, _ = canvas(size)
    s = size * SS
    cloud(img, s,
          [(s * 0.62, s * 0.42, s * 0.16, s * 0.15)],
          [s * 0.44, s * 0.48, s * 0.80, s * 0.62],
          c_top=(200, 212, 236), c_bottom=(120, 138, 176))
    cloud(img, s,
          [(s * 0.42, s * 0.58, s * 0.20, s * 0.19), (s * 0.66, s * 0.62, s * 0.16, s * 0.15)],
          [s * 0.24, s * 0.70, s * 0.82, s * 0.86])
    finish(img, size, "w_cloudy")


def icon_rain(size=96):
    img, _ = canvas(size)
    s = size * SS
    cloud(img, s,
          [(s * 0.46, s * 0.40, s * 0.22, s * 0.20), (s * 0.70, s * 0.44, s * 0.16, s * 0.15)],
          [s * 0.28, s * 0.52, s * 0.84, s * 0.66],
          c_top=(238, 243, 255), c_bottom=(160, 176, 208))
    d = ImageDraw.Draw(img)
    for i, x in enumerate((0.36, 0.52, 0.68)):
        x0 = s * x
        d.line([x0, s * 0.72, x0 - s * 0.045, s * 0.86], fill=C_RAIN + (255,), width=int(s * 0.045))
    finish(img, size, "w_rain")


def icon_snow(size=96):
    img, _ = canvas(size)
    s = size * SS
    cloud(img, s,
          [(s * 0.46, s * 0.40, s * 0.22, s * 0.20), (s * 0.70, s * 0.44, s * 0.16, s * 0.15)],
          [s * 0.28, s * 0.52, s * 0.84, s * 0.66],
          c_top=(238, 243, 255), c_bottom=(160, 176, 208))
    d = ImageDraw.Draw(img)
    for x in (0.36, 0.64):
        cx, cy, r = s * x, s * 0.80, s * 0.065
        for k in range(3):
            a = math.pi * k / 3
            d.line([cx - math.cos(a) * r, cy - math.sin(a) * r,
                    cx + math.cos(a) * r, cy + math.sin(a) * r],
                   fill=C_WHITE + (255,), width=int(s * 0.024))
    finish(img, size, "w_snow")


def icon_thunder(size=96):
    img, _ = canvas(size)
    s = size * SS
    cloud(img, s,
          [(s * 0.46, s * 0.38, s * 0.22, s * 0.20), (s * 0.70, s * 0.42, s * 0.16, s * 0.15)],
          [s * 0.28, s * 0.50, s * 0.84, s * 0.64],
          c_top=(226, 232, 248), c_bottom=(140, 154, 190))
    d = ImageDraw.Draw(img)
    bolt = [(s * 0.54, s * 0.62), (s * 0.42, s * 0.86), (s * 0.52, s * 0.86),
            (s * 0.46, s * 0.99), (s * 0.66, s * 0.74), (s * 0.55, s * 0.74), (s * 0.62, s * 0.62)]
    d.polygon(bolt, fill=C_BOLT + (255,))
    finish(img, size, "w_thunder")


def icon_fog(size=96):
    img, _ = canvas(size)
    s = size * SS
    cloud(img, s,
          [(s * 0.44, s * 0.34, s * 0.21, s * 0.19), (s * 0.68, s * 0.38, s * 0.15, s * 0.14)],
          [s * 0.28, s * 0.46, s * 0.82, s * 0.58],
          c_top=(238, 243, 255), c_bottom=(160, 176, 208))
    d = ImageDraw.Draw(img)
    for i, (x0, x1, y) in enumerate(((0.20, 0.80, 0.70), (0.28, 0.88, 0.80), (0.16, 0.68, 0.90))):
        d.line([s * x0, s * y, s * x1, s * y], fill=(190, 202, 230, 235), width=int(s * 0.045))
    finish(img, size, "w_fog")


def icon_logo(size=128):
    """品牌图标：渐变圆角方块 + 房子 + wifi 弧"""
    img, d = canvas(size)
    s = size * SS

    # 渐变底板（用遮罩裁圆角方块）
    plate = Image.new("L", (s, s), 0)
    ImageDraw.Draw(plate).rounded_rectangle([0, 0, s - 1, s - 1], radius=s * 0.24, fill=255)
    img.paste(gradient_fill(s, C_ACCENT, C_ACCENT2), (0, 0), plate)

    # 房子轮廓
    mx, my = s * 0.5, s * 0.60
    w, h = s * 0.30, s * 0.22
    d.polygon([(mx, my - h * 1.25), (mx - w, my - h * 0.15), (mx + w, my - h * 0.15)], fill=C_WHITE + (255,))
    d.rounded_rectangle([mx - w * 0.72, my - h * 0.2, mx + w * 0.72, my + h], radius=s * 0.02, fill=C_WHITE + (255,))
    d.rounded_rectangle([mx - w * 0.20, my + h * 0.25, mx + w * 0.20, my + h], radius=s * 0.015, fill=C_ACCENT + (255,))

    # wifi 弧（三条）
    for i, r in enumerate((0.13, 0.20, 0.27)):
        d.arc([mx - s * r, my - h * 1.9 - s * r * 0.6, mx + s * r, my - h * 1.9 + s * r * 0.6],
              start=200, end=340, fill=C_WHITE + (240 - i * 40,), width=int(s * 0.022))
    d.ellipse([mx - s * 0.018, my - h * 1.28, mx + s * 0.018, my - h * 1.28 + s * 0.036], fill=C_WHITE + (255,))

    finish(img, size, "logo")


# ---------------------------------------------------------------------------
# 导航图标：白色单色 + 透明背景（界面上用 image_recolor 着色成普通/激活色）
# ---------------------------------------------------------------------------

MONO = (255, 255, 255, 255)
MONO_DIM = (255, 255, 255, 210)


def icon_nav_home(size=48):
    img, d = canvas(size)
    s = size * SS
    r = s * 0.10                      # 圆角/线宽基准
    d.polygon([(s * 0.5, s * 0.14), (s * 0.09, s * 0.46), (s * 0.91, s * 0.46)], fill=MONO)
    d.rounded_rectangle([s * 0.20, s * 0.40, s * 0.80, s * 0.88], radius=r, fill=MONO)
    d.rounded_rectangle([s * 0.42, s * 0.58, s * 0.58, s * 0.88], radius=r * 0.6, fill=(0, 0, 0, 0))
    finish(img, size, "nav_home")


def icon_nav_bulb(size=48):
    img, d = canvas(size)
    s = size * SS
    d.ellipse([s * 0.26, s * 0.10, s * 0.74, s * 0.62], fill=MONO)          # 灯泡
    d.polygon([(s * 0.40, s * 0.56), (s * 0.60, s * 0.56), (s * 0.57, s * 0.70), (s * 0.43, s * 0.70)], fill=MONO)
    d.rounded_rectangle([s * 0.40, s * 0.72, s * 0.60, s * 0.80], radius=s * 0.02, fill=MONO)
    d.rounded_rectangle([s * 0.43, s * 0.84, s * 0.57, s * 0.90], radius=s * 0.02, fill=MONO_DIM)
    finish(img, size, "nav_bulb")


def icon_nav_ac(size=48):
    """雪花（空调）"""
    img, d = canvas(size)
    s = size * SS
    c = s * 0.5
    lw = int(s * 0.075)
    for k in range(3):
        a = math.pi * k / 3
        d.line([c - math.cos(a) * s * 0.38, c - math.sin(a) * s * 0.38,
                c + math.cos(a) * s * 0.38, c + math.sin(a) * s * 0.38], fill=MONO, width=lw)
    # 每根主枝上的小分叉
    for k in range(6):
        a = math.pi * k / 3
        for t in (0.62, 0.85):
            px, py = c + math.cos(a) * s * 0.38 * t, c + math.sin(a) * s * 0.38 * t
            for da in (math.pi / 4, -math.pi / 4):
                d.line([px, py, px + math.cos(a + da) * s * 0.11, py + math.sin(a + da) * s * 0.11],
                       fill=MONO, width=int(lw * 0.7))
    finish(img, size, "nav_ac")


def icon_nav_curtain(size=48):
    img, d = canvas(size)
    s = size * SS
    d.rounded_rectangle([s * 0.12, s * 0.12, s * 0.88, s * 0.20], radius=s * 0.03, fill=MONO)   # 轨道
    d.rounded_rectangle([s * 0.14, s * 0.22, s * 0.46, s * 0.90], radius=s * 0.05, fill=MONO)   # 左帘
    d.rounded_rectangle([s * 0.54, s * 0.22, s * 0.86, s * 0.90], radius=s * 0.05, fill=MONO_DIM)  # 右帘
    finish(img, size, "nav_curtain")


def icon_nav_bolt(size=48):
    img, d = canvas(size)
    s = size * SS
    d.polygon([(s * 0.58, s * 0.08), (s * 0.26, s * 0.54), (s * 0.46, s * 0.54),
               (s * 0.40, s * 0.92), (s * 0.76, s * 0.42), (s * 0.54, s * 0.42)],
              fill=MONO)
    finish(img, size, "nav_bolt")


def icon_nav_shield(size=48):
    img, d = canvas(size)
    s = size * SS
    d.polygon([(s * 0.50, s * 0.08), (s * 0.86, s * 0.22), (s * 0.86, s * 0.52),
               (s * 0.50, s * 0.92), (s * 0.14, s * 0.52), (s * 0.14, s * 0.22)], fill=MONO)
    d.line([s * 0.33, s * 0.50, s * 0.45, s * 0.62], fill=(0, 0, 0, 0), width=int(s * 0.10))
    d.line([s * 0.45, s * 0.62, s * 0.68, s * 0.38], fill=(0, 0, 0, 0), width=int(s * 0.10))
    finish(img, size, "nav_shield")


def icon_nav_cloud(size=48):
    img, d = canvas(size)
    s = size * SS
    d.ellipse([s * 0.22, s * 0.36, s * 0.58, s * 0.72], fill=MONO)
    d.ellipse([s * 0.42, s * 0.24, s * 0.76, s * 0.58], fill=MONO)
    d.rounded_rectangle([s * 0.20, s * 0.52, s * 0.80, s * 0.76], radius=s * 0.12, fill=MONO)
    finish(img, size, "nav_cloud")


def icon_nav_gear(size=48):
    img, d = canvas(size)
    s = size * SS
    c = s * 0.5
    for k in range(8):
        a = 2 * math.pi * k / 8
        d.line([c + math.cos(a) * s * 0.24, c + math.sin(a) * s * 0.24,
                c + math.cos(a) * s * 0.46, c + math.sin(a) * s * 0.46], fill=MONO, width=int(s * 0.14))
    d.ellipse([c - s * 0.30, c - s * 0.30, c + s * 0.30, c + s * 0.30], fill=MONO)
    d.ellipse([c - s * 0.13, c - s * 0.13, c + s * 0.13, c + s * 0.13], fill=(0, 0, 0, 0))
    finish(img, size, "nav_gear")


if __name__ == "__main__":
    icon_sunny()
    icon_partly()
    icon_cloudy()
    icon_rain()
    icon_snow()
    icon_thunder()
    icon_fog()
    icon_logo()
    icon_nav_home()
    icon_nav_bulb()
    icon_nav_ac()
    icon_nav_curtain()
    icon_nav_bolt()
    icon_nav_shield()
    icon_nav_cloud()
    icon_nav_gear()
    print("\n所有图标已生成到 tools/icons/")
