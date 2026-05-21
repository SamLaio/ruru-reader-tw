#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"
//清理字体内存
#include "CustomEpdFont.h"

void HomeActivity::taskTrampoline(void* param) {
  auto* self = static_cast<HomeActivity*>(param);
  self->displayTaskLoop();
}

int HomeActivity::getMenuItemCount() const {
  if (homeMode == HomeMode::RECENTS) {
    // RECENTS 模式：只有書本可選
    return static_cast<int>(recentBooks.size());
  }
  // NORMAL 模式：書本 + 2個大按鈕（檔案區/最近閱讀）+ 快捷三鍵
  return static_cast<int>(recentBooks.size()) + 2 + 3;
}


void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  int skippedMissing = 0;
  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!SdMan.exists(book.path.c_str())) {
      skippedMissing++;
      continue;
    }

    recentBooks.push_back(book);
  }

  // stage15.57: FLOW 冷開機/睡醒後不可因 SD exists 全部誤判而整排空白。
  // 若 recent.bin 有資料但每本都被判不存在，保留原清單顯示；真正開書時 ReaderActivity 仍會檢查檔案。
  if (recentBooks.empty() && !books.empty()) {
    Serial.printf("[%lu] [HOME] All recent paths failed exists check (%d). Keeping recent list for Flow.\n",
                  millis(), skippedMissing);
    for (const RecentBook& book : books) {
      if (recentBooks.size() >= maxBooks) {
        break;
      }
      recentBooks.push_back(book);
    }
  }

}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  // stage15.25 (嚕寶要求：書本不用點進去也要先抓封面)
  //   原本 if (!coverBmpPath.empty()) 才抓 → 沒點進去過的書永遠拿不到 coverBmpPath
  //   現在改：不管 coverBmpPath 有沒有、只要 cache 不存在就主動 load epub 拿 metadata + 生 thumb
  int progress = 0;
  for (RecentBook& book : recentBooks) {
    // 先看「已知 coverBmpPath」對應的 cache 在不在
    bool needBuildThumb = true;
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (SdMan.exists(coverPath.c_str())) {
        needBuildThumb = false;  // 已有 cache、不用建
      }
    }

    if (!needBuildThumb) {
      progress++;
      continue;
    }

    // 進來表示：要嘛 coverBmpPath 空（沒點進去過）、要嘛 cache 不在
    // 兩種狀況都要 load epub/xtc 拿 metadata + 生 thumb
    if (StringUtils::checkFileExtension(book.path, ".epub")) {
      Epub epub(book.path, "/.crosspoint");
      epub.load(false, true);  // Skip CSS、只要 metadata

      if (!showingLoading) {
        showingLoading = true;
        popupRect = GUI.drawPopup(renderer, "Loading...");
      }
      GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
      bool success = epub.generateThumbBmp(coverHeight);
      if (success) {
        // 拿到 thumb 後、回寫 metadata 到 RECENT_BOOKS（含 coverBmpPath、title、author）
        const std::string thumbPath = epub.getThumbBmpPath();
        const std::string title = epub.getTitle();
        const std::string author = epub.getAuthor();
        RECENT_BOOKS.updateBook(book.path, title, author, thumbPath);
        book.coverBmpPath = thumbPath;
        if (!book.title.empty() && title.empty()) {
          // 保留既有 title（避免 metadata 為空時清空）
        } else if (!title.empty()) {
          book.title = title;
          book.author = author;
        }
      } else {
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
        book.coverBmpPath = "";
      }
      coverRendered = false;
      updateRequired = true;
    } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
               StringUtils::checkFileExtension(book.path, ".xtc")) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        if (!showingLoading) {
          showingLoading = true;
          popupRect = GUI.drawPopup(renderer, "Loading...");
        }
        GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
        bool success = xtc.generateThumbBmp(coverHeight);
        if (success) {
          const std::string thumbPath = xtc.getThumbBmpPath();
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, thumbPath);
          book.coverBmpPath = thumbPath;
        } else {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
          book.coverBmpPath = "";
        }
        coverRendered = false;
        updateRequired = true;
      }
    }
    progress++;
  }

  // 讀進度百分比（v4 recent.bin 已快取則跳過 epub.load，只補還沒有的）
  for (RecentBook& book : recentBooks) {
    if (book.progressPercent >= 0) continue;  // 已從 recent.bin v4 讀到，不重算
    if (!StringUtils::checkFileExtension(book.path, ".epub")) continue;
    Epub epub(book.path, "/.crosspoint");
    if (!epub.load(false, true)) continue;
    const int spineCount = epub.getSpineItemsCount();
    if (spineCount <= 0) continue;
    FsFile pf;
    if (!SdMan.openFileForRead("HOME_PROG", epub.getCachePath() + "/progress.bin", pf)) continue;
    uint8_t data[6] = {0};
    const int dataSize = pf.read(data, sizeof(data));
    pf.close();
    if (dataSize != 4 && dataSize != 6) continue;
    const int spineIdx = data[0] + (data[1] << 8);
    if (spineIdx < 0 || spineIdx >= spineCount) continue;
    book.progressPercent = spineIdx * 100 / spineCount;
    RECENT_BOOKS.updateProgress(book.path, book.progressPercent);  // 回寫到 v4 recent.bin
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::loadSdDirs() {
  sdDirs.clear();
  constexpr int maxDirs = 8;  // 最多顯示 8 個目錄
  auto root = SdMan.open("/");
  if (!root) return;
  root.rewindDirectory();
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    if (file.isDirectory()) {
      char name[256];
      file.getName(name, sizeof(name));
      // 跳過隱藏目錄（.crosspoint 等）
      if (name[0] != '.') {
        sdDirs.emplace_back(name);
      }
    }
    file.close();
    if (static_cast<int>(sdDirs.size()) >= maxDirs) break;
  }
  root.close();
}

void HomeActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  hasjianguoUrl = strlen(SETTINGS.jgUsername) > 0;

  selectorIndex = 0;

  auto metrics = UITheme::getInstance().getMetrics();

  // stage21: 保留上一次的 coverBuffer（從閱讀器返回時可直接復原畫面）
  // 只在書單有實際變化時才清掉 buffer；先記住書單舊長度
  const int prevBookCount = static_cast<int>(recentBooks.size());
  const std::string prevFirstPath = recentBooks.empty() ? "" : recentBooks[0].path;

  loadRecentBooks(metrics.homeRecentBooksCount);
  loadSdDirs();

  // 書單改了（新書被開啟，排序變了）→ 舊 buffer 畫面不符，清掉
  const bool bookListChanged = (static_cast<int>(recentBooks.size()) != prevBookCount) ||
                               (!recentBooks.empty() && recentBooks[0].path != prevFirstPath);
  if (bookListChanged) {
    freeCoverBuffer();
  }

  // stage15.6: 封面預載 — 在 onEnter 階段就把 cover thumb 載 RAM
  //            原本要等第一次 render 完才開始載、會看到「先空白、後跳出圖」的閃
  //            預載後第一次 render 就直接畫出來
  if (!recentBooks.empty()) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&HomeActivity::taskTrampoline, "HomeActivityTask",
              8192,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  // coverBuffer is kept alive across onExit/onEnter so returning from reader avoids re-reading SD.
  // It is freed in onEnter if the book list changes, or in the destructor (stack unwind).
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const bool prevPressed = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                           mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextPressed = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                           mappedInput.wasReleased(MappedInputManager::Button::Right);

  const int menuCount = getMenuItemCount();

  // RECENTS 模式按 Back → 回 NORMAL
  if (homeMode == HomeMode::RECENTS &&
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    homeMode = HomeMode::NORMAL;
    selectorIndex = 0;
    coverRendered = false;
    coverBufferStored = false;
    updateRequired = true;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int bookCount = static_cast<int>(recentBooks.size());
    if (homeMode == HomeMode::RECENTS) {
      // RECENTS 模式：只有書本，直接開書
      if (selectorIndex < bookCount) {
        onSelectBook(recentBooks[selectorIndex].path);
      }
    } else {
      // NORMAL 模式：書封可選開書 + 2大按鈕 + 快捷三鍵
      if (selectorIndex < bookCount) {
        // 書封區 → 直接開書
        onSelectBook(recentBooks[selectorIndex].path);
      } else {
        const int menuIdx = selectorIndex - bookCount;
        if (menuIdx == 0) {
          // 檔案區 → MyLibrary
          onMyLibraryOpen();
        } else if (menuIdx == 1) {
          // 最近閱讀 → 切換到 RECENTS 模式
          homeMode = HomeMode::RECENTS;
          selectorIndex = 0;
          coverRendered = false;
          coverBufferStored = false;
          updateRequired = true;
        } else if (menuIdx >= 2) {
          const int quickIdx = menuIdx - 2;
          if (quickIdx == 0) onFileTransferOpen();
          else if (quickIdx == 1) onSettingsOpen();
          else if (quickIdx == 2) onBluetoothOpen();
        }
      }
    }
  } else if (prevPressed) {
    selectorIndex = (selectorIndex + menuCount - 1) % menuCount;
    coverBufferStored = false;  // selectorIndex 改變，封面需要重繪
    updateRequired = true;
  } else if (nextPressed) {
    selectorIndex = (selectorIndex + 1) % menuCount;
    coverBufferStored = false;  // selectorIndex 改變，封面需要重繪
    updateRequired = true;
  }
}

void HomeActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void HomeActivity::render() {
  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  const char* headerTitle    = (homeMode == HomeMode::RECENTS) ? "最近閱讀" : nullptr;
  const char* headerSubtitle = (homeMode == HomeMode::RECENTS) ? "< Back" : nullptr;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, headerTitle, headerSubtitle);

  const std::vector<UIIcon> quickIcons = {UIIcon::Wifi, UIIcon::Settings, UIIcon::Hotspot};
  const std::vector<const char*> quickLabels = {"WiFi", "設定", "藍芽"};
  const int bookCount = static_cast<int>(recentBooks.size());

  if (homeMode == HomeMode::RECENTS) {
    // ── RECENTS 模式：整個畫面都是書封（含進度條），無快捷鍵 ──
    const int coverH = pageHeight - metrics.homeTopPadding;
    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, coverH},
                            recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this));
  } else {
    // ── NORMAL 模式：書封 + 兩個大按鈕 + 快捷三鍵 ──
    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                            recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this));

    const int coverBottomY = metrics.homeTopPadding + metrics.homeCoverTileHeight;
    const int menuRectY = coverBottomY + 8;
    const int menuRectH = pageHeight - menuRectY - 4;
    const int menuSelIdx = selectorIndex - bookCount;
    GUI.drawButtonMenu(
        renderer,
        Rect{0, menuRectY, pageWidth, menuRectH},
        5, menuSelIdx,
        [&quickLabels](int index) -> std::string {
          if (index == 0) return "檔案區";
          if (index == 1) return "最近閱讀";
          return std::string(quickLabels[index - 2]);
        },
        [&quickIcons](int index) -> UIIcon {
          if (index == 0) return UIIcon::Folder;
          if (index == 1) return UIIcon::Book;
          return quickIcons[index - 2];
        },
        2);
  }

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    updateRequired = true;
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}
