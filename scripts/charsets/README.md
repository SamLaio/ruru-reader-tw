# ruru-reader-tw UI 字集

這個資料夾收錄了 `ruru-reader-tw`（閱星曈 X4 繁中韌體）使用的字集檔，
可直接給 `fontconvert.py --charset-file` 用來生成 epdfont 子集化字型 `.h` 檔。

## 檔案清單

| 檔案 | 字數 | 用途 |
|------|------|------|
| `ui_charset.txt` | 7208 | common7000 + UI 三語，給 12pt UI / 外部文字用 |
| **`ui_charset_common7000.txt`** ⭐ | **7082** | 原始常用字集 |
| `ui_charset_merged.txt` | 7208 | common7000 + UI 三語，保留給相容流程用 |
| `charset_full.txt` | 11316 | 原始完整閱讀字集 |
| `ui_charset_reader.txt` | 7208 | common7000 + UI 三語，給 17pt reader / 書名用 |
| `_edu4808.txt` | 4808 | 教育部常用字 4808 字（來源） |
| `_hanchar.json` | 8105 | 中華民國教育部「異體字字典」結構化字表（參考） |

## `ui_charset_common7000.txt` 組成

```
教育部常用字 4808 字（甲表）
  + ChineseType base 累積的繁中字補充（次常用範圍，補到 6800 漢字）
  + 嚕寶介面實際用字（已含於上述）
  + ASCII 95 字（U+0020-007E：英文 / 數字 / 標點）
  + CJK 標點 64 字（U+3000-303F）
  + 全形符號 95 字（U+FF00-FF5E）
  ──
  = 總共約 7000 字
```

## stage15.55 字集分流

| 字型 | charset | 用途 |
|------|---------|------|
| 10pt | `ui_charset_merged.txt` | 外部文字、檔名、狀態、小字 |
| 12pt | `ui_charset.txt` | 設定頁、標題、主要 UI |
| 17pt | `ui_charset_reader.txt` | 書名與 reader 內文 |

## 怎麼用

```bash
cd lib/EpdFont/scripts
python fontconvert.py source_han_sans_tc_17_regular 17 \
  ../builtinFonts/source/SourceHanSansTC/SourceHanSansTC-Regular.otf \
  --charset-file ../../../scripts/charsets/ui_charset_reader.txt \
  > ../builtinFonts/source_han_sans_tc_17_regular.h
```

## 自動化 build pipeline

`platformio.ini` 已接 pre-script `scripts/generate_ui_charset.py` 與 `scripts/generate_ui_font_subset.py`，
build 時自動：

1. 掃 `src/*.cpp/h` 抓中文字
2. 從 `src/LanguageMapper.h` 抓繁中、簡中、英文 UI 文字
3. 產出 10pt / 12pt / 17pt 各自的 charset
4. 重新跑 fontconvert 生成子集字型 .h

## 對外分享版本

如果你要拿這份字集到自己的 e-ink reader 韌體 / epdfont 專案用，
**只需要兩個檔**：

- `ui_charset_common7000.txt`（常用字集本身）
- `_edu4808.txt`（教育部常用字、原始來源）

授權方式：
- 字集本身：CC0 / Public Domain（嚕寶整理、字本身來自公開字表）
- 教育部常用字表：中華民國教育部公告字表（公共領域）
- ChineseType 補充字：原 `crosspoint-chinesetype` MIT License

## stage15.55 字型大小（Source Han Sans TC）

| 字級 | 字集 | header 大小 | bitmap bytes |
|------|------|------------:|-------------:|
| 10pt | common7000 + UI 三語 | 3,106,448 bytes | 355,327 |
| 12pt | common7000 + UI 三語 | 4,170,627 bytes | 523,671 |
| 17pt | common7000 + UI 三語 | 7,185,854 bytes | 999,982 |

stage15.55 BIN 為 5,614,976 bytes，Flash 使用率約 82.9%。

## 已驗證裝置

- Xteink X4（ESP32-C3、320 KB RAM、16 MB Flash）
- 韌體：ruru-reader-tw stage15.55

## 來源

- [TraditionalChinese/TW-ABCN](https://github.com/TraditionalChinese/TW-ABCN) — 臺灣 TW-ABCN 正字甲乙丙表（PDF + xlsx）
- [Watermelonnn/ChineseUsefulToolKit](https://github.com/Watermelonnn/ChineseUsefulToolKit) — 教育部常用字 4808 字 txt
- [gitqwerty777/Chinese-Characters-Standards](https://github.com/gitqwerty777/Chinese-Characters-Standards) — 中華民國 + 中國教育部漢字標準資料
- [icannotttt/crosspoint-chinesetype](https://github.com/icannotttt/crosspoint-chinesetype) — ChineseType base 字集
