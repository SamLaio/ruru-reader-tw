"""
build_ui_fonts.py — 在 PlatformIO build 階段自動子集化思源黑體 UI / reader 字型

流程：
  1. 掃 src/ 所有 .cpp/.h 抓中文字串字面量與 LanguageMapper 三語表 → ui_chars
  2. 產生三種用途：
     - 12pt UI：常用 7000 字 + UI 三語文字
     - 10pt 外部文字：常用 7000 字 + UI 三語文字
     - 17pt reader：常用 7000 字 + UI 三語文字
     → 產出 scripts/charsets/ui_charset_merged.txt 與 ui_charset_reader.txt
  3. 比對 charset 的 hash 跟上次 build 的 hash
     - 一致 → 跳過，不重 build 字型（省時間）
     - 不一致 → 跑 fontconvert.py 產生 source_han_sans_tc_{10,12,17}_regular.h
  4. 把子集字型檔放在 lib/EpdFont/builtinFonts/ 底下

設計原則：
  - 12pt 給 UI
  - 10pt 給外部文字 / 檔名 / 小字
  - 17pt 給 reader / 書名
  - 第一次 build 跑全套約 1-2 分鐘，之後增量 build 無感

呼叫時機：PlatformIO 的 pre 階段（在 build 第一行 compile 前執行）
  platformio.ini: extra_scripts = pre:scripts/build_ui_fonts.py
"""
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

# PlatformIO 把 pre-script 用 exec() 跑，沒有 __file__
# Fallback: 用 cwd 當 ROOT（PlatformIO 預設 cwd 是專案根目錄）
try:
    ROOT = Path(__file__).parent.parent
except NameError:
    ROOT = Path(os.getcwd())
SRC = ROOT / "src"
CHARSETS_DIR = ROOT / "scripts" / "charsets"
UI_CHARSET = CHARSETS_DIR / "ui_charset.txt"
UI_CHARSET_MERGED = CHARSETS_DIR / "ui_charset_merged.txt"
UI_CHARSET_READER = CHARSETS_DIR / "ui_charset_reader.txt"
FULL_CHARSET = CHARSETS_DIR / "charset_full.txt"
COMMON_CHARSET = CHARSETS_DIR / "ui_charset_common7000.txt"
HASH_CACHE = ROOT / ".pio" / "ui_font_charset.hash"  # 上次 build 用的 charset hash

FONT_SOURCE = ROOT / "lib" / "EpdFont" / "builtinFonts" / "source" / "SourceHanSansTC"
FONT_OUTPUT_DIR = ROOT / "lib" / "EpdFont" / "builtinFonts"
FONTCONVERT = ROOT / "lib" / "EpdFont" / "scripts" / "fontconvert.py"
FONTCONVERT_REQUIREMENTS = ROOT / "lib" / "EpdFont" / "scripts" / "requirements.txt"

# UI / 外部文字 / reader 用不同字號，但共用常用字集，避免塞完整閱讀字集
UI_FONT_SIZES = [12]
EXTERNAL_FONT_SIZES = [10]
READER_FONT_SIZES = [17]
UI_FONT_STYLES = ["regular"]

CHINESE_RANGE = re.compile(r"[一-鿿]")
STRING_PATTERN = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
LANGUAGE_ENTRY_PATTERN = re.compile(r'\{"((?:[^"\\\n]|\\.)*)",\s*"((?:[^"\\\n]|\\.)*)",\s*"((?:[^"\\\n]|\\.)*)",\s*"((?:[^"\\\n]|\\.)*)"\}')


def scan_ui_chars():
    """掃 src/ 所有 .cpp/.h 中的中文字串字面量"""
    charset = set()
    for path in SRC.rglob("*"):
        if path.suffix not in {".cpp", ".h"}:
            continue
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except Exception:
            continue
        for match in STRING_PATTERN.finditer(text):
            s = match.group(1)
            for ch in s:
                if CHINESE_RANGE.match(ch):
                    charset.add(ch)
    return charset


