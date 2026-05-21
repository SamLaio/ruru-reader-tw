#include "MonoInkTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Battery.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/chart.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {

constexpr int centerCoverWidth = 200;
constexpr int centerCoverHeight = 300;
constexpr int sideCoverWidth = 56;
constexpr int sideCoverHeight = 240;
constexpr int bookCornerRadius = 4;

constexpr int menuIconSize = 32;
constexpr int menuIconLabelGap = 4;

const uint8_t* monoInkMenuIcon(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:   return FolderIcon;
    case UIIcon::Book:     return BookIcon;
    case UIIcon::Chart:    return ChartIcon;
    case UIIcon::Recent:   return RecentIcon;
    case UIIcon::Settings: return Settings2Icon;
    case UIIcon::Transfer: return TransferIcon;
    case UIIcon::Library:  return LibraryIcon;
    case UIIcon::Wifi:     return WifiIcon;
    case UIIcon::Hotspot:  return HotspotIcon;
    default:               return nullptr;
  }
}

// 圓角遮罩：把矩形書封裁成圓角
void cutRoundedCorners(GfxRenderer& renderer, int x, int y, int w, int h, int r) {
  const int maxX = renderer.getScreenWidth() - 1;
  const int maxY = renderer.getScreenHeight() - 1;
  const int rSq = r * r;
  for (int dy = 0; dy < r; dy++) {
    for (int dx = 0; dx < r; dx++) {
      if ((r - dx) * (r - dx) + (r - dy) * (r - dy) > rSq) {
        const int px0 = x + dx,         py0 = y + dy;
        const int px1 = x + w - 1 - dx, py1 = y + dy;
        const int px2 = x + w - 1 - dx, py2 = y + h - 1 - dy;
        const int px3 = x + dx,         py3 = y + h - 1 - dy;
        if (px0 >= 0 && px0 <= maxX && py0 >= 0 && py0 <= maxY) renderer.drawPixel(px0, py0, false);
        if (px1 >= 0 && px1 <= maxX && py1 >= 0 && py1 <= maxY) renderer.drawPixel(px1, py1, false);
        if (px2 >= 0 && px2 <= maxX && py2 >= 0 && py2 <= maxY) renderer.drawPixel(px2, py2, false);
        if (px3 >= 0 && px3 <= maxX && py3 >= 0 && py3 <= maxY) renderer.drawPixel(px3, py3, false);
      }
    }
  }
}

}  // namespace

// ── 電量 ────────────────────────────────────────────────────────────────────
void MonoInkTheme::drawBattery(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  const uint16_t pct = battery.readPercentage();
  const int x = rect.x;
  const int y = rect.y + 6;
  const int bw = MonoInkMetrics::values.batteryWidth;

  // 外框
  renderer.drawLine(x + 1, y,                x + bw - 3, y);
  renderer.drawLine(x + 1, y + rect.height - 1, x + bw - 3, y + rect.height - 1);
  renderer.drawLine(x,     y + 1,            x,           y + rect.height - 2);
  renderer.drawLine(x + bw - 2, y + 1,       x + bw - 2, y + rect.height - 2);
  renderer.drawPixel(x + bw - 1, y + 3);
  renderer.drawPixel(x + bw - 1, y + rect.height - 4);
  renderer.drawLine(x + bw, y + 4, x + bw, y + rect.height - 5);

  // 填充（三段：>10% / >40% / >70%）
  if (pct > 10) renderer.fillRect(x + 2, y + 2, 3, rect.height - 4);
  if (pct > 40) renderer.fillRect(x + 6, y + 2, 3, rect.height - 4);
  if (pct > 70) renderer.fillRect(x + 10, y + 2, 3, rect.height - 4);

  if (showPercentage) {
    const auto txt = std::to_string(pct) + "%";
    renderer.drawText(SMALL_FONT_ID, x + bw + 4, rect.y, txt.c_str());
  }
}

