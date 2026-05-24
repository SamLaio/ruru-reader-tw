#!/usr/bin/env python3
"""Generate UI charset files from source strings and LanguageMapper."""

from build_ui_fonts import (
    CHARSETS_DIR,
    UI_CHARSET,
    UI_CHARSET_MERGED,
    UI_CHARSET_READER,
    load_external_charset,
    load_common_charset,
    scan_language_mapper_chars,
    scan_ui_chars,
    strip_line_breaks,
    write_charset,
)


def main():
    ui_chars = scan_ui_chars() | scan_language_mapper_chars()
    common_chars = load_common_charset()
    external_common_chars = load_external_charset()
    ui_display_chars = strip_line_breaks("".join(ui_chars | common_chars))
    external_chars = strip_line_breaks("".join(ui_chars | external_common_chars))
    reader_chars = strip_line_breaks("".join(ui_chars | common_chars))

    CHARSETS_DIR.mkdir(parents=True, exist_ok=True)
    write_charset(UI_CHARSET, ui_display_chars)
    write_charset(UI_CHARSET_MERGED, external_chars)
    write_charset(UI_CHARSET_READER, reader_chars)

    print(f"[generate_ui_charset] UI raw chars: {len(ui_chars)}")
    print(f"[generate_ui_charset] UI display chars: {len(ui_display_chars)}")
    print(f"[generate_ui_charset] External chars: {len(external_chars)}")
    print(f"[generate_ui_charset] Reader chars: {len(reader_chars)}")


try:
    Import("env")  # noqa: F821
    main()
except NameError:
    if __name__ == "__main__":
        main()