def scan_language_mapper_chars():
    """額外掃 LanguageMapper 三語表，英文與符號也納入 UI 字集。"""
    mapper = SRC / "LanguageMapper.h"
    charset = set()
    if not mapper.exists():
        return charset
    text = mapper.read_text(encoding="utf-8")
    for match in LANGUAGE_ENTRY_PATTERN.finditer(text):
        for group_index in range(1, 5):
            for ch in match.group(group_index):
                if ch.isprintable() and ch not in {"\n", "\r"}:
                    charset.add(ch)
    return charset


def load_full_charset():
    """載入完整字集，僅作為常用字集不存在時的後備。"""
    if FULL_CHARSET.exists():
        return strip_line_breaks(FULL_CHARSET.read_text(encoding="utf-8"))
    return set()


def load_common_charset():
    """載入常用字集（給 UI / 外部文字 / reader 用）"""
    if COMMON_CHARSET.exists():
        return strip_line_breaks(COMMON_CHARSET.read_text(encoding="utf-8"))
    return load_full_charset()


def strip_line_breaks(text):
    """保留空白與符號，只移除 charset 檔的換行。"""
    return set(text) - {"\n", "\r"}


def write_charset(path, charset):
    path.write_text("".join(sorted(strip_line_breaks("".join(charset)))), encoding="utf-8")


def merged_charset_hash(*charsets):
    """計算 charset 的 hash（給 incremental build 判斷用）"""
    h = hashlib.sha256()
    for charset in charsets:
        h.update(b"\0charset\0")
        for ch in sorted(charset):
            h.update(ch.encode("utf-8"))
    return h.hexdigest()


def expected_font_outputs():
    """Return the generated font headers required by the firmware build."""
    sizes = UI_FONT_SIZES + EXTERNAL_FONT_SIZES + READER_FONT_SIZES
    return [
        FONT_OUTPUT_DIR / f"source_han_sans_tc_{size}_{style}.h"
        for size in sizes
        for style in UI_FONT_STYLES
    ]


def fontconvert_install_hint():
    return f"{sys.executable} -m pip install -r {FONTCONVERT_REQUIREMENTS}"


def can_run_fontconvert():
    try:
        import freetype  # noqa: F401
        return True
    except ModuleNotFoundError:
        return False


def run_fontconvert(size, style, charset_file, output_path):
    """跑 fontconvert.py 產 epdfont .h"""
    font_name = f"source_han_sans_tc_{size}_{style}"
    font_file = FONT_SOURCE / "SourceHanSansTC-Regular.otf"
    if not font_file.exists():
        fonts = list(FONT_SOURCE.glob("*.otf")) + list(FONT_SOURCE.glob("*.ttf"))
        if not fonts:
            print(f"  ERROR: No .otf/.ttf found in {FONT_SOURCE}", file=sys.stderr)
            return False
        font_file = fonts[0]

    cmd = [
        sys.executable,
        str(FONTCONVERT),
        font_name,
        str(size),
        str(font_file),
        "--charset-file",
        str(charset_file),
    ]
    try:
        env = os.environ.copy()
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUTF8"] = "1"
        result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", env=env)
        if result.returncode != 0:
            print(f"  ERROR: fontconvert failed for {font_name}", file=sys.stderr)
            print(result.stderr, file=sys.stderr)
            return False
        output_path.write_text(result.stdout, encoding="utf-8")
        size_kb = output_path.stat().st_size / 1024
        print(f"  Generated {output_path.name} ({size_kb:.1f} KB)")
        return True
    except Exception as e:
        print(f"  ERROR running fontconvert: {e}", file=sys.stderr)
        return False


