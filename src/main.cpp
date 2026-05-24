#include <Arduino.h>
#include <EpdFontLoader.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <builtinFonts/all.h>
#include <esp_task_wdt.h>

#include <cstring>
#include <functional>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
// stage10: KOReaderCredentialStore 砍掉
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#ifndef DISABLE_OPDS
#include "activities/browser/OpdsBookBrowserActivity.h"
#endif
#include "activities/home/HomeActivity.h"
#include "activities/home/MyLibraryActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/settings/BluetoothSettingsActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
// stage10: JianGuoBrowserActivity 砍掉

#include <BluetoothHIDManager.h>
#include "util/ButtonNavigator.h"



HalDisplay display;
HalGPIO gpio;
MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
Activity* currentActivity;

namespace {
void resetWatchdog() {
  esp_task_wdt_reset();
}
}  // namespace

// stage31: 字體系統整併
// 全韌體只用 1 個字體實例（10pt 思源黑體 TC、5400+ 字常用字集）
// 所有 font ID（UI_10/UI_12/SMALL/READER_17/BOOKERLY_*/NOTOSANS_*）都別名指向 RURU_FONT_ID
// 詳見 src/fontIds.h
EpdFont ruruRegularFont(&source_han_sans_tc_10_regular);
EpdFont ruruBoldFont(&source_han_sans_tc_10_regular);  // 子集只產 Regular，Bold slot 共用
EpdFontFamily ruruFontFamily(&ruruRegularFont, &ruruBoldFont);


// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }
}

void enterNewActivity(Activity* activity) {
  currentActivity = activity;
  currentActivity->onEnter();
}

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    resetWatchdog();
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      resetWatchdog();
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    abort = gpio.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    gpio.startDeepSleep();
  }
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    resetWatchdog();
    delay(50);
    gpio.update();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  //等待渲染完成
  uint32_t waitStart = millis();
  const uint32_t MAX_WAIT_TIME = 5000; // 最多等5秒
  while (!APP_STATE.isRenderComplete) {
    resetWatchdog();
    Serial.printf("[%lu] [MAIN] Waiting for main render to complete...\n", millis());
    vTaskDelay(100 / portTICK_PERIOD_MS); // 每100ms检查一次
    
    // 超时保护：避免卡死
    if (millis() - waitStart > MAX_WAIT_TIME) {
      Serial.printf("[%lu] [MAIN] Wait timeout, proceed with PNG render\n", millis());
      break;
    }
  }
  //原逻辑

  APP_STATE.lastSleepFromReader = currentActivity && currentActivity->isReaderActivity();
  APP_STATE.saveToFile();

  //bluetooth
  try {
    auto& btMgr = BluetoothHIDManager::getInstance();
    if (btMgr.isEnabled()) {
      Serial.printf("SLP", "Disabling Bluetooth before deep sleep");
      btMgr.disable();
    }
  } catch (...) {
    Serial.printf("SLP", "Could not disable Bluetooth");
  }


  exitActivity();
  enterNewActivity(new SleepActivity(renderer, mappedInputManager));

  display.deepSleep();
  Serial.printf("[%lu] [   ] Power button press calibration value: %lu ms\n", millis(), t2 - t1);
  Serial.printf("[%lu] [   ] Entering deep sleep.\n", millis());

  gpio.startDeepSleep();
}


void onGoHome();
void onGoToMyLibraryWithPath(const std::string& path);
void onGoToRecentBooks();
void onGoToReader(const std::string& initialEpubPath, const bool returnToLibraryOnBack = false,
                  const std::function<void()>& onReaderBack = onGoHome) {
  try {
    auto& btMgr = BluetoothHIDManager::getInstance();
    if (btMgr.isEnabled()) {
      btMgr.releaseTransientCaches();
    }
  } catch (...) {
    Serial.printf("[%lu] [BT ] Failed to release BLE caches before reader\n", millis());
  }

  exitActivity();
  enterNewActivity(
      new ReaderActivity(renderer, mappedInputManager, initialEpubPath, onReaderBack, onGoToMyLibraryWithPath,
                         returnToLibraryOnBack));
}

void onGoToFileTransfer() {
  exitActivity();
  enterNewActivity(new CrossPointWebServerActivity(renderer, mappedInputManager, onGoHome));
}

void onGoToSettings() {
  exitActivity();
  enterNewActivity(new SettingsActivity(renderer, mappedInputManager, onGoHome));
}

void onGoToMyLibrary() {
  exitActivity();
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome,
                                         [](const std::string& path) { onGoToReader(path, true); }));
}

void onGoToRecentBooks() {
  exitActivity();
  enterNewActivity(new RecentBooksActivity(renderer, mappedInputManager, onGoHome,
                                           [](const std::string& path) {
                                             onGoToReader(path, false, onGoToRecentBooks);
                                           }));
}

void onGoToMyLibraryWithPath(const std::string& path) {
  exitActivity();
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome,
                                         [](const std::string& p) { onGoToReader(p, true); }, path));
}

void onGoToBrowser() {
#ifndef DISABLE_OPDS
  exitActivity();
  enterNewActivity(new OpdsBookBrowserActivity(renderer, mappedInputManager, onGoHome));
#endif
}
// stage10: 堅果雲砍掉，留空 callback 讓 HomeActivity 簽名不變
void onGoToJianGuoYun() {
  // no-op
}

void onGoToBluetooth() {
  exitActivity();
  enterNewActivity(new BluetoothSettingsActivity(renderer, mappedInputManager, onGoHome));
}

