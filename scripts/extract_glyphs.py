"""
extract_glyphs.py — 從壓縮完整 header 抽 glyph 產出瘦身版字型 header

stage31 字體系統：取代 fontconvert.py 跨平台 build pipeline。

流程：
  1. 解壓 source_han_sans_tc_NN_full.h.xz（一次性產的完整字集 header）
  2. 解析三大 table：bitmaps / glyphs / intervals
  3. 依 ui_charset.txt 抽出需要的 glyph 重新組三大 table
  4. 重建 offset / index、輸出瘦身版 .h

優點（vs fontconvert.py + freetype）：
  - 不需要 freetype-py（跨 OS 穩定，Windows / WSL / Linux / macOS 都能跑）
  - 速度快（純文字切 byte，10x 以上比解析 OTF 快）
  - 輸出 deterministic（不會因 OS / Python 版本不同產生不同 diff）
  - 無 OTF 解析的中文路徑問題

限制：
  - 字體大小、bpp、metrics 固定 — 完整 header 是哪個大小，就只能產同樣大小
  - 完整 header 沒包的字（11658 字以外）抽不出來
  - 換字型 / 字號需要重跑 fontconvert.py 重做完整 header

用法：
  python scripts/extract_glyphs.py <full_header.h.xz> <charset.txt> <output.h>
"""
import argparse
import lzma
import re
import sys
from pathlib import Path


# 完整 header 結構（fontconvert.py 產出格式）：
#   static const uint8_t {name}Bitmaps[N] = { 0xXX, 0xXX, ... };
#   static const EpdGlyph {name}Glyphs[] = {
#       { width, height, advanceX, left, top, dataLength, dataOffset }, // <utf-8 char>
#       ...
#   };
#   static const EpdUnicodeInterval {name}Intervals[] = {
#       { 0xXXXX, 0xXXXX, 0xXXXX },  // first, last, offset
#       ...
#   };
#   static const EpdFontData {name} = {
#       {name}Bitmaps,
#       {name}Glyphs,
#       {name}Intervals,
#       intervalCount,
#       advanceY,
#       ascender,
#       descender,
#       is2Bit,
#   };

BITMAP_PATTERN = re.compile(
    r"static const uint8_t (\w+)Bitmaps\[\d+\]\s*=\s*\{([^}]*)\};",
    re.DOTALL,
)
GLYPH_LINE_PATTERN = re.compile(
    r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}"
)
INTERVAL_LINE_PATTERN = re.compile(
    r"\{\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*\}"
)
FONT_DATA_PATTERN = re.compile(
    r"static const EpdFontData (\w+)\s*=\s*\{\s*\w+Bitmaps\s*,\s*\w+Glyphs\s*,"
    r"\s*\w+Intervals\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(true|false)\s*,?\s*\};",
    re.DOTALL,
)


def parse_full_header(content):
    """解析完整 header，回傳 (name, bitmaps_bytes, glyphs_list, intervals_list, font_meta_dict)"""
    # 1. Bitmaps
    bm_match = BITMAP_PATTERN.search(content)
    if not bm_match:
        raise ValueError("Could not find Bitmaps array")
    full_name = bm_match.group(1)  # e.g. "source_han_sans_tc_10_full"
    bm_body = bm_match.group(2)
    # 解 hex bytes
    bitmaps = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", bm_body))

    # 2. Glyphs — 截取 Glyphs[] 區塊
    glyphs_start = content.find(f"{full_name}Glyphs[]")
    if glyphs_start == -1:
        raise ValueError("Could not find Glyphs array")
    glyphs_block_start = content.find("{", glyphs_start) + 1
    # 找對應的 };（注意 glyph 的 {} 不算）
    # 簡單做法：找下一個 "};\n"，但需要避免 inline { } 干擾 → 用行尾的 "};"
    glyphs_end_marker = content.find("\n};", glyphs_block_start)
    glyphs_body = content[glyphs_block_start:glyphs_end_marker]
    glyphs = []
    for m in GLYPH_LINE_PATTERN.finditer(glyphs_body):
        glyphs.append({
            "width": int(m.group(1)),
            "height": int(m.group(2)),
            "advance": int(m.group(3)),
            "left": int(m.group(4)),
            "top": int(m.group(5)),
            "dataLength": int(m.group(6)),
            "dataOffset": int(m.group(7)),
        })

    # 3. Intervals
    intervals_start = content.find(f"{full_name}Intervals[]")
    if intervals_start == -1:
        raise ValueError("Could not find Intervals array")
    intervals_block_start = content.find("{", intervals_start) + 1
    intervals_end_marker = content.find("\n};", intervals_block_start)
    intervals_body = content[intervals_block_start:intervals_end_marker]
    intervals = []
    for m in INTERVAL_LINE_PATTERN.finditer(intervals_body):
        intervals.append({
            "first": int(m.group(1), 16),
            "last": int(m.group(2), 16),
            "offset": int(m.group(3), 16),
        })

    # 4. Font meta（FontData struct）
    meta_match = FONT_DATA_PATTERN.search(content)
    if not meta_match:
        raise ValueError("Could not find EpdFontData struct")
    font_meta = {
        "intervalCount": int(meta_match.group(2)),
        "advanceY": int(meta_match.group(3)),
        "ascender": int(meta_match.group(4)),
        "descender": int(meta_match.group(5)),
        "is2Bit": meta_match.group(6) == "true",
    }

    return full_name, bitmaps, glyphs, intervals, font_meta


