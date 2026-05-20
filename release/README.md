# Release BIN 清單

> 最後更新：2026-05-20（stage19，MonoInk 主畫面重設計）

## 推薦使用

| 版本 | 檔名 | 大小 | 備註 |
| :--- | :--- | ---: | :--- |
| **stage19** | `Ruru-Reader-stage19-monoink.bin` | 5,501,104 bytes | **目前推薦** — 2×2 書封網格、導航修正、彎引號支援 |
| stage18.1 | `Ruru-Reader-stage18.1-monoink.bin` | 5,501,104 bytes | stage18 小修版 |
| stage18 | `Ruru-Reader-stage18-monoink.bin` | 5,501,104 bytes | MonoInk 主題初版 |
| stage17 | `Ruru-Reader-stage17-monoink.bin` | 5,502,128 bytes | 藍牙 HID 修正 |

## stage19 變更

- 主畫面書封改為 **2×2 網格**，4 本書同時顯示（每格約 236×244px）。
- 修正主畫面上下翻頁按鍵無法移動選項的問題（`wasPressed` → `wasReleased`）。
- 移除版本號多餘後綴，啟動畫面版本字串顯示更簡潔。
- 修正啟動畫面版本號位置偏低被遮住的問題。
- reader 字體加入彎引號字符（`'` `'` `"` `"`，U+2018/2019/201C/201D），修正簡中書籍閱讀時標點符號顯示為空白的問題。

## stage18 / 18.1 變更

- MonoInk 主題整合，UI 改為高對比黑白風格。
- 藍牙 HID 連線穩定性修正。

## stage17 變更

- 藍牙 HID report descriptor 修正，改善翻頁器相容性。

## 編譯紀錄

| 版本 | 環境 | RAM | Flash | 結果 |
| :--- | :--- | :--- | :--- | :--- |
| stage19 | `default` | 110,524 / 327,680 bytes（33.7%） | 5,501,104 / 6,553,600 bytes（83.9%） | 成功 |
| stage18.1 | `default` | — | 5,501,104 / 6,553,600 bytes（83.9%） | 成功 |
| stage18 | `default` | — | 5,501,104 / 6,553,600 bytes（83.9%） | 成功 |
| stage17 | `default` | — | 5,502,128 / 6,553,600 bytes（83.9%） | 成功 |
| stage15.50 | `platformio run -e gh_release` | 110,924 / 327,680 bytes（33.9%） | 5,172,868 / 6,553,600 bytes（78.9%） | 成功 |

## 燒錄方式

1. 下載 `Ruru-Reader-stage19-monoink.bin`。
2. 使用 [ESP Web Tools](https://esphome.github.io/esp-web-tools/)、ESPTool 或 XTC Flasher 燒錄。
3. Offset：`0x10000`，裝置：ESP32-C3。
4. 升級後建議刪除 SD 卡 `/.crosspoint/` 資料夾，讓書籍 cache 重建。

## 燒錄注意事項

1. 第一次啟用藍牙要進「設定 → 藍牙」手動啟用一次，之後會依設定恢復。
2. 若藍牙翻頁器曾經配對異常，先在藍牙設定頁斷開，再重新掃描連線。
3. HBTR003-XT 翻頁器 RPA 連線不穩，建議改用 iDal-10822 或 E1 Control。

## 舊版注意

| 檔名 | 問題 |
| :--- | :--- |
| `ruru-reader-tw-stage12-20260505.bin` | ParsedText 空段落處理會讓部分書打不開 |
| `ruru-reader-cn-stage12-20260505.bin` | 同上 |
| `ruru-reader-tw-stage13-20260505.bin` | 繼承 stage12 開書問題 |
| `ruru-reader-cn-stage13-20260505.bin` | 同上 |
