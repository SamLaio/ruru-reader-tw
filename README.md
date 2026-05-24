<h1 align="center">ruru-reader-tw</h1>

<p align="center"><strong>Traditional Chinese firmware for the YueXingTong X4 e-reader.</strong><br>
OPDS cloud library · Source Han Sans TC subset · multilingual UI · BT page-turn · large books · vertical layout</p>

<p align="center">
  <img src="https://img.shields.io/badge/license-MIT_%2B_PolyForm_NC-D4A5A5?style=flat-square" alt="Dual License">
  <img src="https://img.shields.io/badge/platform-ESP32--C3-B8A9C9?style=flat-square" alt="ESP32-C3">
  <img src="https://img.shields.io/badge/font-Source_Han_Sans_TC-A8B5A0?style=flat-square" alt="Source Han Sans TC">
  <img src="https://img.shields.io/badge/OPDS-supported-9B7E93?style=flat-square" alt="OPDS Supported">
  <img src="https://img.shields.io/badge/stage-29-E8B4B8?style=flat-square" alt="Stage 29">
</p>

<p align="center">
  <b>English</b> | <a href="README.zh-TW.md">繁體中文</a>
</p>

---

> **Dual License Notice** — Upstream code is MIT (commercial use OK).
> HelloRuru's stage 8 to stage 29 modifications are under PolyForm Noncommercial 1.0.0.
> **Personal / open-source / learning use is free; commercial use requires a separate license** — contact <hello@helloruru.com>.
> See [LICENSE](LICENSE) for full terms.

---

## What this is

The **YueXingTong X4** (閱星曈 X4) is an ESP32-C3 e-reader with 320KB RAM, 16MB flash, no PSRAM. The stock firmware (ChineseType) is primarily for Simplified Chinese users and bundles cloud services Traditional Chinese readers don't need.

This fork makes the X4 a great reader for Traditional Chinese users while keeping a switchable UI for Simplified Chinese and English.

### 🆕 What's new in stage29

- **OPDS cloud library** — browse your computer's book library straight from the main menu (pairs with [SamLaio/dir2opds](https://github.com/SamLaio/dir2opds)). Up/Down to pick a book, Left/Right to flip OPDS pages, Confirm to download to SD card.
- **Multi-format auto-detection** — downloads correctly identify EPUB / PDF / CBZ / MOBI / TXT / ZIP and use the right extension (fixes the legacy double-extension bug).
- **HTTP status code diagnostics** — OPDS download failures show the actual `HTTP 404 / 503` so you can debug server-side issues.
- **OTA online update removed** — saves 127 KB flash space (use USB flashing instead).

### Existing features (stage 1-29 accumulation)

- **Source Han Sans TC subset font** — UI, book titles, and reader body all switched over.
- **Multilingual UI** — interface strings centralised in `src/LanguageMapper.h`, with Traditional Chinese, Simplified Chinese, and English.
- **Bluetooth HID page-turn fixes** — RPA detection, vector-of-raw-pointers race, double-free on disconnect, blocking scan, and disable-callback protection.
- **Large Book Mode** — 10MB+ EPUBs with thousands of chapters open and page through.
- **HTML void-element self-close pre-pass** — fixes web-novel EPUBs that freeze on tap-TOC.
- **Vertical layout** — EPUB and TXT both support horizontal and vertical writing.
- **Underline settings** — horizontal top/bottom rule and vertical line spacing are adjustable.
- **Custom font upload** — drop `.epdfont` files on the SD card.
- **Bookmarking system** — spine-aligned with progress fallback.
- **Carousel home** — covers, recent reads, and progress bars.
- **HelloRuru rabbit boot and sleep screens**.

### Main menu layout (stage29)

```text
┌──────────────────────────────────────┐
│       [ Recent book covers ]         │
├──────────────────────────────────────┤
│   ╔════════════╦════════════╗       │
│   ║   Files    ║    OPDS    ║       │
│   ╚════════════╩════════════╝       │
├──────────────────────────────────────┤
│   [ WiFi ][ Settings ][ BT ]         │
└──────────────────────────────────────┘
```

## Installation

### 1. Choose a firmware

