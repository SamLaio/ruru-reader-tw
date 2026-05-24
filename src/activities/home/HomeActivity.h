#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <vector>

#include "../Activity.h"
#include "./MyLibraryActivity.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
 public:
  enum class HomeMode { NORMAL, RECENTS };

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int selectorIndex = 0;
  HomeMode homeMode = HomeMode::NORMAL;
  bool updateRequired = false;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasjianguoUrl = false;
  bool coverRendered = false;
  bool coverBufferStored = false;
  uint8_t* coverBuffer = nullptr;
  std::vector<RecentBook> recentBooks;
  std::vector<std::string> sdDirs;
  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onMyLibraryOpen;
  const std::function<void()> onRecentsOpen;
  const std::function<void()> onSettingsOpen;
  const std::function<void()> onFileTransferOpen;
  const std::function<void()> onBluetoothOpen;
#ifndef DISABLE_OPDS
  const std::function<void()> onOpdsBrowserOpen;
#endif
  const std::function<void()> onJianGuoYunOpen;


  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();
  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);
  void loadSdDirs();

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        const std::function<void(const std::string& path)>& onSelectBook,
                        const std::function<void()>& onMyLibraryOpen, const std::function<void()>& onRecentsOpen,
                        const std::function<void()>& onSettingsOpen, const std::function<void()>& onFileTransferOpen,
                        const std::function<void()>& onBluetoothOpen,
#ifndef DISABLE_OPDS
                        const std::function<void()>& onOpdsBrowserOpen,
#endif
                        const std::function<void()>& onJianGuoYunOpen)
      : Activity("Home", renderer, mappedInput),
        onSelectBook(onSelectBook),
        onMyLibraryOpen(onMyLibraryOpen),
        onRecentsOpen(onRecentsOpen),
        onSettingsOpen(onSettingsOpen),
        onFileTransferOpen(onFileTransferOpen),
        onBluetoothOpen(onBluetoothOpen),
#ifndef DISABLE_OPDS
        onOpdsBrowserOpen(onOpdsBrowserOpen),
#endif
        onJianGuoYunOpen(onJianGuoYunOpen) {}
  ~HomeActivity() { freeCoverBuffer(); }
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