void onGoHome() {
  exitActivity();
  enterNewActivity(new HomeActivity(renderer, mappedInputManager,
                                    [](const std::string& path) { onGoToReader(path); },
                                    onGoToMyLibrary, onGoToRecentBooks,
                                    onGoToSettings, onGoToFileTransfer,
                                    onGoToBluetooth,
#ifndef DISABLE_OPDS
                                    onGoToBrowser,
#endif
                                    onGoToJianGuoYun));
}

void setupDisplayAndFonts() {
  display.begin();
  renderer.begin();
  Serial.printf("[%lu] [   ] Display initialized\n", millis());
  // stage31: 所有 font ID 都別名指向 RURU_FONT_ID，只插入一次
  // UI_10/UI_12/SMALL/READER_17/BOOKERLY_*/NOTOSANS_* 全部解析為同個 ID
  renderer.insertFont(RURU_FONT_ID, ruruFontFamily);
  Serial.printf("[%lu] [   ] Fonts setup (stage31 single-font mode)\n", millis());
}

void setup() {
    // force serial for debugging
  Serial.begin(115200);
  delay(500);
  resetWatchdog();
  Serial.printf("[%lu] [DBG] setup() start - FIRMWARE DEBUG BUILD 001\n", millis());
  Serial.flush();

  t1 = millis();

  gpio.begin();

  // Only start serial if USB connected
  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    // Wait up to 3 seconds for Serial to be ready to catch early logs
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) {
      resetWatchdog();
      delay(10);
    }
  }

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!SdMan.begin()) {
    Serial.printf("[%lu] [   ] SD card initialization failed\n", millis());
    setupDisplayAndFonts();
    exitActivity();
    enterNewActivity(new FullScreenMessageActivity(renderer, mappedInputManager, "SD card error", EpdFontFamily::BOLD));
    return;
  }

  SETTINGS.loadFromFile();
  // stage10: KOREADER_STORE 砍掉
  UITheme::getInstance().reload();

  ButtonNavigator::setMappedInputManager(mappedInputManager);
  
  // Initialize Bluetooth HID button injection only (no auto-enable on boot to preserve heap for EPUB)
  try {
    auto& btMgr = BluetoothHIDManager::getInstance();
    btMgr.setButtonInjector([](uint8_t buttonIndex) {
      gpio.injectButtonPress(buttonIndex);
    });
    Serial.printf("MAIN", "Bluetooth HID initialized with button injection");
  } catch (...) {
    Serial.printf("MAIN", "Failed to initialize Bluetooth HID");
  }

  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::PowerButton:
      // For normal wakeups, verify power button press duration
      Serial.printf("[%lu] [   ] Verifying power button press duration\n", millis());
      verifyPowerButtonDuration();
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      Serial.printf("[%lu] [   ] Wakeup reason: After USB Power\n", millis());
      gpio.startDeepSleep();
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  Serial.printf("[%lu] [   ] Starting CrossPoint version " CROSSPOINT_VERSION "\n", millis());

  setupDisplayAndFonts();
  Serial.printf("[%lu] [DBG] setupDisplayAndFonts done\n", millis());
  Serial.flush();

  EpdFontLoader::loadFontsFromSd(renderer);
  Serial.printf("[%lu] [DBG] loadFontsFromSd done\n", millis());
  Serial.flush();

  exitActivity();
  enterNewActivity(new BootActivity(renderer, mappedInputManager));

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

  // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
  // crashed (indicated by readerActivityLoadCount > 0)
  if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
      mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
        Serial.printf("home1\n");
    onGoHome();
  } else {
    // Keep openEpubPath for Flow/recent display after waking. readerActivityLoadCount is the boot-loop guard.
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    Serial.printf("reader\n");
    onGoToReader(path);
    //onGoHome();
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}



void loop() {
  resetWatchdog();
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.update();

    // Check for Bluetooth inactivity timeouts and auto-reconnect
  try {
    BluetoothHIDManager::getInstance().updateActivity();
    if (!(currentActivity && currentActivity->isReaderActivity())) {
      BluetoothHIDManager::getInstance().checkAutoReconnect();
    }
  } catch (...) {
    // Ignore errors in Bluetooth management
  }

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    Serial.printf("[%lu] [MEM] Free: %d bytes, Total: %d bytes, Min Free: %d bytes\n", millis(), ESP.getFreeHeap(),
                  ESP.getHeapSize(), ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  // Check for physical button presses, virtual button presses, or activity prevention
  bool hasActivity = gpio.wasAnyPressed() || gpio.wasAnyReleased() || 
                     (currentActivity && currentActivity->preventAutoSleep());
  
  // Also check for recent BLE activity to prevent power sleep during BLE use
  try {
    const auto& btMgr = BluetoothHIDManager::getInstance();
    if (btMgr.isEnabled()) {
      // If BLE is enabled, check if there's been recent activity
      // We consider that activity if the manager has been tracking it
      // (This prevents the system from sleeping while using BLE controller)
      hasActivity = hasActivity || btMgr.hasRecentActivity();
    }
  } catch (...) {
    // Ignore BLE check errors
  }
  
  if (hasActivity) {
    lastActivityTime = millis();  // Reset inactivity timer
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    Serial.printf("[%lu] [SLP] Auto-sleep triggered after %lu ms of inactivity\n", millis(), sleepTimeoutMs);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  const unsigned long activityStartTime = millis();
  if (currentActivity) {
    resetWatchdog();
    currentActivity->loop();
    resetWatchdog();
  }
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      Serial.printf("[%lu] [LOOP] New max loop duration: %lu ms (activity: %lu ms)\n", millis(), maxLoopDuration,
                    activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();  // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    delay(10);  // Normal delay when no activity requires fast response
  }
}