Latest builds are on [GitHub Releases](https://github.com/HelloRuru/ruru-reader-tw/releases):

| Build | Use case | File |
| :--- | :--- | :--- |
| 🌟 **stage29** | **Latest public release**; OPDS full support, multi-format detection, side-key pagination, HTTP diagnostics, OTA removed | [`Ruru-Reader-stage29-monoink.bin`](https://github.com/HelloRuru/ruru-reader-tw/releases/tag/stage29-opds) |
| stage15.55 | Legacy baseline; 10pt restored, 17pt reader common7000 subset | `ruru-reader-tw-stage15.55-20260516.bin` |
| stage15.48 | Source Han Sans, multilingual UI, BT disable hardening | `ruru-reader-tw-stage15.48-20260515.bin` |
| stage15.46 | Verified-stable baseline | `ruru-reader-tw-stage15.46-20260515.bin` |

> Legacy stage15.x builds are kept for reference; new installs should use **stage29**.

### 2. Flash via web

Open <https://flasher.crosspoint.world/> in **Chrome** (WebSerial required). Connect the X4 via USB-C, pick the BIN, click flash.

### 3. Or flash via ESPTool

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 --baud 921600 \
  write_flash -z 0x10000 release/Ruru-Reader-stage29-monoink.bin
```

### 4. First-boot setup

1. Power on.
2. Settings → Bluetooth → enable Bluetooth (if you have a page-turner).
3. Settings → WiFi → connect to your home network (required for OPDS).
4. Settings → OPDS Browser → enter server URL (see [OPDS cloud library](#opds-cloud-library) below).
5. Subsequent boots restore the saved state.

> When upgrading to stage29, consider deleting `.crosspoint/` on the SD card so book caches are rebuilt. Settings are preserved.

## OPDS cloud library

Stage29 adds an OPDS browser so you can pull books from a computer-side OPDS server straight to the SD card, no USB needed.

### Pairs with SamLaio/dir2opds

This firmware's OPDS feature is designed to **pair with [SamLaio/dir2opds](https://github.com/SamLaio/dir2opds)** — a lightweight tool that turns any folder into an OPDS server. See the upstream repo for installation.

### Three-step setup

1. **Computer side**: launch dir2opds pointing at your book library folder (defaults to port 8080).
2. **YueXingTong side**: Settings → OPDS Browser → enter `http://<computer-IP>:8080` (not `127.0.0.1` — the YueXingTong can't reach it).
3. **Browse**: tap the OPDS large button on the main menu to browse, pick, and download.

### Controls

```text
┌─ In book list ──────────────────────────┐
│   ↑ ↓  Select book / option              │
│   ← →  Flip OPDS page (large libraries)  │
│   ●    Confirm (open folder / download)  │
│   ⏴    Back to previous level             │
└──────────────────────────────────────────┘
```

Download failures show the HTTP status code (e.g. `Download failed: HTTP 404`) for easy debugging.

## Verified hardware

### Bluetooth page-turners

| Device | Connect | Page-turn | Notes |
| :--- | :--- | :--- | :--- |
| iDal-10822 | Works | Works, keycode `0x4E` | Daily driver |
| E1 Control | Works | Works, keycode `0x4B` | Backup |
| HBTR003-XT | Works | Works | Stable after stage7-9 BT fixes (RPA / addrType / heap) |
| Keykey mini | Works | Works | — |

### Large EPUB compatibility

| Book | Size | Chapters | Open | Page-turn | Tap TOC |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Web novel (no chapter index) | 15 MB | - | Works | Works | Works |
| Multi-chapter web novel | 10.25 MB | 2752 | Works | Works | Works |

## UI strings and font subsetting

Interface strings live in:

```text
src/LanguageMapper.h
```

Before each build, the following run automatically:

```text
scripts/generate_ui_charset.py
scripts/generate_ui_font_subset.py
```

Flow:

1. Scan UI strings in `src/`.
2. Read Traditional Chinese, Simplified Chinese, and English text from `LanguageMapper`.
3. Produce `scripts/charsets/ui_charset.txt` and `scripts/charsets/ui_charset_merged.txt`.
4. Subset from the Source Han Sans TC source font.
5. Regenerate the built-in font headers:

```text
lib/EpdFont/builtinFonts/source_han_sans_tc_10_regular.h
lib/EpdFont/builtinFonts/source_han_sans_tc_12_regular.h
lib/EpdFont/builtinFonts/source_han_sans_tc_17_regular.h
```

Current charset strategy:

| Point size | Use | Charset |
| :--- | :--- | :--- |
| 10pt | External strings, filenames, status, small text | common7000 + UI tri-language |
| 12pt | Settings pages, titles, primary UI | Pure UI tri-language |
| 17pt | Book titles and reader body | common7000 + UI tri-language |

Current stage15.48 build footprint:

| Metric | Value |
| :--- | :--- |
| RAM | `110,924 / 327,680 bytes`, about `33.9%` |
| Flash | `4,532,080 / 6,553,600 bytes`, about `69.2%` |
| BIN | `4,719,872 bytes` |

## Project structure

```text
ruru-reader-tw/
├── lib/
│   ├── EpdFont/                 # Built-in fonts and fontconvert tooling
│   ├── Epub/                    # EPUB parsing and pagination
│   ├── GfxRenderer/             # Rendering and vertical text
│   └── hal/
│       └── BluetoothHIDManager  # BT HID page-turn
├── src/
│   ├── LanguageMapper.h         # Multilingual UI string table
│   ├── CrossPointSettings.*     # Settings storage, with uiLanguage
│   ├── SettingsLists.h          # Settings page entries
│   ├── activities/
│   │   ├── home/                # Carousel / recent reads
│   │   ├── reader/              # EPUB/TXT/XTC reader
│   │   └── settings/            # Settings, BT, fonts, cache
│   └── components/
│       └── themes/              # Lyra / Flow themes
├── scripts/
│   ├── generate_ui_charset.py
│   ├── generate_ui_font_subset.py
│   └── charsets/
└── release/                     # Pre-built BINs (gitignored by default)
```

## Customising

| Want to change | Look at |
| :--- | :--- |
| UI translations | `src/LanguageMapper.h` |
| Language menu | `src/SettingsLists.h` and `CrossPointSettings::uiLanguage` |
| Font subset | `scripts/charsets/ui_charset_common7000.txt` and `scripts/generate_ui_font_subset.py` |
| Reader font | `source_han_sans_tc_17_regular` in `src/main.cpp` |
| Bluetooth keycodes | `lib/hal/DeviceProfiles.cpp` |
| Reader menu items | `src/activities/reader/EpubReaderMenuActivity.h` |
| Skip auto-reconnect window | `BluetoothHIDManager::_uiBluetoothActive` |

## Design rationale

1. **NimBLE address-type heuristic**
   Read MAC byte 0 bits 7:6 instead of trusting `getAddressType()`. RPA addresses are recognised as RANDOM so the controller can actually find them.

2. **Zero manual BLE client release**
   Never call `NimBLEDevice::deleteClient` by hand; lifecycle belongs to NimBLE.

3. **HTML pre-pass over swapping parsers**
   A small state machine closes void elements before expat runs, instead of replacing the XML parser wholesale.

4. **Per-use-case font subsetting**
   The 12pt UI font does not ship common-7000; only the 10pt external-text font and the 17pt reader font do. Keeps BIN size manageable.

5. **Centralise UI text first, wire incrementally**
   New strings go into `LanguageMapper`. Screens already wired to the mapper switch with language; remaining hard-coded strings are migrated over time.

## Known limitations

| Area | Limitation | Impact / Notes |
| :--- | :--- | :--- |
| OPDS auth | Basic Auth (plain base64) | Best used on LAN; for public access add a reverse proxy |
| OPDS HTTPS | Accepted but uses `setInsecure()` — no cert verification | Fine for self-signed; doesn't block MITM |
| OPDS catalog | Only walks root → category → books (3 levels) | No OPDS 1.2 catalog discovery |
| OTA update | Removed in stage29 | Use USB flashing; saves 127 KB flash |
| Reader fonts | Subset by point size (10/12/17pt) | Rare characters may be missing; upload custom `.epdfont` to fill |
| BT page-turner | Field-tested with iDal-10822 / E1 Control / HBTR003-XT / Keykey mini | Other models may need keycode profile changes |

## Acknowledgments

| Upstream | Author | Contribution |
| :--- | :--- | :--- |
| [crosspoint-reader](https://github.com/daveallie/crosspoint-reader) | Dave Allie | Original MIT-licensed reader |
| CrossInk | uxjulia | Intermediate Chinese-friendly fork |
| CrossInk-Carousel | chintanvajariya | UI theme system (Lyra / Flow / 3Covers) |
| [crosspoint-chinesetype](https://github.com/icannotttt/crosspoint-chinesetype) | icannotttt | Chinese localisation, JianGuo cloud, KOReader sync, BT skeleton |
| [crosspoint-chinesetype](https://github.com/SamLaio/crosspoint-chinesetype) | SamLaio | Vertical layout and multilingual-UI reference; **stage28.9 adopts upstream OPDS Parser / Activity / HttpDownloader / UrlUtils wholesale** |
| [dir2opds](https://github.com/SamLaio/dir2opds) | SamLaio | Recommended OPDS server companion; this firmware's OPDS feature targets it |

Fonts:

- [Source Han Sans](https://github.com/adobe-fonts/source-han-sans) by Adobe, SIL Open Font License 1.1.
- Previously used [jf-openhuninn](https://justfont.com/huninn/) by justfont, CC BY 4.0.

## Requirements

- ESP32-C3 + 16MB flash + e-ink display (X4 hardware)
- Bluetooth HID page-turner, or use the physical buttons
- SD card for books, caches, and custom fonts

## License

This project uses a **dual-license model**. See [LICENSE](LICENSE) for full terms.

| Code | License | Commercial use |
| :--- | :--- | :--- |
| Upstream (Dave Allie, uxjulia, etc.) | MIT | Allowed |
| HelloRuru modifications (stage 8–29) | PolyForm Noncommercial 1.0.0 | Requires a separate commercial license |
| Source Han Sans | SIL Open Font License 1.1 | Per OFL |
| jf-openhuninn (legacy) | CC BY 4.0 | With attribution |

Free for personal flashing, learning, research, non-profit use, and source modification and redistribution.
Commercial use of HelloRuru's modifications: please contact <hello@helloruru.com>.

---

<p align="center">
  <sub>Made by <a href="https://github.com/HelloRuru">HelloRuru</a> · 2026 · for Traditional Chinese readers</sub>
</p>
