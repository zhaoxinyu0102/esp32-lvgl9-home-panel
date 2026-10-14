#!/usr/bin/env python3
"""
字体自检（防"方框"回归）

检查两件事：
 1. 界面字符串里所有非 ASCII 字符，主字库（lv_font_cjk_24）+ fallback（Montserrat）是否覆盖
 2. 有没有把中文交给"不含汉字"的字体去渲染 —— Montserrat 系列只含 ASCII/符号，
    一旦用它画中文就是方框（豆腐块），这是本项目踩过最多的坑

用法： python tools/check_font_coverage.py
"""

import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_C = os.path.join(ROOT, "src", "lv_font_cjk_24.c")
MONTS = [14, 16, 18, 20, 24, 32, 48]
MONT_RANGES = [(32, 126), (176, 63650)]

# 只有 ASCII/符号、没有汉字的字体别名
NO_CJK_FONTS = ("UI_FONT_NUM_XL", "UI_FONT_NUM_L", "UI_FONT_NUM_M", "UI_FONT_H1",
                "UI_FONT_BODY", "lv_font_montserrat_14", "lv_font_montserrat_16",
                "lv_font_montserrat_20", "lv_font_montserrat_24",
                "lv_font_montserrat_32", "lv_font_montserrat_48")


def cjk_covered():
    head = open(FONT_C, "r", encoding="utf-8", errors="ignore").read(60000)
    cov = set()
    for a, b in re.findall(r"--range\s+0x([0-9A-Fa-f]+)-0x([0-9A-Fa-f]+)", head):
        for cp in range(int(a, 16), int(b, 16) + 1):
            cov.add(chr(cp))
    m = re.search(r"--symbols\s+([^\s]+)", head)
    if m:
        cov.update(m.group(1))
    return cov


def mont_covers(ch):
    cp = ord(ch)
    return any(a <= cp <= b for a, b in MONT_RANGES)


def string_literals(path):
    src = open(path, "r", encoding="utf-8", errors="ignore").read()
    out, i, n = [], 0, len(src)
    while i < n:
        if src[i] == '"':
            i += 1
            buf = []
            while i < n and src[i] != '"':
                if src[i] == '\\' and i + 1 < n:
                    buf.append(src[i + 1])
                    i += 2
                    continue
                buf.append(src[i])
                i += 1
            out.append("".join(buf))
        i += 1
    return out


def source_files():
    files = []
    for d in ("src", "src/ui", "src/ui/screens", "src/ui/components", "src/app"):
        for ext in ("*.c", "*.cpp", "*.h"):
            files += glob.glob(os.path.join(ROOT, d, ext))
    return sorted(set(f for f in files
                      if not f.endswith("lv_font_cjk_24.c")
                      and not f.endswith("ui_images.h")))


def check_wrong_font():
    """行内同时出现"含中文的字符串字面量"和"无汉字字体"就报警"""
    bad = []
    for f in source_files():
        for ln, line in enumerate(open(f, "r", encoding="utf-8", errors="ignore"), 1):
            if not any(font in line for font in NO_CJK_FONTS):
                continue
            for lit in re.findall(r'"([^"]*)"', line):
                if any("\u4e00" <= ch <= "\u9fff" for ch in lit):
                    bad.append((os.path.relpath(f, ROOT), ln, line.strip()))
                    break
    return bad


def main():
    cov = cjk_covered()
    files = source_files()

    used, fallback_used, missing = set(), set(), {}
    for f in files:
        for lit in string_literals(f):
            for ch in lit:
                if ord(ch) < 128:
                    continue
                used.add(ch)
                if ch in cov:
                    continue
                if mont_covers(ch):
                    fallback_used.add(ch)
                    continue
                missing.setdefault(ch, set()).add(os.path.basename(f))

    print("= 1. 字符覆盖 =")
    print("  主字库覆盖字符数: %d, 界面用到非 ASCII 字符数: %d" % (len(cov), len(used)))
    print("  靠 fallback(Montserrat) 渲染的符号: %s"
          % (" ".join("U+%04X" % ord(c) for c in sorted(fallback_used)) or "(无)"))
    if missing:
        print("  [x] 会显示方框的字符:")
        for ch in sorted(missing):
            print("     U+%04X <- %s" % (ord(ch), ", ".join(sorted(missing[ch]))))
    else:
        print("  [OK] 没有无法渲染的字符")

    print("\n= 2. 字体用错（中文交给无汉字字体）=")
    bad = check_wrong_font()
    if bad:
        print("  [x] 以下位置的中文会显示成方框，请把字体换成 UI_FONT_CN:")
        for path, ln, line in bad:
            print("     %s:%d  %s" % (path, ln, line[:110]))
    else:
        print("  [OK] 没有中文被交给无汉字字体")
    return 1 if (missing or bad) else 0


if __name__ == "__main__":
    raise SystemExit(main())
