#!/usr/bin/env python3
"""Generate Source Han Sans TC subset font headers."""

from build_ui_fonts import (
    FONT_OUTPUT_DIR,
    EXTERNAL_FONT_SIZES,
    READER_FONT_SIZES,
    UI_CHARSET,
    UI_CHARSET_MERGED,
    UI_CHARSET_READER,
    UI_FONT_SIZES,
    UI_FONT_STYLES,
    run_fontconvert,
)


def main():
    if not UI_CHARSET.exists():
        raise SystemExit(f"Missing charset file: {UI_CHARSET}")
    if not UI_CHARSET_MERGED.exists():
        raise SystemExit(f"Missing charset file: {UI_CHARSET_MERGED}")
    if not UI_CHARSET_READER.exists():
        raise SystemExit(f"Missing charset file: {UI_CHARSET_READER}")

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
    if success_count != total:
        raise SystemExit(f"[generate_ui_font_subset] Failed: {success_count}/{total}")
    print(f"[generate_ui_font_subset] OK: {success_count}/{total}")


try:
    Import("env")  # noqa: F821
    main()
except NameError:
    if __name__ == "__main__":
        main()
