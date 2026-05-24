#include "BluetoothSettingsActivity.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "LanguageMapper.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BluetoothSettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<BluetoothSettingsActivity*>(param);
  self->displayTaskLoop();
}
void BluetoothSettingsActivity::displayTaskLoop() {
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
void BluetoothSettingsActivity::onEnter() {
  Activity::onEnter();
  
  renderingMutex = xSemaphoreCreateMutex();
  selectedIndex = 0;
  viewMode = ViewMode::DEVICE_LIST;
  lastError = "";
  lastScanTime = 0;
  
  // Get BLE manager instance
  try {
    btMgr = &BluetoothHIDManager::getInstance();
    Serial.printf("BT BluetoothHIDManager ready");
    
    // Restore Bluetooth persistent state on entry
    if (SETTINGS.bluetoothEnabled && !btMgr->isEnabled()) {
      Serial.printf("BT Restoring Bluetooth from settings (enabled)");
      if (btMgr->enable()) {
        lastError = "藍芽已啟用";
      } else {
        lastError = "藍芽啟用失敗";
        SETTINGS.bluetoothEnabled = 0;
        SETTINGS.saveToFile();
      }
    } else if (!SETTINGS.bluetoothEnabled && btMgr->isEnabled()) {
      Serial.printf("BT Disabling Bluetooth per settings (disabled)");
      btMgr->disable();
      lastError = "藍芽已關閉";
    }
    if (btMgr && btMgr->isEnabled()) {
      lastError = "掃描中...";
      btMgr->startScan(8000);
      lastScanTime = millis();
    }
  } catch (const std::exception& e) {
    Serial.printf("BT Failed to get BLE manager: %s", e.what());
    lastError = getChineseName("BLE manager error");
    btMgr = nullptr;
  } catch (...) {
    Serial.printf("BT Unknown error getting BLE manager");
    lastError = getChineseName("Unknown error");
    btMgr = nullptr;
  }
  
  updateRequired = true;
  xTaskCreate(&BluetoothSettingsActivity::taskTrampoline, "BluetoothSettingsActivity",
            8192,
            this,               // Parameters
            1,                  // Priority
            &displayTaskHandle  // Task handle
  );
}

void BluetoothSettingsActivity::onExit() {
  Activity::onExit();
  
  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  if (renderingMutex) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    if (displayTaskHandle) {
      vTaskDelete(displayTaskHandle);
      displayTaskHandle = nullptr;
    }
    xSemaphoreGive(renderingMutex);
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
  
  // Stop any ongoing scan
  if (btMgr && btMgr->isScanning()) {
    btMgr->stopScan();
  }
}

void BluetoothSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onComplete) onComplete();
    return;
  }

  // Check if scan completed
  if (btMgr && viewMode == ViewMode::DEVICE_LIST && !btMgr->isScanning() && lastScanTime > 0) {
    if (millis() - lastScanTime > 500) { // Small delay to see final results
      lastScanTime = 0;
      updateRequired = true;
    }
  }

  handleDeviceListInput();
}

void BluetoothSettingsActivity::handleMainMenuInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex = (selectedIndex > 0) ? selectedIndex - 1 : 1;
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex = (selectedIndex < 1) ? selectedIndex + 1 : 0;
    updateRequired = true;
  }
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (!btMgr) {
      lastError = getChineseName("BLE not available");
      Serial.printf("BT BLE manager not available");
      updateRequired = true;
      return;
    }

    if (selectedIndex == 0) {
      // Toggle Bluetooth
      try {
        if (btMgr->isEnabled()) {
          Serial.printf("BT Disabling Bluetooth...");
          if (btMgr->disable()) {
            lastError = getChineseName("Bluetooth disabled");
            SETTINGS.bluetoothEnabled = 0;
            SETTINGS.saveToFile();
          } else {
            lastError = getChineseName("Failed to disable");
          }
        } else {
          Serial.printf("BT Enabling Bluetooth...");
          if (btMgr->enable()) {
            lastError = getChineseName("Bluetooth enabled");
            SETTINGS.bluetoothEnabled = 1;
            SETTINGS.saveToFile();
          } else {
            lastError = btMgr->lastError.empty() ? getChineseName("Failed to enable") : btMgr->lastError;
          }
        }
      } catch (const std::exception& e) {
        lastError = std::string("Error: ") + e.what();
        Serial.printf("BT Toggle error: %s", e.what());
      } catch (...) {
        lastError = getChineseName("Unknown toggle error");
        Serial.printf("BT Unknown error toggling Bluetooth");
      }
      updateRequired = true;
    } else if (selectedIndex == 1) {
      // Start scan and switch to device list
      if (btMgr->isEnabled()) {
        btMgr->startScan(10000);
        lastScanTime = millis();
        viewMode = ViewMode::DEVICE_LIST;
        selectedIndex = 0;
        lastError = "";
      } else {
        lastError = getChineseName("Enable BT first");
      }
      updateRequired = true;
    }
  }
}

