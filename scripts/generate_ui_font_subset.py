#!/usr/bin/env python3
"""Generate Source Han Sans TC subset font headers."""

from build_ui_fonts import (
    FONT_OUTPUT_DIR,
    can_run_fontconvert,
    EXTERNAL_FONT_SIZES,
    READER_FONT_SIZES,
    UI_CHARSET,
    UI_CHARSET_MERGED,
    UI_CHARSET_READER,
    UI_FONT_SIZES,
    UI_FONT_STYLES,
    expected_font_outputs,
    fontconvert_install_hint,
    run_fontconvert,
)


def main():
    if not UI_CHARSET.exists():
        raise SystemExit(f"Missing charset file: {UI_CHARSET}")
    if not UI_CHARSET_MERGED.exists():
        raise SystemExit(f"Missing charset file: {UI_CHARSET_MERGED}")
    if not UI_CHARSET_READER.exists():
        raise SystemExit(f"Missing charset file: {UI_CHARSET_READER}")

    if not can_run_fontconvert():
        missing_outputs = [path for path in expected_font_outputs() if not path.exists()]
        print("[generate_ui_font_subset] freetype-py is not installed for this Python.", flush=True)
        print(f"[generate_ui_font_subset] To regenerate fonts, run: {fontconvert_install_hint()}", flush=True)
        if not missing_outputs:
            print("[generate_ui_font_subset] Existing generated font headers found; skipping regeneration.", flush=True)
            return
        missing = ", ".join(path.name for path in missing_outputs)
        raise SystemExit(f"[generate_ui_font_subset] Missing generated font headers: {missing}")

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