def codepoint_to_glyph_index(cp, intervals):
    """從 intervals 找 codepoint 對應的 glyph index，找不到回 -1"""
    for itv in intervals:
        if itv["first"] <= cp <= itv["last"]:
            return itv["offset"] + (cp - itv["first"])
    return -1


def extract_subset(bitmaps, glyphs, intervals, wanted_codepoints):
    """
    從完整字集抽出 wanted_codepoints 對應的 glyph，重組三大 table。

    Returns: (new_bitmaps, new_glyphs, new_intervals)
    """
    # 1. 找出每個 wanted_cp 對應的原 glyph index（若不存在則跳過）
    cp_to_old_idx = {}
    for cp in sorted(wanted_codepoints):
        idx = codepoint_to_glyph_index(cp, intervals)
        if idx >= 0:
            cp_to_old_idx[cp] = idx

    # 2. 按 cp 順序新建 glyph list + 重算 bitmap offset
    new_glyphs = []
    new_bitmaps = bytearray()
    cp_to_new_idx = {}

    for cp in sorted(cp_to_old_idx.keys()):
        old_idx = cp_to_old_idx[cp]
        old_glyph = glyphs[old_idx]
        # 從原 bitmaps 切出該 glyph 的 bitmap bytes
        old_off = old_glyph["dataOffset"]
        old_len = old_glyph["dataLength"]
        glyph_bytes = bitmaps[old_off : old_off + old_len]

        # 新 offset = 當前累積長度
        new_off = len(new_bitmaps)
        new_bitmaps.extend(glyph_bytes)

        new_glyph = dict(old_glyph)
        new_glyph["dataOffset"] = new_off
        new_glyphs.append(new_glyph)
        cp_to_new_idx[cp] = len(new_glyphs) - 1

    # 3. 重建 intervals — 連續 cp 合併
    new_intervals = []
    cps = sorted(cp_to_new_idx.keys())
    if cps:
        start = cps[0]
        end = cps[0]
        base_idx = cp_to_new_idx[start]
        for cp in cps[1:]:
            if cp == end + 1 and cp_to_new_idx[cp] == cp_to_new_idx[end] + 1:
                end = cp
            else:
                new_intervals.append({"first": start, "last": end, "offset": base_idx})
                start = cp
                end = cp
                base_idx = cp_to_new_idx[cp]
        new_intervals.append({"first": start, "last": end, "offset": base_idx})

    return bytes(new_bitmaps), new_glyphs, new_intervals


def format_bitmaps(bitmaps, var_name, indent=4):
    """格式化 bitmaps 成 0xXX 陣列字串"""
    lines = []
    bytes_per_line = 16
    for i in range(0, len(bitmaps), bytes_per_line):
        chunk = bitmaps[i : i + bytes_per_line]
        line = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(" " * indent + line + ",")
    if lines:
        # 最後一行去掉尾巴逗號
        lines[-1] = lines[-1].rstrip(",")
    body = "\n".join(lines)
    return f"static const uint8_t {var_name}Bitmaps[{len(bitmaps)}] = {{\n{body}\n}};\n"


def format_glyphs(glyphs, var_name, indent=4):
    """格式化 glyphs 成 EpdGlyph 陣列"""
    lines = []
    for g in glyphs:
        lines.append(
            " " * indent
            + f"{{ {g['width']}, {g['height']}, {g['advance']}, {g['left']}, {g['top']}, {g['dataLength']}, {g['dataOffset']} }},"
        )
    body = "\n".join(lines)
    return f"static const EpdGlyph {var_name}Glyphs[] = {{\n{body}\n}};\n"