void BluetoothSettingsActivity::handleDeviceListInput() {
  if (!btMgr) return;

  std::vector<BluetoothDevice> filteredDevices;
  for (const auto& dev : btMgr->getDiscoveredDevicesSnapshot()) {
    if (dev.name != "Unknown" && dev.name != "mobike") {
      filteredDevices.push_back(dev);
    }
  }
  const auto& devices = filteredDevices;
  const auto& connectedDevices = btMgr->getConnectedDevices();
  const int maxIndex = devices.empty() ? 0 : static_cast<int>(devices.size()) - 1;
  if (selectedIndex > maxIndex) {
    selectedIndex = maxIndex;
  }

  if (!devices.empty() && mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex = (selectedIndex > 0) ? selectedIndex - 1 : maxIndex;
    updateRequired = true;
  } else if (!devices.empty() && mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex = (selectedIndex < maxIndex) ? selectedIndex + 1 : 0;
    updateRequired = true;
  }
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (!btMgr->isEnabled()) {
      lastError = "請先啟用藍芽";
      updateRequired = true;
      return;
    }
    Serial.printf("BT Quick rescan...");
    lastError = "掃描中...";
    btMgr->startScan(8000);
    lastScanTime = millis();
    selectedIndex = 0;
    updateRequired = true;
    return;
  }
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (connectedDevices.empty()) {
      lastError = "沒有已連線裝置";
      updateRequired = true;
      return;
    }
    Serial.printf("BT Disconnecting from all devices...");
    std::vector<std::string> deviceAddresses = connectedDevices;
    lastError = "斷開中...";
    updateRequired = true;
    for (const auto& addr : deviceAddresses) {
      Serial.printf("BT Disconnecting from %s", addr.c_str());
      btMgr->disconnectFromDevice(addr);
    }
    lastError = "已斷開";
    selectedIndex = 0;
    updateRequired = true;
    return;
  }
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (!btMgr->isEnabled()) {
      Serial.printf("BT Enabling Bluetooth from flat page...");
      lastError = "藍芽啟用中...";
      updateRequired = true;
      if (btMgr->enable()) {
        SETTINGS.bluetoothEnabled = 1;
        SETTINGS.saveToFile();
        lastError = "掃描中...";
        btMgr->startScan(8000);
        lastScanTime = millis();
      } else {
        lastError = btMgr->lastError.empty() ? "藍芽啟用失敗" : btMgr->lastError;
      }
      selectedIndex = 0;
      updateRequired = true;
      return;
    }

    if (devices.empty()) {
      Serial.printf("BT Refreshing scan...");
      lastError = "掃描中...";
      btMgr->startScan(8000);
      lastScanTime = millis();
      selectedIndex = 0;
      updateRequired = true;
      return;
    }

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(devices.size())) {
      const auto& device = devices[selectedIndex];
      
      Serial.printf("BT Connecting to %s (%s)", device.name.c_str(), device.address.c_str());
      lastError = "連線中...";
      updateRequired = true;
      
      if (btMgr->connectToDeviceWithRetries(device.address, 3)) {
        lastError = std::string("已連線 ") + device.name;
        Serial.printf("BT Successfully connected to %s", device.name.c_str());
      } else {
        lastError = btMgr->lastError.empty() ? "連線失敗" : btMgr->lastError;
        Serial.printf("BT Failed to connect after retries: %s", lastError.c_str());
      }
      updateRequired = true;
    }
  }
}

void BluetoothSettingsActivity::render() {
  renderDeviceList();
}

