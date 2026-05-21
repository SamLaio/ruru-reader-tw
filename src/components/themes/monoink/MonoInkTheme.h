#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// MonoInk theme — 效能優先 × 黑白強對比
// 設計原則：
//   - 選中 = 整列/整格實心黑底反白字（最強對比、e-paper 一刷清楚）
//   - 分隔線 = 1px 實線，不用虛線
//   - 主畫面 = 正面大封面置中 + 左右縮圖等比縮小（省梯形透視計算）
//   - 進度條 = 底部 4px 實心黑條，不加外框
//   - Header = 左書名 bold + 右電量，底部單線
namespace MonoInkMetrics {
constexpr ThemeMetrics values = {
    .batteryWidth = 16,
    .batteryHeight = 12,
    .topPadding = 4,
    .batteryBarHeight = 36,
    .headerHeight = 76,
    .verticalSpacing = 12,
    .contentSidePadding = 18,
    .listRowHeight = 38,
    .listWithSubtitleRowHeight = 58,
    .menuRowHeight = 60,
    .menuSpacing = 6,
    .tabSpacing = 6,
    .tabBarHeight = 36,
    .scrollBarWidth = 3,
    .scrollBarRightOffset = 4,
    .homeTopPadding = 44,
    .homeCoverHeight = 240,
    .homeCoverTileHeight = 496,
    .homeRecentBooksCount = 4,
    .homeContinueReadingInMenu = false,
    .homeMenuTopOffset = 24,
    .buttonHintsHeight = 40,
    .sideButtonHintsWidth = 30,
    .progressBarHeight = 14,
    .progressBarMarginTop = 1,
    .statusBarHorizontalMargin = 5,
    .statusBarVerticalMargin = 18,
    .keyboardKeyWidth = 22,
    .keyboardKeyHeight = 40,
    .keyboardKeySpacing = 0,
    .keyboardBottomKeyHeight = 35,
    .keyboardBottomKeySpacing = 5,
    .keyboardBottomAligned = true,
    .keyboardCenteredText = false,
    .keyboardVerticalOffset = -13,
    .keyboardTextFieldWidthPercent = 85,
    .keyboardWidthPercent = 90,
    .keyboardKeyCornerRadius = 0,
    .bookProgressBarHeight = 4,
    .versionTextRightX = 20,
    .versionTextY = 55,
};
}  // namespace MonoInkMetrics

class MonoInkTheme : public BaseTheme {
 public:
  void drawBattery(const GfxRenderer& renderer, Rect rect, bool showPercentage = true) const override;
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle = nullptr,
                const std::function<UIIcon(int index)>& rowIcon = nullptr,
                const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                const std::function<bool(int index)>& isHeader = nullptr) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon,
                      int sdDirCount = 0) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, const BookReadingStats* stats = nullptr,
                           float progressPercent = -1.0f) const override;
  Rect drawPopup(const GfxRenderer& renderer, const char* message) const override;
};