// ── 標頭 ────────────────────────────────────────────────────────────────────
// subtitle 非 null 時作為第二行副標題（麵包屑或路徑）顯示在標題下方小字
void MonoInkTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                               const char* subtitle) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showPct =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  int batteryX = rect.x + rect.width - MonoInkMetrics::values.contentSidePadding -
                 MonoInkMetrics::values.batteryWidth;
  if (showPct) {
    const auto pctTxt = std::to_string(battery.readPercentage()) + "%";
    batteryX -= renderer.getTextWidth(SMALL_FONT_ID, pctTxt.c_str());
  }
  drawBattery(renderer,
              Rect{batteryX, rect.y + 6, MonoInkMetrics::values.batteryWidth,
                   MonoInkMetrics::values.batteryHeight},
              showPct);

  if (title) {
    constexpr int pad = 18;
    const int titleY = rect.y + 8;
    const int maxTitleW = batteryX - pad * 2;

    // 標題
    auto truncated = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleW, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + pad, titleY,
                      truncated.c_str(), true, EpdFontFamily::BOLD);

    // 副標題（小字，標題下方）
    if (subtitle && subtitle[0] != '\0') {
      const int subY = titleY + renderer.getLineHeight(UI_12_FONT_ID) + 2;
      auto truncSub = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxTitleW);
      renderer.drawText(SMALL_FONT_ID, rect.x + pad, subY, truncSub.c_str(), true);
    }

    // 底部分隔線（加粗 2px）
    renderer.drawLine(rect.x, rect.y + rect.height - 2,
                      rect.x + rect.width, rect.y + rect.height - 2, 2, true);
  }
}

// ── Tab Bar ─────────────────────────────────────────────────────────────────
// 選中+鎖定：黑底白字實心塊
// 選中+游標：底部 3px 粗底線（懸停感）
// 非選中：純文字，無框無線
void MonoInkTheme::drawTabBar(const GfxRenderer& renderer, Rect rect,
                               const std::vector<TabInfo>& tabs, bool selected) const {
  constexpr int hPad = 10;
  constexpr int vPad = 4;
  int currentX = rect.x + MonoInkMetrics::values.contentSidePadding;

  for (const auto& tab : tabs) {
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, tab.label, EpdFontFamily::BOLD);
    const int blockW = tw + hPad * 2;
    const int blockH = rect.height - 2;

    if (tab.selected && selected) {
      // 黑底色塊 + 白字
      renderer.fillRect(currentX, rect.y + 1, blockW, blockH, true);
      renderer.drawText(UI_10_FONT_ID, currentX + hPad, rect.y + vPad,
                        tab.label, false, EpdFontFamily::BOLD);
    } else if (tab.selected) {
      // 游標懸停：底部 3px 粗線
      renderer.fillRect(currentX, rect.y + blockH - 1, blockW, 3, true);
      renderer.drawText(UI_10_FONT_ID, currentX + hPad, rect.y + vPad,
                        tab.label, true, EpdFontFamily::BOLD);
    } else {
      renderer.drawText(UI_10_FONT_ID, currentX + hPad, rect.y + vPad,
                        tab.label, true, EpdFontFamily::REGULAR);
    }

    currentX += blockW + MonoInkMetrics::values.tabSpacing;
  }

  // 底部 1px 分隔線
  renderer.drawLine(rect.x, rect.y + rect.height - 1,
                    rect.x + rect.width, rect.y + rect.height - 1, 1, true);
}