void BluetoothSettingsActivity::renderMainMenu() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  constexpr int sidePadding = 20;

  renderer.clearScreen();

  // Header
  renderer.drawCenteredText(UI_12_FONT_ID, 15, getChineseName("Bluetooth Settings"), true, EpdFontFamily::BOLD);

  // Status line
  std::string statusLine;
  if (btMgr) {
    if (btMgr->isEnabled()) {
      auto connDevices = btMgr->getConnectedDevices();
      char buf[64];
      snprintf(buf, sizeof(buf), getChineseName("Bluetooth enabled with devices"), connDevices.size());
      statusLine = buf;
    } else {
      statusLine = getChineseName("Bluetooth disabled");
    }
  } else {
    statusLine = getChineseName("Bluetooth Error");
  }
  statusLine = renderer.truncatedText(SMALL_FONT_ID, statusLine.c_str(), pageWidth - sidePadding * 2);
  renderer.drawText(SMALL_FONT_ID, sidePadding, 45, statusLine.c_str());

  // Error message if any
  if (!lastError.empty()) {
    const auto errorLine = renderer.truncatedText(UI_10_FONT_ID, lastError.c_str(), pageWidth - sidePadding * 2);
    renderer.drawText(UI_10_FONT_ID, sidePadding, 75, errorLine.c_str());
  }

  // Menu items
  constexpr int startY = 110;
  constexpr int lineHeight = 40;
  const char* items[] = {
      btMgr && btMgr->isEnabled() ? getChineseName("Disable Bluetooth") : getChineseName("Enable Bluetooth"),
      getChineseName("Scan devices")
  };

  for (int i = 0; i < 2; i++) {
    const int itemY = startY + i * lineHeight;
    
    // Draw selection indicator
    if (i == selectedIndex) {
      renderer.drawText(UI_10_FONT_ID, 5, itemY, ">");
    }
    
    renderer.drawText(UI_10_FONT_ID, 25, itemY, items[i]);
  }

  // Button hints
  const auto labels = mappedInput.mapLabels(getChineseName("Back"), getChineseName("Open"), getChineseName("Left label"),
                                            getChineseName("Right label"));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void BluetoothSettingsActivity::renderDeviceList() {
  const int pageWidth  = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics  = UITheme::getInstance().getMetrics();

  renderer.clearScreen();

  // ── Header（MonoInk drawHeader）──────────────────────────────────────────
  const char* headerTitle = btMgr && btMgr->isEnabled()
      ? (btMgr->isScanning() ? "藍芽  掃描中..." : "藍芽  已啟用")
      : "藍芽  已關閉";
  GUI.drawHeader(renderer, Rect{0, 0, pageWidth, metrics.headerHeight}, headerTitle);

  if (!btMgr) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "藍芽模組錯誤");
    renderer.displayBuffer();
    return;
  }

  // ── 狀態副標（已連線數 + 訊息）─────────────────────────────────────────
  constexpr int sidePad = 18;
  const int statusY = metrics.headerHeight + 6;

  std::vector<BluetoothDevice> filteredDevices;
  for (const auto& dev : btMgr->getDiscoveredDevicesSnapshot()) {
    if (dev.name != "Unknown" && dev.name != "mobike") {
      filteredDevices.push_back(dev);
    }
  }
  const auto& devices        = filteredDevices;
  const auto& connectedDevices = btMgr->getConnectedDevices();

  // 已連線徽章（黑底白字）
  if (!connectedDevices.empty()) {
    char badge[32];
    snprintf(badge, sizeof(badge), "已連線 %zu 台", connectedDevices.size());
    const int bw = renderer.getTextWidth(SMALL_FONT_ID, badge) + 12;
    const int bh = renderer.getLineHeight(SMALL_FONT_ID) + 6;
    renderer.fillRoundedRect(sidePad, statusY, bw, bh, 4, Color::Black);
    renderer.drawText(SMALL_FONT_ID, sidePad + 6, statusY + 3, badge, false);
  }

  // 訊息列（lastError / 提示）
  if (!lastError.empty()) {
    const int msgY = statusY + renderer.getLineHeight(SMALL_FONT_ID) + 10;
    const auto msg = renderer.truncatedText(SMALL_FONT_ID, lastError.c_str(), pageWidth - sidePad * 2);
    renderer.drawText(SMALL_FONT_ID, sidePad, msgY, msg.c_str(), true);
  }

  // ── 底部按鍵提示列（固定在畫面底部，MonoInk 手動繪製）────────────────
  // MonoInk drawButtonHints 是 stub，藍芽頁面有明確的四個操作需要告知使用者
  constexpr int hintBarH = 36;
  const int    barTop    = pageHeight - hintBarH;
  const char*  confirmLbl = btMgr->isEnabled() ? "確認：連線" : "確認：啟用";

  // 上方 1px 分隔線
  renderer.drawLine(0, barTop, pageWidth, barTop, 1, true);

  // 四個提示區等寬排列
  struct HintItem { const char* txt; };
  const HintItem hints[4] = { {"返回"}, {confirmLbl}, {"←刷新"}, {"→斷開"} };
  const int slotW = pageWidth / 4;
  for (int i = 0; i < 4; i++) {
    const int tw = renderer.getTextWidth(SMALL_FONT_ID, hints[i].txt);
    const int tx = i * slotW + (slotW - tw) / 2;
    const int ty = barTop + (hintBarH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
    renderer.drawText(SMALL_FONT_ID, tx, ty, hints[i].txt, true);
    // 區塊分隔（除最後一格）
    if (i < 3) {
      renderer.drawLine((i + 1) * slotW, barTop + 6,
                        (i + 1) * slotW, pageHeight - 6, 1, true);
    }
  }

  // ── 裝置清單 ────────────────────────────────────────────────────────────
  // 可用高度 = header 之後 到 底部提示列之前
  const int listStartY = metrics.headerHeight + metrics.verticalSpacing
                         + renderer.getLineHeight(SMALL_FONT_ID) + 14
                         + (!lastError.empty() ? renderer.getLineHeight(SMALL_FONT_ID) + 8 : 0);
  const int listEndY   = barTop - 4;
  constexpr int rowH   = 46;
  const int maxVisible = std::max(1, (listEndY - listStartY) / rowH);

  if (!btMgr->isEnabled()) {
    // 整塊提示：未啟用
    const int midY = listStartY + (listEndY - listStartY) / 2 - renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawCenteredText(UI_10_FONT_ID, midY, "藍芽尚未啟用", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, midY + renderer.getLineHeight(UI_10_FONT_ID) + 8,
                              "按確認鍵啟用藍芽", true);
  } else if (devices.empty()) {
    const int midY = listStartY + (listEndY - listStartY) / 2 - renderer.getLineHeight(UI_10_FONT_ID);
    const char* emptyMsg = btMgr->isScanning() ? "正在尋找裝置..." : "沒有找到可用裝置";
    renderer.drawCenteredText(UI_10_FONT_ID, midY, emptyMsg, true, EpdFontFamily::REGULAR);
    if (!btMgr->isScanning()) {
      renderer.drawCenteredText(SMALL_FONT_ID, midY + renderer.getLineHeight(UI_10_FONT_ID) + 8,
                                "按←重新掃描", true);
    }
  } else {
    int scrollOffset = 0;
    if (selectedIndex >= maxVisible) {
      scrollOffset = selectedIndex - maxVisible + 1;
    }

    int displayIdx = 0;
    for (int i = scrollOffset;
         i < static_cast<int>(devices.size()) && displayIdx < maxVisible;
         i++, displayIdx++) {
      const int rowY       = listStartY + displayIdx * rowH;
      const auto& device   = devices[i];
      const bool isSel     = (i == selectedIndex);
      const bool connected = btMgr->isConnected(device.address);

      // 選中：整列黑底
      if (isSel) {
        renderer.fillRect(0, rowY, pageWidth, rowH - 2, true);
      } else if (displayIdx > 0) {
        // 非選中分隔線
        renderer.drawLine(sidePad, rowY, pageWidth - sidePad, rowY, 1, true);
      }

      // 裝置名稱（第一行）
      char row[128];
      snprintf(row, sizeof(row), "%s%s%s",
               connected  ? "[已連線] " : "",
               device.isHID ? "[HID] " : "",
               device.name.c_str());
      const int nameMaxW = pageWidth - sidePad * 2 - 60;  // 右側留訊號強度
      const auto nameStr = renderer.truncatedText(UI_10_FONT_ID, row, nameMaxW);
      renderer.drawText(UI_10_FONT_ID, sidePad, rowY + 4, nameStr.c_str(), !isSel,
                        isSel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

      // 訊號強度（右對齊）
      const std::string sigStr = getSignalStrengthIndicator(device.rssi);
      const int sigW = renderer.getTextWidth(SMALL_FONT_ID, sigStr.c_str());
      renderer.drawText(SMALL_FONT_ID, pageWidth - sidePad - sigW, rowY + 4,
                        sigStr.c_str(), !isSel);

      // 位址縮寫（第二行，小字）
      const std::string addrShort = device.address.size() > 11
                                        ? device.address.substr(0, 11)
                                        : device.address;
      char detail[48];
      snprintf(detail, sizeof(detail), "RSSI %d  %s", device.rssi, addrShort.c_str());
      renderer.drawText(SMALL_FONT_ID, sidePad, rowY + 4 + renderer.getLineHeight(UI_10_FONT_ID) + 2,
                        detail, !isSel);
    }

    // 捲軸（若有超出可視範圍）
    if (static_cast<int>(devices.size()) > maxVisible) {
      const int sbH = (listEndY - listStartY) * maxVisible / static_cast<int>(devices.size());
      const int sbY = listStartY +
                      (listEndY - listStartY - sbH) * scrollOffset /
                      (static_cast<int>(devices.size()) - maxVisible);
      const int sbX = pageWidth - 4;
      renderer.drawLine(sbX, listStartY, sbX, listEndY, 1, true);
      renderer.fillRect(sbX - 2, sbY, 3, sbH, true);
    }
  }

  renderer.displayBuffer();
}

std::string BluetoothSettingsActivity::getSignalStrengthIndicator(const int32_t rssi) const {
  // Convert RSSI to signal bars representation (matching WiFi scanner style)
  if (rssi >= -50) {
    return "||||";  // Excellent
  }
  if (rssi >= -60) {
    return " |||";  // Good
  }
  if (rssi >= -70) {
    return "  ||";  // Fair
  }
  return "   |";  // Very weak
}
