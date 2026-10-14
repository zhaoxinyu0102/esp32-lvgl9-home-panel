#!/usr/bin/env python3
"""
按 LVGL 真实的 cmap 规则，逐个确认界面用到的字符"到底有没有字形"。

为什么要单独做这个：中文字库的 CJK 部分用的是 SPARSE_TINY cmap（靠 unicode_list 稀疏表），
不能按 "gid = glyph_id_start + (码位 - range_start)" 去算，否则会得出错误结论。
本脚本解析 unicode_list / glyph_id_ofs_list，用真实规则把字符映射到字形 id，
再检查该字形是否有实际位图数据（box_w * box_h > 0）。

结论含义：
  有字形      -> 画得出来
  映射不上    -> 会显示方框（豆腐块），需要换字体或补字库
  映射上但空  -> 同样等于看不到字

用法： python tools/check_font_glyphs.py
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "src", "lv_font_cjk_24.c")

DSC_RE = re.compile(r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), "
                    r"\.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}")
CMAP_RE = re.compile(
    r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*"
    r"\.unicode_list = (\w+), \.glyph_id_ofs_list = (\w+), \.list_length = (\d+), "
    r"\.type = (\w+)", re.S)


def parse_int_list(lines, name):
    """解析形如 static const uint16_t unicode_list_2[] = {...}; 的数组"""
    if name == "NULL":
        return []
    start = None
    for i, l in enumerate(lines):
        if re.match(r"\s*(static\s+)?const\s+\w+\s+%s\[\]" % re.escape(name), l):
            start = i
            break
    if start is None:
        return []
    vals = []
    for l in lines[start:]:
        if "};" in l and vals:
            break
        vals += [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", re.sub(r"//.*", "", l))]
    return vals


def main():
    lines = open(FONT, "r", encoding="utf-8", errors="ignore").read().split("\n")

    i_dsc = next(i for i, l in enumerate(lines) if "glyph_dsc[] = {" in l)
    i_cmaps = next(i for i, l in enumerate(lines) if "cmaps[] =" in l)

    dsc = []
    for l in lines[i_dsc:i_cmaps]:
        m = DSC_RE.search(l)
        if m:
            dsc.append(tuple(int(x) for x in m.groups()))

    cmaps = []
    # cmap 的字段分两行写（.range_* 一行，.unicode_list/.type 一行），先把整块拼起来再匹配
    cmap_block = "\n".join(lines[i_cmaps:i_cmaps + 90])
    for m in CMAP_RE.finditer(cmap_block):
        cmaps.append({
            "start": int(m.group(1)), "len": int(m.group(2)), "gid0": int(m.group(3)),
            "ulist": m.group(4), "ofs": m.group(5), "list_len": int(m.group(6)),
            "type": m.group(7),
        })

    print("glyph_dsc 字形数: %d    cmaps 条目: %d" % (len(dsc), len(cmaps)))

    # 预取稀疏表
    for c in cmaps:
        c["ulist_vals"] = parse_int_list(lines, c["ulist"])
        c["ofs_vals"] = parse_int_list(lines, c["ofs"])

    def char_to_gid(ch):
        cp = ord(ch)
        for c in cmaps:
            if not (c["start"] <= cp < c["start"] + c["len"]):
                continue
            if c["type"].endswith("SPARSE_TINY"):
                # 注意：LVGL 的稀疏表里存的是"相对 range_start 的偏移"，不是绝对码位
                try:
                    idx = c["ulist_vals"].index(cp - c["start"])
                except ValueError:
                    return None
                return c["gid0"] + idx
            if c["type"].endswith("FORMAT0_FULL"):
                return c["gid0"] + c["ofs_vals"][cp - c["start"]]
            return c["gid0"] + (cp - c["start"])
        return None

    # 收集界面用到的字符
    files = []
    for d in ("src", "src/ui", "src/ui/screens", "src/ui/components", "src/app"):
        for ext in ("*.c", "*.cpp", "*.h"):
            files += glob.glob(os.path.join(ROOT, d, ext))

    used = set()
    for f in sorted(set(files)):
        if f.endswith("lv_font_cjk_24.c") or f.endswith("ui_images.h"):
            continue
        src = open(f, encoding="utf-8", errors="ignore").read()
        # 去掉注释，跳过 strstr/strcmp 这类只做匹配、不上屏的字面量
        src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
        src = re.sub(r"//[^\n]*", "", src)
        for m in re.finditer(r'"(?:[^"\\]|\\.)*"', src):
            ctx = src[max(0, m.start() - 28):m.start()]
            if re.search(r"(strstr|strcmp|strncmp|strchr)\s*\([^()]*$", ctx):
                continue
            lit = m.group(0)[1:-1]
            for ch in lit:
                if ord(ch) > 127:
                    used.add(ch)

    ok, no_gid, empty = [], [], []
    for ch in sorted(used):
        gid = char_to_gid(ch)
        if gid is None or gid >= len(dsc):
            no_gid.append(ch)
            continue
        bi, adv, bw, bh, ox, oy = dsc[gid]
        (ok if bw > 0 and bh > 0 else empty).append(ch)

    print("界面非 ASCII 字符: %d" % len(used))
    print("  有字形(正常显示): %d" % len(ok))
    print("  映射不到字形(主字库无此字): %d  %s"
          % (len(no_gid), " ".join("U+%04X" % ord(c) for c in no_gid[:30])))
    print("  映射到空字形: %d  %s" % (len(empty), "".join(empty[:30])))

    # 这些"映射不到"的符号由 Montserrat fallback 兜底，单独列出来确认它是 ASCII/符号区
    print("\n提示：上面“映射不到”的字符若属于 ° · … — “ ” − 这类符号，"
          "会由 .fallback = &lv_font_montserrat_24 渲染，不算方框。")
    cjk_missing = [c for c in no_gid if "\u4e00" <= c <= "\u9fff"]
    if cjk_missing:
        print("!! 有 %d 个汉字没有字形（会显示方框）: %s"
              % (len(cjk_missing), "".join(cjk_missing)))
        return 1
    print("汉字全部有字形 OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