// ── 清單 ────────────────────────────────────────────────────────────────────
// 選中列 = 整列實心黑底反白字（最強對比，e-paper 一刷清楚）
// 非選中 = 1px 細實線分隔
// 右側 value = 選中反白，非選中純文字（無框）
void MonoInkTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                             const std::function<std::string(int index)>& rowTitle,
                             const std::function<std::string(int index)>& rowSubtitle,
                             const std::function<UIIcon(int index)>& /*rowIcon*/,
                             const std::function<std::string(int index)>& rowValue,
                             bool /*highlightValue*/,
                             const std::function<bool(int index)>& /*isHeader*/) const {
  const int rowH = (rowSubtitle != nullptr)
                       ? MonoInkMetrics::values.listWithSubtitleRowHeight
                       : MonoInkMetrics::values.listRowHeight;
  const int pageItems = rect.height / rowH;
  const int totalPages = (itemCount + pageItems - 1) / pageItems;

  // 捲軸
  if (totalPages > 1) {
    const int scrollH = rect.height * pageItems / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollY =
        rect.y + (rect.height - scrollH) * currentPage / (totalPages - 1);
    const int scrollX =
        rect.x + rect.width - MonoInkMetrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollX, rect.y, scrollX, rect.y + rect.height - 1, true);
    renderer.fillRect(scrollX - MonoInkMetrics::values.scrollBarWidth, scrollY,
                      MonoInkMetrics::values.scrollBarWidth, scrollH, true);
  }

  const int contentWidth =
      rect.width -
      (totalPages > 1
           ? (MonoInkMetrics::values.scrollBarWidth + MonoInkMetrics::values.scrollBarRightOffset)
           : 0);
  const int pageStart = selectedIndex / pageItems * pageItems;
  constexpr int textIndent = 16;

  for (int i = pageStart; i < itemCount && i < pageStart + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowH;
    const bool sel = (i == selectedIndex);

    if (sel) {
      // 整列實心黑底
      renderer.fillRect(rect.x, itemY, contentWidth, rowH, true);
    } else {
      // 1px 細實線分隔（最後一列除外）
      if (i < itemCount - 1) {
        renderer.drawLine(rect.x + textIndent, itemY + rowH - 1,
                          rect.x + contentWidth - MonoInkMetrics::values.contentSidePadding,
                          itemY + rowH - 1, 1, true);
      }
    }

    const int textX = rect.x + textIndent;
    const int valueW = (rowValue != nullptr) ? 80 : 0;
    const int textWidth = contentWidth - textIndent - valueW - MonoInkMetrics::values.contentSidePadding;

    // 主標題
    const int titleY = (rowSubtitle != nullptr)
                           ? itemY + 6
                           : itemY + (rowH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    auto itemStr = renderer.truncatedText(UI_10_FONT_ID, rowTitle(i).c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, titleY, itemStr.c_str(),
                      !sel, sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    // 副標題（小字）
    if (rowSubtitle != nullptr) {
      const int subY = titleY + renderer.getLineHeight(UI_10_FONT_ID) + 2;
      auto sub = renderer.truncatedText(SMALL_FONT_ID, rowSubtitle(i).c_str(), textWidth);
      renderer.drawText(SMALL_FONT_ID, textX, subY, sub.c_str(), !sel);
    }

    // 右側值
    if (rowValue != nullptr) {
      const auto valStr = rowValue(i);
      if (!valStr.empty()) {
        const int vw = renderer.getTextWidth(UI_10_FONT_ID, valStr.c_str(),
                                             sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
        const int vx = rect.x + contentWidth - MonoInkMetrics::values.contentSidePadding - vw;
        const int valY = itemY + (rowH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
        renderer.drawText(UI_10_FONT_ID, vx, valY, valStr.c_str(),
                          !sel, sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      }
    }
  }
}

// ── 子標頭（黑底白字帶，層級感明確）─────────────────────────────────────────
// label = 左側文字（路徑、分類名）
// rightLabel = 右側文字（頁數、計數等，可 null）
void MonoInkTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect,
                                  const char* label, const char* rightLabel) const {
  if (!label) return;
  constexpr int pad = 14;

  // 整列黑底
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);

  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int textY = rect.y + (rect.height - lineH) / 2;

  // 右側文字（白字）
  int rightW = 0;
  if (rightLabel && rightLabel[0] != '\0') {
    rightW = renderer.getTextWidth(SMALL_FONT_ID, rightLabel) + pad;
    const int rx = rect.x + rect.width - pad - renderer.getTextWidth(SMALL_FONT_ID, rightLabel);
    renderer.drawText(SMALL_FONT_ID, rx, textY, rightLabel, false);
  }

  // 左側文字（白字，截斷避免與右側重疊）
  const int maxW = rect.width - pad * 2 - rightW;
  auto truncLabel = renderer.truncatedText(SMALL_FONT_ID, label, maxW);
  renderer.drawText(SMALL_FONT_ID, rect.x + pad, textY, truncLabel.c_str(), false);
}

// ── 按鍵提示（關閉，保留 stub）───────────────────────────────────────────────
void MonoInkTheme::drawButtonHints(GfxRenderer& /*renderer*/, const char* /*btn1*/,
                                    const char* /*btn2*/, const char* /*btn3*/,
                                    const char* /*btn4*/) const {}

void MonoInkTheme::drawSideButtonHints(const GfxRenderer& /*renderer*/, const char* /*topBtn*/,
                                        const char* /*bottomBtn*/) const {}

// ── 主畫面 Menu ──────────────────────────────────────────────────────────────
// sdDirCount == 2  → NORMAL 模式：前兩項為「最近閱讀（大）」+「檔案區（小）」大按鈕
// sdDirCount == 0  → RECENTS 模式：只有快捷三鍵
// buttonCount 順序：0..sdDirCount-1 = 大按鈕區，sdDirCount..end = WiFi/設定/藍芽
void MonoInkTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount,
                                   int selectedIndex,
                                   const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>& rowIcon,
                                   int sdDirCount) const {
  if (buttonCount < 1) return;

  const int W = renderer.getScreenWidth();  // 480

  // 大按鈕區高度（NORMAL 模式）
  constexpr int bigAreaH = 120;
  // 大按鈕寬度：最近閱讀 60%、檔案區 40%
  const int bigW0 = W * 40 / 100;  // 192 — 檔案區（左小）
  const int bigW1 = W - bigW0;     // 288 — 最近閱讀（右大）

  const int topAreaH = (sdDirCount > 0) ? bigAreaH : 0;

  // 快捷三鍵
  const int quickCount = std::max(0, buttonCount - sdDirCount);
  const int quickH     = rect.height - topAreaH;
  const int quickTileH = std::max(56, quickH - 8);
  const int quickTileW = quickCount > 0
      ? (W - 8 * 2 - 6 * (quickCount - 1)) / quickCount
      : W - 16;
  const int quickAreaY = rect.y + topAreaH + (quickH - quickTileH) / 2;

  // ── 輔助：畫單一磁貼 ──
  auto drawTile = [&](int idx, int tx, int ty, int tw, int th, bool isBigBtn) {
    if (idx < 0 || idx >= buttonCount || tw <= 0 || th <= 0) return;
    const bool sel = (idx == selectedIndex);

    if (sel) {
      renderer.fillRect(tx, ty, tw, th, true);
    } else {
      renderer.fillRect(tx, ty, tw, th, false);
      renderer.drawRect(tx, ty, tw, th, true);
    }

    // icon + label 垂直置中（大按鈕用 UI_12，快捷用 UI_10）
    const int fontId  = isBigBtn ? UI_12_FONT_ID : UI_10_FONT_ID;
    const uint8_t* ic = rowIcon ? monoInkMenuIcon(rowIcon(idx)) : nullptr;
    if (ic) {
      const int iconSz  = std::min(menuIconSize, th / 2);
      const int labelH  = renderer.getLineHeight(fontId);
      const int contentH = iconSz + menuIconLabelGap + labelH;
      const int iconX   = tx + (tw - iconSz) / 2;
      const int iconY   = ty + (th - contentH) / 2;
      if (sel) renderer.fillRect(iconX, iconY, iconSz, iconSz, false);
      renderer.drawIcon(ic, iconX, iconY, iconSz, iconSz);
      const auto lbl   = buttonLabel(idx);
      const auto style = sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const int lblW   = renderer.getTextWidth(fontId, lbl.c_str(), style);
      renderer.drawText(fontId, tx + (tw - lblW) / 2, iconY + iconSz + menuIconLabelGap,
                        lbl.c_str(), !sel, style);
    } else {
      const auto lbl   = buttonLabel(idx);
      const auto style = sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const auto trunc = renderer.truncatedText(fontId, lbl.c_str(), tw - 12);
      const int lblW   = renderer.getTextWidth(fontId, trunc.c_str(), style);
      const int lblH   = renderer.getLineHeight(fontId);
      renderer.drawText(fontId, tx + (tw - lblW) / 2, ty + (th - lblH) / 2,
                        trunc.c_str(), !sel, style);
    }

    if (sel) {
      for (int i = 1; i <= 2; i++)
        renderer.drawRect(tx - i, ty - i, tw + i * 2, th + i * 2, false);
    }
  };

  // ── NORMAL 模式：兩個大按鈕（最近閱讀 60% | 檔案區 40%）──
  if (sdDirCount >= 2) {
    const int bigY = rect.y;
    drawTile(0, rect.x,        bigY, bigW0, bigAreaH, true);
    drawTile(1, rect.x + bigW0, bigY, bigW1, bigAreaH, true);
  }

  // ── 分隔線 ──
  renderer.drawLine(rect.x + 8, quickAreaY - 4, rect.x + W - 8, quickAreaY - 4, 1, true);

  // ── 快捷三鍵 ──
  for (int i = 0; i < quickCount; i++) {
    const int qx = rect.x + 8 + i * (quickTileW + 6);
    drawTile(sdDirCount + i, qx, quickAreaY, quickTileW, quickTileH, false);
  }
}

// ── 主畫面書封（2欄×2排，共4格）─────────────────────────────────────────────
// 每格 ~232×240，thumb 240px 剛好填滿
void MonoInkTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                        const std::vector<RecentBook>& recentBooks,
                                        const int selectorIndex, bool& coverRendered,
                                        bool& coverBufferStored, bool& bufferRestored,
                                        std::function<bool()> storeCoverBuffer,
                                        const BookReadingStats* /*stats*/,
                                        float /*progressPercent*/) const {
  if (recentBooks.empty()) return;

  const int count = static_cast<int>(recentBooks.size());
  const bool hasSel = (selectorIndex >= 0 && selectorIndex < count);
  const int curIdx = hasSel ? selectorIndex : 0;

  if (bufferRestored) {
    coverRendered = true;
    coverBufferStored = true;
    return;
  }
  coverRendered = true;
  coverBufferStored = storeCoverBuffer();

  const int thumbLookupH = MonoInkMetrics::values.homeCoverHeight;  // 240

  constexpr int cols = 2;
  constexpr int rows = 2;
  constexpr int gap  = 8;

  const int tileW = (rect.width  - gap * (cols - 1)) / cols;   // (480-8)/2 = 236
  const int tileH = (rect.height - gap * (rows - 1)) / rows;   // (496-8)/2 = 244

  // ── 輔助：畫單一封面磁貼 ──
  auto drawBookTile = [&](int bookIdx, int tx, int ty, int tw, int th) {
    const bool sel = hasSel && (bookIdx == curIdx);

    renderer.fillRect(tx, ty, tw, th, false);
    renderer.drawRect(tx, ty, tw, th, true);

    if (bookIdx < 0 || bookIdx >= count) return;
    const RecentBook& book = recentBooks[bookIdx];

    const std::string cp = UITheme::getCoverThumbPath(book.coverBmpPath, thumbLookupH);
    bool drawn = false;
    if (!cp.empty()) {
      FsFile f;
      if (SdMan.openFileForRead("HOME", cp, f)) {
        Bitmap bmp(f);
        if (bmp.parseHeaders() == BmpReaderError::Ok) {
          renderer.drawBitmap(bmp, tx, ty, tw, th);
          drawn = true;
        }
        f.close();
      }
    }
    if (!drawn) {
      const int iconSz = std::min(32, std::min(tw, th) - 8);
      renderer.drawIcon(CoverIcon, tx + (tw - iconSz) / 2, ty + (th - iconSz) / 2, iconSz, iconSz);
    }

    // 書名條（底部黑底白字）
    std::string title = book.title.empty() ? book.path : book.title;
    if (book.title.empty()) {
      const size_t sl = title.find_last_of('/');
      if (sl != std::string::npos) title = title.substr(sl + 1);
      const size_t dt = title.find_last_of('.');
      if (dt != std::string::npos && dt > 0) title = title.substr(0, dt);
    }
    const int barH = renderer.getLineHeight(SMALL_FONT_ID) + 4;
    const int barY = ty + th - barH;
    renderer.fillRect(tx, barY, tw, barH, true);
    auto truncTitle = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), tw - 6);
    renderer.drawText(SMALL_FONT_ID, tx + 3, barY + 2, truncTitle.c_str(), false);

    // 進度條（在書名條下方，書封底部）
    if (bookIdx >= 0 && bookIdx < count) {
      const RecentBook& pb = recentBooks[bookIdx];
      if (pb.progressPercent >= 0) {
        constexpr int pbH = 4;
        const int pbY = ty + th - pbH;
        const int pbW = tw - 4;
        const int pbX = tx + 2;
        renderer.fillRect(pbX, pbY, pbW, pbH, false);
        renderer.drawRect(pbX, pbY, pbW, pbH, true);
        const int fillW = (pbW - 2) * pb.progressPercent / 100;
        if (fillW > 0) renderer.fillRect(pbX + 1, pbY + 1, fillW, pbH - 2, true);
      }
    }

    // 選中：3px 粗外框（不反黑，封面保持原色；clamp 避免負座標觸發 outside range）
    if (sel) {
      for (int i = 1; i <= 3; i++) {
        const int bx = std::max(0, tx - i);
        const int by = std::max(0, ty - i);
        renderer.drawRect(bx, by, tw + i * 2, th + i * 2, true);
      }
    }
  };

  // ── 2×2 格局（書本循環顯示，選中書固定在格 0）──
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      const int slot     = row * cols + col;
      const int bookIdx  = (curIdx + slot) % count;
      const int tx = rect.x + col * (tileW + gap);
      const int ty = rect.y + row * (tileH + gap);
      drawBookTile(bookIdx, tx, ty, tileW, tileH);
    }
  }

}

// ── 彈出視窗 ─────────────────────────────────────────────────────────────────
Rect MonoInkTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  constexpr int margin = 16;
  constexpr int y = 60;
  const int tw = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::BOLD);
  const int th = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = tw + margin * 2;
  const int h = th + margin * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  // 黑外框 + 白內底（高對比）
  renderer.fillRect(x - 3, y - 3, w + 6, h + 6, true);
  renderer.fillRect(x, y, w, h, false);

  renderer.drawText(UI_12_FONT_ID, x + (w - tw) / 2, y + margin - 2,
                    message, true, EpdFontFamily::BOLD);
  renderer.displayBuffer();
  return Rect{x, y, w, h};
}
