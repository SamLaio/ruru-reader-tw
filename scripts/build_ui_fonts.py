"""
build_ui_fonts.py — stage31 字體系統：純 Python 抽 glyph

stage31 改動：
  - 整個韌體只用 1 個字體尺寸（10pt 思源黑體 TC）
  - build 不再依賴 freetype，改用 scripts/extract_glyphs.py
  - 從壓縮的完整 header (source_han_sans_tc_10_full.h.xz, ~11000 字)
    抽出 ui_charset.txt 需要的字 → source_han_sans_tc_10_regular.h

流程：
  1. 掃 src/ 所有 .cpp/.h 抓中文字串字面量與 LanguageMapper 三語表 → ui_chars
  2. 合併 ui_chars + 教育部 4808 + RIME 次常用 + 直排符號 + ASCII + 全形標點
     → scripts/charsets/ui_charset_runtime.txt
  3. hash 比對：跟上次 build 的 hash 一致就跳過（incremental）
  4. 跑 scripts/extract_glyphs.py 從 source_han_sans_tc_10_full.h.xz 抽 glyph
     輸出 lib/EpdFont/builtinFonts/source_han_sans_tc_10_regular.h

設計原則：
  - 完整 header (source_han_sans_tc_10_full.h.xz, 543 KB) 是「字體池」，永遠固定
  - 重做完整 header 需要 fontconvert.py + freetype（手動，非 build pipeline）
  - 平常 build 只解壓 + 切 byte，不依賴 freetype

呼叫時機：PlatformIO 的 pre 階段
  platformio.ini: extra_scripts = pre:scripts/build_ui_fonts.py
"""
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

# PlatformIO 把 pre-script 用 exec() 跑，沒有 __file__
try:
    ROOT = Path(__file__).parent.parent
except NameError:
    ROOT = Path(os.getcwd())
SRC = ROOT / "src"
CHARSETS_DIR = ROOT / "scripts" / "charsets"
EXTRACT_SCRIPT = ROOT / "scripts" / "extract_glyphs.py"

# 字集檔
UI_CHARSET_RUNTIME = CHARSETS_DIR / "ui_charset_runtime.txt"
EDU4808 = CHARSETS_DIR / "_edu4808.txt"
RIME_COMMON = CHARSETS_DIR / "_rime_common.txt"
VERTICAL_SYMBOLS = CHARSETS_DIR / "_vertical_symbols.txt"

# 完整 header (xz 壓縮) + 輸出瘦身 header
FULL_HEADER_XZ = ROOT / "lib" / "EpdFont" / "builtinFonts" / "source_han_sans_tc_10_full.h.xz"
OUTPUT_HEADER = ROOT / "lib" / "EpdFont" / "builtinFonts" / "source_han_sans_tc_10_regular.h"
OUTPUT_VAR_NAME = "source_han_sans_tc_10_regular"

HASH_CACHE = ROOT / ".pio" / "ui_font_charset.hash"

# 掃描 pattern
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


def load_charset_file(path):
    """讀字集檔，回傳 set of chars（去掉空白/換行）"""
    if not path.exists():
        return set()
    text = path.read_text(encoding="utf-8")
    return set(c for c in text if not c.isspace() and ord(c) >= 0x20)


def build_target_charset():
    """組目標字集：教育部 4808 + RIME 次常用 + 掃出 UI 三語 + 直排符號 + ASCII + 全形標點"""
    edu = load_charset_file(EDU4808)
    rime = load_charset_file(RIME_COMMON)
    vertical = load_charset_file(VERTICAL_SYMBOLS)
    ui = scan_ui_chars() | scan_language_mapper_chars()
    ascii_chars = set(chr(i) for i in range(0x20, 0x7F))
    # 全形標點 + 通用標點
    punct = set()
    for code_range in [range(0x3000, 0x3040), range(0xFF00, 0xFFA0), range(0x2010, 0x2030)]:
        for cp in code_range:
            punct.add(chr(cp))
    return edu | rime | vertical | ui | ascii_chars | punct, len(edu), len(rime), len(vertical), len(ui)