def main():
    print("[build_ui_fonts] Scanning UI Chinese characters from src/...")

    # 1. 掃 UI 字
    ui_chars = scan_ui_chars() | scan_language_mapper_chars()
    print(f"  Found {len(ui_chars)} unique UI Chinese characters")

    # 2. 合併不同用途的字集
    common_chars = load_common_charset()
    print(f"  Loaded {len(common_chars)} chars from common charset (for UI/external/reader text)")

    ui_display_chars = strip_line_breaks("".join(ui_chars | common_chars))
    external_chars = strip_line_breaks("".join(ui_chars | common_chars))
    reader_chars = strip_line_breaks("".join(ui_chars | common_chars))
    print(f"  UI display total: {len(ui_display_chars)} chars")
    print(f"  External total: {len(external_chars)} chars")
    print(f"  Reader total: {len(reader_chars)} chars")

    # 寫出 merged charset 供後續 fontconvert 用
    CHARSETS_DIR.mkdir(parents=True, exist_ok=True)
    write_charset(UI_CHARSET, ui_display_chars)
    write_charset(UI_CHARSET_MERGED, external_chars)
    write_charset(UI_CHARSET_READER, reader_chars)

    # 3. hash 比對：跟上次一樣 → 跳過
    current_hash = merged_charset_hash(ui_chars, external_chars, reader_chars)
    HASH_CACHE.parent.mkdir(parents=True, exist_ok=True)
    if HASH_CACHE.exists():
        last_hash = HASH_CACHE.read_text().strip()
        if last_hash == current_hash and all(
            path.exists()
            for path in expected_font_outputs()
        ):
            print("[build_ui_fonts] No charset changes since last build, skipping font generation.")
            return

    if not can_run_fontconvert():
        missing_outputs = [path for path in expected_font_outputs() if not path.exists()]
        print("[build_ui_fonts] freetype-py is not installed for this Python.", file=sys.stderr)
        print(f"[build_ui_fonts] To regenerate fonts, run: {fontconvert_install_hint()}", file=sys.stderr)
        if not missing_outputs:
            print("[build_ui_fonts] Existing generated font headers found; skipping regeneration.")
            return
        missing = ", ".join(path.name for path in missing_outputs)
        raise SystemExit(f"[build_ui_fonts] Missing generated font headers: {missing}")

    # 4. 跑 fontconvert 產生子集字型
    print("[build_ui_fonts] Charset changed (or first build), regenerating UI fonts...")
    success_count = 0
    for size in UI_FONT_SIZES:
        for style in UI_FONT_STYLES:
            output = FONT_OUTPUT_DIR / f"source_han_sans_tc_{size}_{style}.h"
            if run_fontconvert(size, style, UI_CHARSET, output):
                success_count += 1
    for size in EXTERNAL_FONT_SIZES:
        for style in UI_FONT_STYLES:
            output = FONT_OUTPUT_DIR / f"source_han_sans_tc_{size}_{style}.h"
            if run_fontconvert(size, style, UI_CHARSET_MERGED, output):
                success_count += 1
    for size in READER_FONT_SIZES:
        for style in UI_FONT_STYLES:
            output = FONT_OUTPUT_DIR / f"source_han_sans_tc_{size}_{style}.h"
            if run_fontconvert(size, style, UI_CHARSET_READER, output):
                success_count += 1

    total = (len(UI_FONT_SIZES) + len(EXTERNAL_FONT_SIZES) + len(READER_FONT_SIZES)) * len(UI_FONT_STYLES)
    if success_count == total:
        HASH_CACHE.write_text(current_hash)
        print(f"[build_ui_fonts] OK ({success_count}/{total} fonts regenerated)")
    else:
        print(f"[build_ui_fonts] WARN: only {success_count}/{total} succeeded", file=sys.stderr)


# PlatformIO pre-script 入口（platformio.ini extra_scripts 會呼叫）
try:
    Import("env")  # noqa: F821 — pio injects
    main()
except NameError:
    # 也支援獨立執行：python scripts/build_ui_fonts.py
    if __name__ == "__main__":
        main()