def format_intervals(intervals, var_name, indent=4):
    """格式化 intervals 成 EpdUnicodeInterval 陣列"""
    lines = []
    for itv in intervals:
        lines.append(
            " " * indent
            + f"{{ 0x{itv['first']:X}, 0x{itv['last']:X}, 0x{itv['offset']:X} }},"
        )
    body = "\n".join(lines)
    return f"static const EpdUnicodeInterval {var_name}Intervals[] = {{\n{body}\n}};\n"


def format_font_data(var_name, intervals_count, meta):
    is2bit_str = "true" if meta["is2Bit"] else "false"
    return (
        f"static const EpdFontData {var_name} = {{\n"
        f"    {var_name}Bitmaps,\n"
        f"    {var_name}Glyphs,\n"
        f"    {var_name}Intervals,\n"
        f"    {intervals_count},\n"
        f"    {meta['advanceY']},\n"
        f"    {meta['ascender']},\n"
        f"    {meta['descender']},\n"
        f"    {is2bit_str},\n"
        f"}};\n"
    )


def build_header(out_name, bitmaps, glyphs, intervals, meta):
    parts = [
        f"/**\n"
        f" * generated by extract_glyphs.py (stage31 純 Python build)\n"
        f" * name: {out_name}\n"
        f" * glyphs: {len(glyphs)}\n"
        f" * bitmap bytes: {len(bitmaps)}\n"
        f" */\n"
        f"#pragma once\n"
        f'#include "EpdFontData.h"\n\n',
        format_bitmaps(bitmaps, out_name),
        "\n",
        format_glyphs(glyphs, out_name),
        "\n",
        format_intervals(intervals, out_name),
        "\n",
        format_font_data(out_name, len(intervals), meta),
    ]
    return "".join(parts)


def read_charset(charset_path):
    """讀字集檔，回傳 set of codepoints"""
    text = Path(charset_path).read_text(encoding="utf-8")
    return set(ord(c) for c in text if not c.isspace() or c == " ")


def main():
    parser = argparse.ArgumentParser(description="從完整 header 抽 glyph 產出瘦身版 header")
    parser.add_argument("full_header", help="完整 header 路徑（.h 或 .h.xz）")
    parser.add_argument("charset_file", help="目標字集 .txt")
    parser.add_argument("output_header", help="輸出瘦身版 .h 路徑")
    parser.add_argument("--out-name", default=None, help="輸出字體 var 名稱（預設從輸出檔名推）")
    args = parser.parse_args()

    full_path = Path(args.full_header)
    charset_path = Path(args.charset_file)
    out_path = Path(args.output_header)
    out_name = args.out_name or out_path.stem  # e.g. source_han_sans_tc_10_regular

    print(f"[extract_glyphs] Reading {full_path.name}...")
    if full_path.suffix == ".xz":
        with lzma.open(full_path, "rt", encoding="utf-8") as f:
            content = f.read()
    else:
        content = full_path.read_text(encoding="utf-8")
    print(f"  Loaded {len(content):,} chars")

    print("[extract_glyphs] Parsing full header...")
    full_name, bitmaps, glyphs, intervals, meta = parse_full_header(content)
    print(f"  Parsed: {len(glyphs)} glyphs, {len(intervals)} intervals, {len(bitmaps):,} bitmap bytes")

    print(f"[extract_glyphs] Reading charset {charset_path.name}...")
    wanted = read_charset(charset_path)
    print(f"  Want {len(wanted)} codepoints")

    print("[extract_glyphs] Extracting subset...")
    new_bitmaps, new_glyphs, new_intervals = extract_subset(bitmaps, glyphs, intervals, wanted)
    coverage = len(new_glyphs) / len(wanted) * 100 if wanted else 0
    print(f"  Extracted {len(new_glyphs)} glyphs ({coverage:.1f}% coverage), {len(new_bitmaps):,} bitmap bytes")

    print(f"[extract_glyphs] Writing {out_path.name}...")
    header = build_header(out_name, new_bitmaps, new_glyphs, new_intervals, meta)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(header, encoding="utf-8", newline="\n")
    out_kb = out_path.stat().st_size / 1024
    print(f"  Done. Output: {out_kb:.1f} KB")


if __name__ == "__main__":
    main()