def write_charset(path, charset):
    text = "".join(sorted(c for c in charset if not c.isspace() or c == " "))
    path.write_text(text, encoding="utf-8", newline="\n")


def charset_hash(charset):
    """計算 charset 的 hash（給 incremental build 判斷用）

    stage32 修 race condition：hash 包含輸入檔指紋，避免下列場景跳過 extract：
      - 換完整 header（譬如 TC 版 → CJK 全版）
      - 改 extract_glyphs.py 邏輯

    OUTPUT_HEADER 不放進來（會被自己 extract 寫過 → mtime 自我變動 → 永遠 mismatch）。
    """
    h = hashlib.sha256()
    for ch in sorted(charset):
        h.update(ch.encode("utf-8"))
    # 只算「輸入指紋」：完整 header xz + extract_glyphs.py
    for path in [FULL_HEADER_XZ, EXTRACT_SCRIPT]:
        try:
            st = path.stat()
            h.update(f"\0{path.name}={st.st_size}:{int(st.st_mtime_ns)}".encode())
        except FileNotFoundError:
            h.update(f"\0{path.name}=MISSING".encode())
    return h.hexdigest()


def run_extract():
    """跑 extract_glyphs.py 產出瘦身 header"""
    cmd = [
        sys.executable,
        str(EXTRACT_SCRIPT),
        str(FULL_HEADER_XZ),
        str(UI_CHARSET_RUNTIME),
        str(OUTPUT_HEADER),
        "--out-name",
        OUTPUT_VAR_NAME,
    ]
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    env["PYTHONUTF8"] = "1"
    result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", env=env)
    if result.returncode != 0:
        print("[build_ui_fonts] ERROR: extract_glyphs.py failed", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False
    # 把 extract_glyphs.py 的 stdout 轉發
    for line in result.stdout.splitlines():
        print(f"  {line}")
    return True


def main():
    print("[build_ui_fonts] stage31 字體 build (純 Python 抽 glyph)")

    if not FULL_HEADER_XZ.exists():
        print(f"[build_ui_fonts] ERROR: 完整 header 不存在: {FULL_HEADER_XZ}", file=sys.stderr)
        print("  請先跑 fontconvert.py 重做完整 header (one-time setup)", file=sys.stderr)
        raise SystemExit(1)

    if not EXTRACT_SCRIPT.exists():
        print(f"[build_ui_fonts] ERROR: extract_glyphs.py 不存在: {EXTRACT_SCRIPT}", file=sys.stderr)
        raise SystemExit(1)

    # 1. 組目標字集
    target, n_edu, n_rime, n_vertical, n_ui = build_target_charset()
    print(f"  目標字集組成:")
    print(f"    教育部 4808:  {n_edu} 字")
    print(f"    RIME 次常用:  {n_rime} 字")
    print(f"    直排符號:     {n_vertical} 字")
    print(f"    UI 三語掃出:  {n_ui} 字")
    print(f"    + ASCII / 全形標點 / 通用標點")
    print(f"  合併後總字數: {len(target)} 字")

    # 2. 寫 runtime charset
    CHARSETS_DIR.mkdir(parents=True, exist_ok=True)
    write_charset(UI_CHARSET_RUNTIME, target)

    # 3. hash 比對：跟上次一樣 → 跳過
    current_hash = charset_hash(target)
    HASH_CACHE.parent.mkdir(parents=True, exist_ok=True)
    if HASH_CACHE.exists() and OUTPUT_HEADER.exists():
        last_hash = HASH_CACHE.read_text().strip()
        if last_hash == current_hash:
            print("[build_ui_fonts] 字集無變化，跳過 extract（已有 cached header）")
            return

    # 4. 跑 extract_glyphs 抽 glyph
    print("[build_ui_fonts] 跑 extract_glyphs.py 抽 glyph...")
    if not run_extract():
        raise SystemExit(1)

    HASH_CACHE.write_text(current_hash)
    print("[build_ui_fonts] OK")


# PlatformIO pre-script 入口
try:
    Import("env")  # noqa: F821 — pio injects
    main()
except NameError:
    if __name__ == "__main__":
        main()
