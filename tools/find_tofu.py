#!/usr/bin/env python3
"""找出（并可选修复）会显示成"方框"的字符。

原理：界面上的文字由 lv_font_cjk_24 渲染，缺字时回退到 Montserrat 24；
两个字体都没有的字形 → LVGL 画方框。本脚本解析两份字库的 cmap，
扫描源码里的字符串字面量，报出"两边都没有"的字符并给出替代建议。

用法：
    python tools/find_tofu.py            # 只报告
    python tools/find_tofu.py --fix      # 按替换表自动修掉
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CJK = os.path.join(ROOT, "src", "lv_font_cjk_24.c")
MS = os.path.join(ROOT, "lib", "lvgl", "src", "font", "lv_font_montserrat_24.c")

# 缺字 → 替代（都是两边都有的字符）
REPLACE = {
    "\u2026": "...",   # …
    "\u00b7": "|",     # ·
    "\u2014": "-",     # —
    "\u2192": "->",    # →
    "\u2212": "-",     # −
    "\u201c": "\u300c",   # “ → 「（注意：不能用 ASCII 双引号，会截断 C 字符串）
    "\u201d": "\u300d",   # ” → 」
    "\u2018": "'",
    "\u2019": "'",
}


def parse_cmap(path):
    """返回一个函数 has(cp) -> bool"""
    if not os.path.exists(path):
        print("!! 找不到字库源文件: %s" % path)
        sys.exit(1)
    src = io.open(path, encoding="utf-8", errors="ignore").read()

    # 先把所有 unicode_list 数组解析出来
    lists = {}
    for m in re.finditer(r"unicode_list_(\d+)\[\]\s*=\s*\{(.*?)\};", src, re.S):
        vals = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", m.group(2))]
        lists["unicode_list_" + m.group(1)] = vals

    entries = []
    for m in re.finditer(
        r"\.range_start\s*=\s*(\d+)\s*,\s*\.range_length\s*=\s*(\d+)\s*,"
        r".*?\.unicode_list\s*=\s*(\w+)\s*,"
        r".*?\.list_length\s*=\s*(\d+)\s*,"
        r".*?\.type\s*=\s*(\w+)",
        src, re.S):
        entries.append({
            "start": int(m.group(1)),
            "len": int(m.group(2)),
            "ulist": m.group(3),
            "list_len": int(m.group(4)),
            "type": m.group(5),
        })

    def has(cp):
        for e in entries:
            if e["ulist"] == "NULL":
                if e["start"] <= cp < e["start"] + e["len"]:
                    return True
            else:
                if cp >= e["start"] and (cp - e["start"]) in lists.get(e["ulist"], []):
                    return True
        return False

    return has, len(entries)


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    s = re.sub(r"//[^\n]*", " ", s)
    return s


def literals(path):
    """产出 (行号, 字符串内容)"""
    src = io.open(path, encoding="utf-8", errors="ignore").read()
    clean = strip_comments(src)
    # 行号按原文算：用逐个匹配的方式，并在原文里定位
    for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', clean):
        line = clean.count("\n", 0, m.start()) + 1
        text = m.group(1)
        try:
            text = text.encode("utf-8").decode("unicode_escape").encode("latin1").decode("utf-8")
        except Exception:
            pass
        yield line, text


def main():
    fix = "--fix" in sys.argv
    has_cjk, n1 = parse_cmap(CJK)
    has_ms, n2 = parse_cmap(MS)
    print("字库 cmap 条目: cjk=%d  montserrat=%d" % (n1, n2))

    def covered(ch):
        cp = ord(ch)
        return has_cjk(cp) or has_ms(cp)

    targets = []
    for dirpath, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for fn in files:
            if not fn.endswith((".c", ".cpp", ".h")):
                continue
            if fn.startswith("lv_font_") or fn == "index_html.h":
                continue
            targets.append(os.path.join(dirpath, fn))

    bad = {}
    for path in targets:
        for line, text in literals(path):
            for ch in text:
                if ord(ch) < 0x20:
                    continue
                if not covered(ch):
                    key = ch
                    bad.setdefault(key, []).append((os.path.relpath(path, ROOT), line, text[:60]))

    if not bad:
        print("没有发现会显示成方框的字符 ✔")
        return

    print("\n两边字库都没有的字符（会显示成方框）:")
    for ch, where in sorted(bad.items(), key=lambda kv: -len(kv[1])):
        sub = REPLACE.get(ch, "（无替代）")
        print("  U+%04X %r → %s   出现 %d 处" % (ord(ch), ch, sub, len(where)))
        for w in where[:6]:
            print("      %s:%d  %s" % w)

    if not fix:
        print("\n（加 --fix 自动替换）")
        return

    print("\n开始替换 …")
    changed = 0
    for path in targets:
        src = io.open(path, encoding="utf-8").read()
        new = src
        for ch, rep in REPLACE.items():
            if ch in new:
                new = new.replace(ch, rep)
        if new != src:
            io.open(path, "w", encoding="utf-8", newline="\n").write(new)
            changed += 1
            print("  已修 %s" % os.path.relpath(path, ROOT))
    print("共修改 %d 个文件" % changed)


if __name__ == "__main__":
    sys.exit(main())
