#include "BluetoothHIDManager.h"
#include <NimBLEDevice.h>
#include <HalGPIO.h>
#include <WiFi.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <algorithm>
#include <utility>
#include "../../src/CrossPointSettings.h"

// HID Service and characteristic UUIDs
static const char* HID_SERVICE_UUID = "1812";
static const char* HID_REPORT_UUID = "2A4D";
static const char* HID_INFO_UUID = "2A4A";

static constexpr uint8_t HID_KEY_ARROW_RIGHT = 0x4F;
static constexpr uint8_t HID_KEY_ARROW_LEFT = 0x50;
static constexpr uint8_t HID_KEY_ARROW_DOWN = 0x51;
static constexpr uint8_t HID_KEY_ARROW_UP = 0x52;
static constexpr uint8_t HID_KEY_ENTER = 0x28;
static constexpr uint8_t HID_KEY_ESCAPE = 0x29;
static constexpr uint8_t HID_KEY_BACKSPACE = 0x2A;
static constexpr uint8_t HID_KEY_SPACE = 0x2C;

// Global static for singleton
static BluetoothHIDManager* g_instance = nullptr;

// Scan callbacks for NimBLE 2.x - keep as static to ensure it stays alive
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    Serial.printf("BT onResult callback triggered!\n");
    if (g_instance) {
      // onScanResult expects non-const pointer, need to cast
      g_instance->onScanResult(const_cast<NimBLEAdvertisedDevice*>(advertisedDevice));
    } else {
      Serial.printf("BT onResult called but g_instance is NULL!");
    }
  }
  
  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    Serial.printf("BT onScanEnd callback: %d devices, reason: %d", results.getCount(), reason);
  }
};

// Static instance to keep callbacks alive during scan
static ScanCallbacks scanCallbacks;

// Client connection callbacks
class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) override {
    Serial.printf("BT Client connected: %s", pClient->getPeerAddress().toString().c_str());
  }
  
  void onDisconnect(NimBLEClient* pClient, int reason) override {
    if (g_instance) {
      g_instance->handleClientDisconnect(pClient, reason);
    }
  }
};

BluetoothHIDManager& BluetoothHIDManager::getInstance() {
  if (!g_instance) {
    g_instance = new BluetoothHIDManager();
    Serial.printf("BT BluetoothHIDManager instance created");
  }
  return *g_instance;
}

BluetoothHIDManager::BluetoothHIDManager() {
  Serial.printf("BT BluetoothHIDManager constructor");
  _stateMutex = xSemaphoreCreateMutex();
  _connectedDevices.reserve(4);
  _discoveredDevices.reserve(24);
}

BluetoothHIDManager::~BluetoothHIDManager() {
  cleanup();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
    _stateMutex = nullptr;
  }
}

void BluetoothHIDManager::cleanup() {
  if (_enabled) {
    disable();
  }
}

bool BluetoothHIDManager::lockState(TickType_t ticks) const {
  return !_stateMutex || xSemaphoreTake(_stateMutex, ticks) == pdTRUE;
}

void BluetoothHIDManager::unlockState() const {
  if (_stateMutex) {
    xSemaphoreGive(_stateMutex);
  }
}

bool BluetoothHIDManager::enable() {
  if (_enabled) {
    Serial.printf("BT Already enabled");
    return true;
  }
  
  Serial.printf("BT Enabling Bluetooth...");
  
  // CRITICAL: Disable WiFi when enabling Bluetooth
  // ESP32-C3 cannot have both WiFi and BLE enabled simultaneously
  if (WiFi.getMode() != WIFI_OFF) {
    Serial.printf("BT Disabling WiFi to enable Bluetooth (mutual exclusion)");
    WiFi.disconnect(true);  // true = turn off WiFi radio
    WiFi.mode(WIFI_OFF);
    delay(100);  // Brief delay to ensure WiFi is fully powered down
  }
  
  try {
    // Initialize NimBLE stack
    NimBLEDevice::init("CrossPoint");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // +9dBm
    NimBLEDevice::setSecurityAuth(true, false, true);
    
    _enabled = true;
    lastError = "";
    
    Serial.printf("BT Bluetooth enabled successfully");
    loadState();
    loadKeyMappings();
    
    // NOTE: background auto‑connect was removed in order to conserve memory
    // and avoid multiple concurrent connection attempts.  callers such as
    // main.cpp or the settings UI should invoke connectToDeviceWithRetries()
    // explicitly when appropriate.
    
    return true;
  } catch (const std::exception& e) {
    Serial.printf("BT Failed to enable Bluetooth: %s", e.what());
    lastError = std::string("Init failed: ") + e.what();
    _enabled = false;
    return false;
  } catch (...) {
    Serial.printf("BT Failed to enable Bluetooth: unknown error");
    lastError = "Init failed: unknown error";
    _enabled = false;
    return false;
  }
}

bool BluetoothHIDManager::disable() {
  if (!_enabled) {
    Serial.printf("BT Already disabled");
    return true;
  }
  
  Serial.printf("BT Disabling Bluetooth...");
  
  if (_scanning) {
    stopScan();
  }
  delay(500);

  _enabled = false;
  
  // Disconnect all devices
  while (true) {
    std::string address;
    if (lockState(pdMS_TO_TICKS(200))) {
      if (!_connectedDevices.empty()) {
        address = _connectedDevices.front().address;
      }
      unlockState();
    }
    if (address.empty()) {
      break;
    }
    disconnectFromDevice(address);
  }
  delay(200);
  
  // Deinitialize NimBLE stack
  NimBLEDevice::deinit(true);
  
  lastError = "";
  
  Serial.printf("BT Bluetooth disabled");
  return true;
}

void BluetoothHIDManager::startScan(uint32_t durationMs) {
  if (!_enabled) {
    Serial.printf("BT Cannot scan: enabled=%d scanning=%d", _enabled, _scanning);
    return;
  }
  if (_scanning) {
    stopScan();
    delay(50);
  }
  
  Serial.printf("BT Starting BLE scan for %lu ms", durationMs);
  _scanning = true;
  if (lockState(pdMS_TO_TICKS(200))) {
    _discoveredDevices.clear();
    unlockState();
  }
  
  try {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (!pScan) {
      Serial.printf("BT Failed to get scan object");
      _scanning = false;
      _scanEndTime = 0;
      return;
    }

    Serial.printf("BT Setting up scan callbacks...");
    // Use static callbacks object to ensure it stays alive
    pScan->setScanCallbacks(&scanCallbacks, false);
    pScan->clearResults();
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    
    Serial.printf("BT Starting continuous scan (duration: 0 = continuous)...");
    // In NimBLE 2.x, duration=0 means scan continuously until stop() is called
    // Parameter 1: 0 = continuous scan
    // Parameter 2: isContinue (false = clear old results)
    bool started = pScan->start(0, false);

    if (!started) {
      Serial.printf("BT Failed to start scan!");
      _scanning = false;
      _scanEndTime = 0;
      return;
    }

    _scanEndTime = durationMs > 0 ? millis() + durationMs : 0;
    Serial.printf("BT Scan started for %lu ms", durationMs);
  } catch (const std::exception& e) {
    Serial.printf("BT Scan failed: %s", e.what());
    _scanning = false;
    _scanEndTime = 0;
    lastError = std::string("Scan failed: ") + e.what();
  }
}


void BluetoothHIDManager::stopScan() {
  if (!_scanning) return;
  
  Serial.printf("BT Stopping scan");
  
  try {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan) {
      pScan->stop();
    }
  } catch (...) {
    Serial.printf("BT Error stopping scan");
  }
  
  _scanning = false;
  _scanEndTime = 0;
  const auto discoveredCount = getDiscoveredDevicesSnapshot().size();
  Serial.printf("BT Scan complete, found %u devices", static_cast<unsigned>(discoveredCount));
}

void BluetoothHIDManager::releaseTransientCaches() {
  if (!_enabled) {
    return;
  }

  if (_scanning) {
    stopScan();
    delay(80);
  }

  try {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan) {
      pScan->clearResults();
    }
  } catch (...) {
    Serial.printf("BT Error clearing scan cache");
  }

  if (lockState(pdMS_TO_TICKS(200))) {
    _discoveredDevices.clear();
    _discoveredDevices.shrink_to_fit();
    _discoveredDevices.reserve(8);

    for (auto& dev : _connectedDevices) {
      dev.reportChars.clear();
      dev.reportChars.shrink_to_fit();
    }
    unlockState();
  }

  Serial.printf("BT Released transient BLE caches");
}

void BluetoothHIDManager::onScanResult(NimBLEAdvertisedDevice* advertisedDevice) {
  if (!advertisedDevice) return;
  
  std::string address = advertisedDevice->getAddress().toString();
  std::string name = advertisedDevice->getName();
  int rssi = advertisedDevice->getRSSI();
  uint8_t addressType = advertisedDevice->getAddressType();
  
  // Check if device advertises HID service
  bool isHID = advertisedDevice->isAdvertisingService(NimBLEUUID(HID_SERVICE_UUID));
  
  if (!lockState(pdMS_TO_TICKS(50))) {
    return;
  }

  bool added = false;
  for (auto& dev : _discoveredDevices) {
    if (dev.address == address) {
      dev.rssi = rssi;
      dev.addressType = addressType;
      if (isHID) dev.isHID = true;
      unlockState();
      return;
    }
  }
  
  BluetoothDevice device;
  device.address = address;
  device.name = name.empty() ? "Unknown" : name;
  device.rssi = rssi;
  device.addressType = addressType;
  device.isHID = isHID;

  _discoveredDevices.push_back(device);
  added = true;
  unlockState();

  if (added) {
    const std::string prefix = (address.size() >= 8) ? address.substr(0, 8) : address;
    Serial.printf("BT Scan device: %s (%s) prefix=%s RSSI:%d HID:%d",
      device.name.c_str(), device.address.c_str(), prefix.c_str(), rssi, isHID);
  
    Serial.printf("BT Found device: %s (%s) RSSI:%d HID:%d",
            device.name.c_str(), device.address.c_str(), rssi, isHID);
  }
}



bool BluetoothHIDManager::connectToDevice(const std::string& address) {
  if (!_enabled) {
    Serial.printf("BT Cannot connect: Bluetooth not enabled");
    lastError = "Bluetooth not enabled";
    return false;
  }
  if (address.empty()) {
    Serial.printf("BT Invalid address (empty) passed to connectToDevice");
    lastError = "Invalid address";
    return false;
  }
  if (_scanning) {
    Serial.printf("BT Stop scan before connecting");
    stopScan();
    delay(350);
  }
  
  // Check if already connected
  if (isConnected(address)) {
    Serial.printf("BT Already connected to %s", address.c_str());
    return true;
  }
  
  // Prefer scanned metadata, but do not require HID to be present in the advertisement.
  // Some page turners only reveal HID service after connection.
  bool seen = false;
  uint8_t addressType = BLE_ADDR_PUBLIC;
  std::string scannedName;
  if (lockState(pdMS_TO_TICKS(500))) {
    for (const auto& dev : _discoveredDevices) {
      if (dev.address == address) {
        seen = true;
        addressType = dev.addressType;
        scannedName = dev.name;
        break;
      }
    }
    unlockState();
  }
  // 保留攔截：不在掃描結果裡 → 直接返回失敗
  if (!seen) {
    Serial.printf("BT Device %s not in scan results (skipping requirement)", address.c_str());
    return false;
  }
  
  Serial.printf("BT Connecting to device %s", address.c_str());
  
  NimBLEClient* pClient = nullptr;
  try {
    pClient = NimBLEDevice::createClient();
    if (!pClient) {
      lastError = "Failed to create client";
      Serial.printf("BT Failed to create BLE client");
      return false;
    }
    pClient->setSelfDelete(true, true);
    pClient->setConnectTimeout(8000);
    pClient->setConnectionParams(12, 24, 0, 400, 80, 80);

    static ClientCallbacks clientCallbacks;
    pClient->setClientCallbacks(&clientCallbacks, false);

    if (!pClient->connect(NimBLEAddress(address, addressType), true, false, false)) {
      lastError = "Connection failed";
      Serial.printf("BT Failed to connect to %s type=%u", address.c_str(), addressType);
      // pClient self-deletes on connection failure. Do not touch it here.
      delay(80);
      return false;
    }

    Serial.printf("BT Connected, discovering services...");
    
    NimBLERemoteService* pService = pClient->getService(HID_SERVICE_UUID);
    if (!pService) {
      lastError = "HID service not found";
      Serial.printf("BT Device %s doesn't have HID service", address.c_str());
      pClient->disconnect();
      delay(80);
      return false;
    }

    Serial.printf("BT Found HID service, enumerating report characteristics...");
    
    auto pCharacteristics = pService->getCharacteristics(true);
    NimBLERemoteCharacteristic* pReportChar = nullptr;
    
    int reportCount = 0;
    std::vector<NimBLERemoteCharacteristic*> reportChars;
    
    for (auto it = pCharacteristics.begin(); it != pCharacteristics.end(); ++it) {
      auto* pChar = *it;
      Serial.printf("BT Characteristic UUID: %s, canRead:%d canWrite:%d canNotify:%d canIndicate:%d",
              pChar->getUUID().toString().c_str(),
              pChar->canRead(), pChar->canWrite(), pChar->canNotify(), pChar->canIndicate());
      
      if (pChar->getUUID().equals(NimBLEUUID(HID_REPORT_UUID))) {
        reportCount++;
        Serial.printf("BT Found Report char #%d, notify:%d indicate:%d UUID:%s", 
                reportCount, pChar->canNotify(), pChar->canIndicate(),
                pChar->getUUID().toString().c_str());
        
        if (pChar->canNotify() || pChar->canIndicate()) {
          reportChars.push_back(pChar);
          Serial.printf("BT Added Report char #%d for subscription", reportCount);
        }
      }
    }
    
    if (reportChars.empty()) {
      lastError = "No input report characteristic found";
      Serial.printf("BT No Report characteristic with notify/indicate found");
      pClient->disconnect();
      delay(80);
      return false;
    }
    
    Serial.printf("BT Subscribing to %d Report characteristics...", reportChars.size());
    
    for (size_t i = 0; i < reportChars.size(); i++) {
      auto* pChar = reportChars[i];
      Serial.printf("BT Subscribing to Report char #%d...", i + 1);
      bool subResult = pChar->subscribe(true, onHIDNotify);
      Serial.printf("BT Report char #%d subscribe result: %d", i + 1, subResult);
      if (!subResult) {
        Serial.printf("BT Failed to subscribe to Report char #%d (continuing)", i + 1);
      }
    }
    
    ConnectedDevice connDev;
    connDev.address = address;
    connDev.client = pClient;
    connDev.subscribed = true;
    connDev.lastActivityTime = millis();
    connDev.wasConnected = true;
    
    connDev.name = scannedName;
    if (!connDev.name.empty()) {
      Serial.printf("BT Device found in scan results: %s (%s)", connDev.name.c_str(), address.c_str());
    } else {
      Serial.printf("BT Device not in scan results (may be previously paired): %s", address.c_str());
    }
    
    connDev.profile = DeviceProfiles::findDeviceProfile(address.c_str(), connDev.name.c_str());
    
    if (connDev.profile) {
      Serial.printf("BT ✓ Using device profile: %s (byte[%d] for keycode)", 
              connDev.profile->name, connDev.profile->reportByteIndex);
    } else {
      Serial.printf("BT No known profile matched for %s, will auto-detect from HID codes", address.c_str());
    }
    
    if (lockState(pdMS_TO_TICKS(500))) {
      _connectedDevices.push_back(connDev);
      unlockState();
    } else {
      lastError = "Bluetooth state busy";
      pClient->disconnect();
      delay(80);
      return false;
    }
    
    Serial.printf("BT Successfully connected to %s", address.c_str());
    lastError = "Connected";

    saveLastConnectedDevice(address, connDev.name);
    saveState();  // 更新多台配對清單
    releaseTransientCaches();
    
    return true;
    
  } catch (const std::exception& e) {
    lastError = std::string("Connection error: ") + e.what();
    Serial.printf("BT %s", lastError.c_str());
    if (pClient) {
      try {
        pClient->disconnect();
      } catch (...) {
        Serial.printf("BT Warning: failed to cleanup client after exception");
      }
    }
    return false;
  } catch (...) {
    lastError = "Unknown connection error";
    Serial.printf("BT %s", lastError.c_str());
    if (pClient) {
      try {
        pClient->disconnect();
      } catch (...) {
        Serial.printf("BT Warning: failed to cleanup client after unknown exception");
      }
    }
    return false;
  }
}

// Simple retry wrapper around connectToDevice.  It invokes the single-
// attempt function up to `maxAttempts` times with a short delay between
// tries.  This keeps higher-level code (UI) from having to loop itself.
bool BluetoothHIDManager::connectToDeviceWithRetries(const std::string& address, int maxAttempts) {
  if (!_enabled) {
    Serial.printf("BT Cannot connect (retries): Bluetooth not enabled");
    lastError = "Bluetooth not enabled";
    return false;
  }
  if (address.empty()) {
    Serial.printf("BT Invalid address (empty) passed to connectToDeviceWithRetries");
    lastError = "Invalid address";
    return false;
  }
  if (isConnected(address)) {
    Serial.printf("BT Already connected to %s (retries)", address.c_str());
    return true;
  }
  if (maxAttempts <= 0) maxAttempts = 1;
  for (int i = 0; i < maxAttempts; ++i) {
    Serial.printf("BT retry %d/%d for %s", i + 1, maxAttempts, address.c_str());
    if (connectToDevice(address)) {
      return true;
    }
    delay(200);
  }
  return false;
}

bool BluetoothHIDManager::disconnectFromDevice(const std::string& address) {
  Serial.printf("BT Disconnecting from device %s", address.c_str());

  NimBLEClient* client = nullptr;
  if (lockState(pdMS_TO_TICKS(500))) {
    auto it = std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
      [&address](const ConnectedDevice& dev) { return dev.address == address; });

    if (it != _connectedDevices.end()) {
      client = it->client;
      _connectedDevices.erase(it);
    }
    unlockState();
  }

  if (client) {
    try {
      Serial.printf("BT Calling disconnect on client...");
      client->disconnect();
    } catch (const std::exception& e) {
      Serial.printf("BT Error during disconnect: %s", e.what());
    } catch (...) {
      Serial.printf("BT Unknown error during disconnect");
    }

    Serial.printf("BT Disconnected from %s", address.c_str());
    return true;
  }
  
  Serial.printf("BT Device %s not in connected list", address.c_str());
  return false;
}

bool BluetoothHIDManager::isConnected(const std::string& address) const {
  bool connected = false;
  if (lockState(pdMS_TO_TICKS(200))) {
    connected = std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
      [&address](const ConnectedDevice& dev) { return dev.address == address && dev.client != nullptr; }) !=
      _connectedDevices.end();
    unlockState();
  }
  return connected;
}

std::vector<std::string> BluetoothHIDManager::getConnectedDevices() const {
  std::vector<std::string> addresses;
  if (lockState(pdMS_TO_TICKS(200))) {
    for (const auto& dev : _connectedDevices) {
      if (dev.client != nullptr) {
        addresses.push_back(dev.address);
      }
    }
    unlockState();
  }
  return addresses;
}

std::vector<BluetoothDevice> BluetoothHIDManager::getDiscoveredDevicesSnapshot() const {
  std::vector<BluetoothDevice> devices;
  if (lockState(pdMS_TO_TICKS(200))) {
    devices = _discoveredDevices;
    unlockState();
  }
  return devices;
}

ConnectedDevice* BluetoothHIDManager::findConnectedDevice(const std::string& address) {
  auto it = std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
    [&address](const ConnectedDevice& dev) { return dev.address == address; });
  
  if (it != _connectedDevices.end()) {
    return &(*it);
  }
  return nullptr;
}

void BluetoothHIDManager::handleClientDisconnect(NimBLEClient* pClient, int reason) {
  if (!_enabled || !pClient) {
    return;
  }

  const std::string address = pClient->getPeerAddress().toString();
  Serial.printf("BT Client disconnected: %s (reason: %d)", address.c_str(), reason);

  if (lockState(pdMS_TO_TICKS(100))) {
    for (auto& dev : _connectedDevices) {
      if (dev.client == pClient || dev.address == address) {
        dev.client = nullptr;
        dev.subscribed = false;
        dev.reportChars.clear();
        dev.lastButtonState = false;
        dev.lastHIDKeycode = 0x00;
        dev.lastActivityTime = 0;
        dev.wasConnected = true;
        unlockState();
        return;
      }
    }
    unlockState();
  }
}

void BluetoothHIDManager::processInputEvents() {
  // Input events are processed via notifications callback
  // This method is kept for potential polling-based implementations
}

void BluetoothHIDManager::setInputCallback(std::function<void(uint16_t)> callback) {
  if (lockState(pdMS_TO_TICKS(100))) {
    _inputCallback = callback;
    unlockState();
  } else {
    _inputCallback = callback;
  }
  Serial.printf("BT Input callback registered");
}

void BluetoothHIDManager::setButtonInjector(std::function<void(uint8_t)> injector) {
  if (lockState(pdMS_TO_TICKS(100))) {
    _buttonInjector = injector;
    unlockState();
  } else {
    _buttonInjector = injector;
  }
  Serial.printf("BT Button injector registered");
}

void BluetoothHIDManager::beginKeymapLearning(uint8_t targetButton) {
  if (lockState(pdMS_TO_TICKS(100))) {
    _learningTargetButton = targetButton;
    _hasLastLearnedMapping = false;
    unlockState();
  }
  Serial.printf("BT Keymap learning started for target button %u", targetButton);
}

void BluetoothHIDManager::cancelKeymapLearning() {
  if (lockState(pdMS_TO_TICKS(100))) {
    _learningTargetButton = 0xFF;
    unlockState();
  }
  Serial.printf("BT Keymap learning cancelled");
}

bool BluetoothHIDManager::isKeymapLearning() const {
  bool learning = false;
  if (lockState(pdMS_TO_TICKS(100))) {
    learning = _learningTargetButton != 0xFF;
    unlockState();
  }
  return learning;
}

bool BluetoothHIDManager::consumeLastLearnedMapping(BluetoothKeyMapping& mapping) {
  bool hasMapping = false;
  if (lockState(pdMS_TO_TICKS(100))) {
    if (_hasLastLearnedMapping) {
      mapping = _lastLearnedMapping;
      _hasLastLearnedMapping = false;
      hasMapping = true;
    }
    unlockState();
  }
  return hasMapping;
}

std::vector<BluetoothKeyMapping> BluetoothHIDManager::getKeyMappings() const {
  std::vector<BluetoothKeyMapping> mappings;
  if (lockState(pdMS_TO_TICKS(100))) {
    mappings = _keyMappings;
    unlockState();
  }
  return mappings;
}

void BluetoothHIDManager::clearKeyMappings() {
  if (lockState(pdMS_TO_TICKS(100))) {
    _keyMappings.clear();
    _hasLastLearnedMapping = false;
    _learningTargetButton = 0xFF;
    unlockState();
  }
  saveKeyMappings();
  Serial.printf("BT Key mappings cleared");
}

bool BluetoothHIDManager::hasRecentActivity() const {
  // Check if any connected device has had activity in the last 4 minutes
  // This prevents power sleep while using BLE controller
  unsigned long now = millis();
  bool recent = false;
  if (!lockState(pdMS_TO_TICKS(100))) {
    return false;
  }
  for (const auto& device : _connectedDevices) {
    if (device.lastActivityTime > 0) {
      unsigned long timeSinceActivity = now - device.lastActivityTime;
      if (timeSinceActivity < 240000) {  // 4 minute (240 second) threshold to keep BLE alive
        recent = true;
        break;
      }
    }
  }
  unlockState();
  return recent;
}

// Static callback for HID notifications
void BluetoothHIDManager::onHIDNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (!g_instance || !pData || length == 0) return;
  
  // Log raw data
  char hexStr[128] = {0};
  int offset = 0;
  for (size_t i = 0; i < length && i < 16; i++) {
    offset += snprintf(hexStr + offset, sizeof(hexStr) - offset, "%02X ", pData[i]);
  }
  Serial.printf("BT HID Report (%d bytes): %s", length, hexStr);

  // Get device
  ConnectedDevice* device = nullptr;
  bool stateLocked = false;
  if (pChar && pChar->getRemoteService()) {
    auto client = pChar->getRemoteService()->getClient();
    if (client) {
      std::string deviceAddr = client->getPeerAddress().toString();
      stateLocked = g_instance->lockState(pdMS_TO_TICKS(20));
      if (!stateLocked) {
        return;
      }
      device = g_instance->findConnectedDevice(deviceAddr);
    }
  }
  if (!device) {
    if (stateLocked) {
      g_instance->unlockState();
    }
    return;
  }

  device->lastActivityTime = millis();

  uint8_t  keycode = 0x00;
  uint8_t  keyByteIndex = 0;
  bool    isPressed = false;

  if (device->profile) {
    // 原有裝置邏輯不變
    if (length >= device->profile->reportByteIndex + 1) {
      keycode = pData[device->profile->reportByteIndex];
      keyByteIndex = device->profile->reportByteIndex;
    }
    if (strcmp(device->profile->name, "IINE Game Brick") == 0) {
      isPressed = (pData[0] & 0x01) != 0;
    } else {
      isPressed = (keycode != 0x00);
    }
  } else {
    if (length == 1) {
      keycode = pData[0];
      keyByteIndex = 0;
      isPressed = (keycode != 0x00);
    } else if (length == 2) {
      // Common consumer-control report: E9 00 = PageUp, EA 00 = PageDown.
      keycode = pData[0] != 0x00 ? pData[0] : pData[1];
      keyByteIndex = pData[0] != 0x00 ? 0 : 1;
      isPressed = (keycode != 0x00);
    } else if (length == 3) {
      uint8_t modifier = pData[0];
      keycode = pData[2];
      if (modifier == 0x01 || keycode == 0x4B) {
        keycode = 0x4B; keyByteIndex = (modifier == 0x01) ? 0 : 2; isPressed = true;
      } else if (modifier == 0x02 || keycode == 0x4E) {
        keycode = 0x4E; keyByteIndex = (modifier == 0x02) ? 0 : 2; isPressed = true;
      } else {
        keyByteIndex = 2;
        isPressed = (keycode != 0x00);
      }
    }
    // ====================== 6位元組翻頁器（正確邊沿觸發版）======================
    else if (length == 6) {
      uint8_t key = pData[0];
      
      if (key == 0x01) {
        keycode = 0x4B;
        keyByteIndex = 0;
        isPressed = true;
        Serial.printf("BT 6鍵翻頁器 [上一頁] 按下\n");
      }
      else if (key == 0x02) {
        keycode = 0x4E;
        keyByteIndex = 0;
        isPressed = true;
        Serial.printf("BT 6鍵翻頁器 [下一頁] 按下\n");
      }
      else if (pData[2] != 0x00) {
        keycode = pData[2];
        keyByteIndex = 2;
        isPressed = true;
        Serial.printf("BT 6-byte keyboard key: 0x%02X\n", keycode);
      }
      else {
        keycode = 0x00;
        isPressed = false;
      }
    }
    // ======================================================================
    else if (length >= 5) {
      uint8_t byte4 = pData[4];
      if (byte4 == 0x07 || byte4 == 0x09) {
        keycode = byte4;
        keyByteIndex = 4;
        isPressed = (pData[0] & 0x01) != 0;
      } else {
        keycode = 0x00;
        keyByteIndex = 0;
        for (size_t i = 0; i < length; ++i) {
          const uint8_t candidate = pData[i];
          if (candidate == HID_KEY_ARROW_UP || candidate == HID_KEY_ARROW_LEFT ||
              candidate == HID_KEY_ARROW_DOWN || candidate == HID_KEY_ARROW_RIGHT ||
              candidate == HID_KEY_ENTER || candidate == HID_KEY_ESCAPE ||
              candidate == HID_KEY_BACKSPACE || candidate == HID_KEY_SPACE ||
              candidate == 0x4B || candidate == 0x4E || candidate == 0xE9 || candidate == 0xEA) {
            keycode = candidate;
            keyByteIndex = static_cast<uint8_t>(i);
            break;
          }
        }
        if (keycode == 0x00 && length > 2) {
          keycode = pData[2];
          keyByteIndex = 2;
        }
        isPressed = (keycode != 0);
      }
    } else {
      keycode = (length > 2) ? pData[2] : 0;
      keyByteIndex = (length > 2) ? 2 : 0;
      isPressed = (keycode != 0);
    }
  }

  // ====================== 【保留邊沿觸發】======================
  bool wasPressedPrevious = device->lastButtonState;

  // 鬆開 → 更新狀態，不動作
  if (!isPressed) {
    device->lastButtonState = false;
    device->lastHIDKeycode = 0x00;
    if (stateLocked) {
      g_instance->unlockState();
    }
    return;
  }

  // 已經按住 → 不重複觸發
  if (wasPressedPrevious) {
    if (stateLocked) {
      g_instance->unlockState();
    }
    return;
  }

  // ====================== 【唯一一次觸發：按下瞬間】======================
  device->lastButtonState = true;
  device->lastHIDKeycode = keycode;

  Serial.printf("BT >>> BUTTON PRESSED: keycode=0x%02X <<<\n", keycode);

  const bool shouldLearn = g_instance->_learningTargetButton != 0xFF;
  const std::string learnAddress = device->address;
  const uint8_t reportLength = static_cast<uint8_t>(std::min<size_t>(length, 255));

  bool shouldInject = false;
  uint8_t injectedButton = 0xFF;
  std::function<void(uint8_t)> buttonInjector;
  bool shouldSendInput = false;
  std::function<void(uint16_t)> inputCallback;

  if (!shouldLearn && g_instance->_buttonInjector) {
    uint8_t btn = g_instance->mapKeycodeToButton(*device, reportLength, keyByteIndex, keycode);
    if (btn != 0xFF) {
      unsigned long now = millis();
      if (now - device->lastInjectionTime >= 150) {
        String buttonName = "Unknown";
        if (btn == HalGPIO::BTN_UP) buttonName = "PageBack";
        else if (btn == HalGPIO::BTN_DOWN) buttonName = "PageForward";
        else if (btn == HalGPIO::BTN_BACK) buttonName = "Back";
        else if (btn == HalGPIO::BTN_CONFIRM) buttonName = "Confirm";
        Serial.printf("BT Mapped key -> %s\n", buttonName.c_str());
        Serial.printf("BT Injecting button: %d\n", btn);
        device->lastInjectionTime = now;
        buttonInjector = g_instance->_buttonInjector;
        injectedButton = btn;
        shouldInject = true;
      }
    }
  }

  if (!shouldLearn && g_instance->_inputCallback) {
    inputCallback = g_instance->_inputCallback;
    shouldSendInput = true;
  }

  if (stateLocked) {
    g_instance->unlockState();
  }

  if (shouldLearn) {
    g_instance->learnKeyMapping(learnAddress, reportLength, keyByteIndex, keycode);
    return;
  }

  if (shouldInject && buttonInjector) {
    buttonInjector(injectedButton);
  }

  if (shouldSendInput && inputCallback) {
    inputCallback(keycode);
  }
}


uint16_t BluetoothHIDManager::parseHIDReport(uint8_t* data, size_t length) {
  if (length < 3) {
    Serial.printf("BT Invalid HID report length: %d", length);
    return 0;
  }
  
  uint8_t modifier = data[0];
  uint8_t keycode = data[2]; // First key in the report
  
  // If no key pressed (all zeros), return 0
  if (keycode == 0 && modifier == 0) {
    return 0;
  }
  
  // Log non-empty reports
  Serial.printf("BT HID Report: mod=0x%02X key=0x%02X", modifier, keycode);
  
  // Combine modifier and keycode (modifier in upper byte, keycode in lower)
  uint16_t combined = (static_cast<uint16_t>(modifier) << 8) | keycode;
  
  return combined;
}

// Map HID keycodes to navigator buttons based on device profile
// Only maps keycodes that match the current device's profile to prevent
// unwanted D-pad or other button inputs from triggering page turns
uint8_t BluetoothHIDManager::mapKeycodeToButton(const ConnectedDevice& device, uint8_t reportLength, uint8_t byteIndex,
                                                uint8_t keycode) {
  // Log keycode for debugging
  if (keycode != 0x00) {
    Serial.printf("BT mapKeycodeToButton() called with keycode: 0x%02X", keycode);
  }

  if (const auto* custom = findKeyMapping(device.address, reportLength, byteIndex, keycode)) {
    Serial.printf("BT Matched custom keymap len=%u byte[%u]=0x%02X -> button %u", reportLength, byteIndex, keycode,
                  custom->targetButton);
    return custom->targetButton;
  }

  const auto* profile = device.profile;
  
  // Try the matched device profile first, then fall back to common HID codes.
  if (profile) {
    if (keycode == HID_KEY_ARROW_UP || keycode == HID_KEY_ARROW_LEFT) {
      Serial.printf("BT Matched keyboard arrow 0x%02X -> PageBack", keycode);
      return HalGPIO::BTN_UP;
    } else if (keycode == HID_KEY_ARROW_DOWN || keycode == HID_KEY_ARROW_RIGHT) {
      Serial.printf("BT Matched keyboard arrow 0x%02X -> PageForward", keycode);
      return HalGPIO::BTN_DOWN;
    } else if (keycode == HID_KEY_ENTER || keycode == HID_KEY_SPACE) {
      Serial.printf("BT Matched keyboard confirm key 0x%02X -> Confirm", keycode);
      return HalGPIO::BTN_CONFIRM;
    } else if (keycode == HID_KEY_ESCAPE || keycode == HID_KEY_BACKSPACE) {
      Serial.printf("BT Matched keyboard back key 0x%02X -> Back", keycode);
      return HalGPIO::BTN_BACK;
    } else if (keycode == profile->pageUpCode) {
      Serial.printf("BT Matched profile pageUpCode 0x%02X (%s) -> PageBack", keycode, profile->name);
      return HalGPIO::BTN_UP;
    } else if (keycode == profile->pageDownCode) {
      Serial.printf("BT Matched profile pageDownCode 0x%02X (%s) -> PageForward", keycode, profile->name);
      return HalGPIO::BTN_DOWN;
    } else {
      Serial.printf("BT Keycode 0x%02X not in profile %s (expecting 0x%02X/0x%02X), trying generic map",
              keycode, profile->name, profile->pageUpCode, profile->pageDownCode);
    }
  }
  
  // No profile - fall back to generic HID consumer codes only
  switch (keycode) {
    // 翻頁器核心對映（按需修改）
    case 0x01:   // 左Ctrl → 上一頁
    case 0x4B:   // PageUp → 上一頁
    case HID_KEY_ARROW_UP:    // ArrowUp → 上一頁
    case HID_KEY_ARROW_LEFT:  // ArrowLeft → 上一頁
    case 0xE9:   // Consumer PageUp → 上一頁
      Serial.printf("BT Mapped key 0x%02X -> PageBack", keycode);
      return HalGPIO::BTN_UP;
    
    case 0x02:   // 左Shift → 下一頁
    case 0x4E:   // PageDown → 下一頁
    case HID_KEY_ARROW_DOWN:   // ArrowDown → 下一頁
    case HID_KEY_ARROW_RIGHT:  // ArrowRight → 下一頁
    case 0xEA:   // Consumer PageDown → 下一頁
      Serial.printf("BT Mapped key 0x%02X -> PageForward", keycode);
      return HalGPIO::BTN_DOWN;

    case HID_KEY_ENTER:  // Enter → 確認
    case HID_KEY_SPACE:  // Space → 確認
      Serial.printf("BT Mapped key 0x%02X -> Confirm", keycode);
      return HalGPIO::BTN_CONFIRM;

    case HID_KEY_ESCAPE:     // Esc → 返回
    case HID_KEY_BACKSPACE:  // Backspace → 返回
      Serial.printf("BT Mapped key 0x%02X -> Back", keycode);
      return HalGPIO::BTN_BACK;

    case 0x00:   // 忽略釋放事件
      return 0xFF;

    default:
      Serial.printf("BT Unmapped keycode: 0x%02X (翻頁器未匹配)", keycode);
      return 0xFF;
  }
}

const BluetoothKeyMapping* BluetoothHIDManager::findKeyMapping(const std::string& address, uint8_t reportLength,
                                                               uint8_t byteIndex, uint8_t keycode) const {
  for (const auto& mapping : _keyMappings) {
    if (mapping.address == address && mapping.reportLength == reportLength && mapping.byteIndex == byteIndex &&
        mapping.keycode == keycode) {
      return &mapping;
    }
  }
  return nullptr;
}

void BluetoothHIDManager::learnKeyMapping(const std::string& address, uint8_t reportLength, uint8_t byteIndex,
                                          uint8_t keycode) {
  if (!lockState(pdMS_TO_TICKS(100))) {
    return;
  }

  bool learned = false;
  uint8_t targetButton = 0xFF;
  if (_learningTargetButton != 0xFF && keycode != 0x00) {
    targetButton = _learningTargetButton;

    BluetoothKeyMapping mapping;
    mapping.address = address;
    mapping.reportLength = reportLength;
    mapping.byteIndex = byteIndex;
    mapping.keycode = keycode;
    mapping.targetButton = targetButton;

    auto it = std::find_if(_keyMappings.begin(), _keyMappings.end(), [&](const BluetoothKeyMapping& existing) {
      return existing.address == mapping.address && existing.reportLength == mapping.reportLength &&
             existing.byteIndex == mapping.byteIndex && existing.keycode == mapping.keycode;
    });
    if (it != _keyMappings.end()) {
      *it = mapping;
    } else {
      _keyMappings.push_back(mapping);
    }

    _lastLearnedMapping = mapping;
    _hasLastLearnedMapping = true;
    _learningTargetButton = 0xFF;
    learned = true;
  }
  unlockState();

  if (learned) {
    saveKeyMappings();
    Serial.printf("BT Learned keymap %s len=%u byte[%u]=0x%02X -> button %u", address.c_str(), reportLength, byteIndex,
                  keycode, targetButton);
  }
}

void BluetoothHIDManager::updateActivity() {
  if (_scanning && _scanEndTime > 0 && static_cast<long>(millis() - _scanEndTime) >= 0) {
    stopScan();
  }

  // Check inactivity timeouts every 10 seconds
  unsigned long now = millis();
  if (now - lastMaintenanceCheck < 10000) {
    return;
  }
  lastMaintenanceCheck = now;

  // Check for inactive connections
  std::string inactiveAddress;
  if (lockState(pdMS_TO_TICKS(100))) {
    for (const auto& device : _connectedDevices) {
      if (device.lastActivityTime > 0) {
      unsigned long inactiveTime = now - device.lastActivityTime;
      if (inactiveTime > INACTIVITY_TIMEOUT_MS) {
        Serial.printf("BT Device %s inactive for %lu ms, disconnecting", device.address.c_str(), inactiveTime);
          inactiveAddress = device.address;
          break;
        }
      }
    }
    unlockState();
  }
  if (!inactiveAddress.empty()) {
    disconnectFromDevice(inactiveAddress);
  }
}

void BluetoothHIDManager::checkAutoReconnect() {
  if (!_enabled) return;

  static unsigned long lastReconnectCheck = 0;
  const unsigned long now = millis();
  if (now - lastReconnectCheck < 5000) return;
  lastReconnectCheck = now;

  // 優先處理已連過但斷線的裝置
  std::string reconnectAddress;
  if (lockState(pdMS_TO_TICKS(100))) {
    for (const auto& device : _connectedDevices) {
      if (device.wasConnected && device.client && !device.client->isConnected()) {
        reconnectAddress = device.address;
        break;
      }
    }
    unlockState();
  }

  if (!reconnectAddress.empty()) {
    Serial.printf("BT Device %s was disconnected, attempting auto-reconnect...", reconnectAddress.c_str());
    disconnectFromDevice(reconnectAddress);
    if (connectToDevice(reconnectAddress)) {
      Serial.printf("BT Auto-reconnected to %s", reconnectAddress.c_str());
    } else {
      Serial.printf("BT Auto-reconnect to %s failed: %s", reconnectAddress.c_str(), lastError.c_str());
    }
    return;  // 每次只處理一台，避免堆疊太深
  }

  // _restoredDevices（bt_state.bin 歷史裝置）的重連由 BluetoothSettingsActivity
  // 在使用者開啟藍芽設定頁時主動觸發，不在 main loop 做阻塞連線避免 watchdog 超時
}

void BluetoothHIDManager::saveState() {
  // 把目前已連線（或曾連線）的裝置清單寫進 /.crosspoint/bt_state.bin
  // 格式：[version:u8][count:u8]([address:string][name:string][addrType:u8] × count)
  std::vector<std::pair<std::string, std::string>> snapshot;
  uint8_t addrTypes[32] = {};
  uint8_t snapCount = 0;
  if (lockState(pdMS_TO_TICKS(100))) {
    for (const auto& dev : _connectedDevices) {
      if (!dev.address.empty() && snapCount < 8) {
        snapshot.push_back({dev.address, dev.name});
        addrTypes[snapCount] = 0;
        snapCount++;
      }
    }
    unlockState();
  }
  SdMan.mkdir("/.crosspoint");
  FsFile f;
  if (!SdMan.openFileForWrite("BT", "/.crosspoint/bt_state.bin", f)) {
    Serial.printf("BT saveState: failed to open bt_state.bin");
    return;
  }
  const uint8_t version = 2;
  serialization::writePod(f, version);
  const uint8_t count = static_cast<uint8_t>(snapshot.size());
  serialization::writePod(f, count);
  for (uint8_t i = 0; i < count; i++) {
    serialization::writeString(f, snapshot[i].first);   // address
    serialization::writeString(f, snapshot[i].second);  // name
    serialization::writePod(f, addrTypes[i]);
  }
  f.close();
  Serial.printf("BT saveState: saved %u devices", count);
}

void BluetoothHIDManager::loadState() {
  // 從 /.crosspoint/bt_state.bin 還原上次的已配對裝置清單
  // 只恢復清單讓 checkAutoReconnect 能識別，不在這裡主動連線
  FsFile f;
  if (!SdMan.openFileForRead("BT", "/.crosspoint/bt_state.bin", f)) {
    Serial.printf("BT loadState: no bt_state.bin found");
    return;
  }
  uint8_t version = 0;
  serialization::readPod(f, version);
  if (version != 2) {
    Serial.printf("BT loadState: unknown version %u, skipping", version);
    f.close();
    return;
  }
  uint8_t count = 0;
  serialization::readPod(f, count);
  if (count > 8) count = 8;

  std::vector<std::pair<std::string, std::string>> loaded;
  for (uint8_t i = 0; i < count; i++) {
    std::string addr, name;
    uint8_t addrType = 0;
    serialization::readString(f, addr);
    serialization::readString(f, name);
    serialization::readPod(f, addrType);
    if (!addr.empty()) loaded.push_back({addr, name});
  }
  f.close();

  if (!lockState(pdMS_TO_TICKS(200))) {
    Serial.printf("BT loadState: state lock timeout");
    return;
  }
  // 把還沒在 _connectedDevices 裡的裝置放進 _restoredDevices
  // checkAutoReconnect 會從這裡取出並主動連線
  for (const auto& entry : loaded) {
    bool already = false;
    for (const auto& d : _connectedDevices) {
      if (d.address == entry.first) { already = true; break; }
    }
    if (!already) {
      _restoredDevices.push_back(entry);
    }
  }
  unlockState();
  Serial.printf("BT loadState: %u devices queued for reconnect", static_cast<unsigned>(loaded.size()));
}

void BluetoothHIDManager::saveLastConnectedDevice(const std::string& address, const std::string& name) {
  Serial.printf("BT Saving last connected device: %s (%s)", name.c_str(), address.c_str());
  
  // Make sure the directory exists
  SdMan.mkdir("/.crosspoint");
  
  FsFile outputFile;
  if (!SdMan.openFileForWrite("BT", "/.crosspoint/bluetooth.bin", outputFile)) {
    Serial.printf("BT Failed to open bluetooth.bin for writing");
    return;
  }
  
  // Write version
  uint8_t version = 1;
  serialization::writePod(outputFile, version);
  
  // Write device info
  serialization::writeString(outputFile, address);
  serialization::writeString(outputFile, name);
  
  outputFile.close();
  Serial.printf("BT Last connected device saved successfully");
}

bool BluetoothHIDManager::loadLastConnectedDevice(std::string& address, std::string& name) {
  FsFile inputFile;
  if (!SdMan.openFileForRead("BT", "/.crosspoint/bluetooth.bin", inputFile)) {
    Serial.printf("BT No saved bluetooth device found");
    return false;
  }
  
  // Read version
  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != 1) {
    Serial.printf("BT Unknown bluetooth.bin version: %d", version);
    inputFile.close();
    return false;
  }
  
  // Read device info
  serialization::readString(inputFile, address);
  serialization::readString(inputFile, name);
  
  inputFile.close();
  if (address.empty()) {
    Serial.printf("BT Loaded bluetooth.bin but address field is empty");
    return false;
  }
  Serial.printf("BT Loaded last connected device: %s (%s)", name.c_str(), address.c_str());
  return true;
}

void BluetoothHIDManager::saveKeyMappings() {
  std::vector<BluetoothKeyMapping> mappings;
  if (lockState(pdMS_TO_TICKS(100))) {
    mappings = _keyMappings;
    unlockState();
  } else {
    Serial.printf("BT Keymap save skipped: state lock timeout");
    return;
  }

  Serial.printf("BT Saving %u key mappings", static_cast<unsigned>(mappings.size()));

  SdMan.mkdir("/.crosspoint");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("BT", "/.crosspoint/bluetooth_keymap.bin", outputFile)) {
    Serial.printf("BT Failed to open bluetooth_keymap.bin for writing");
    return;
  }

  uint8_t version = 1;
  serialization::writePod(outputFile, version);
  uint8_t count = static_cast<uint8_t>(std::min<size_t>(mappings.size(), 32));
  serialization::writePod(outputFile, count);

  for (uint8_t i = 0; i < count; i++) {
    const auto& mapping = mappings[i];
    serialization::writeString(outputFile, mapping.address);
    serialization::writePod(outputFile, mapping.reportLength);
    serialization::writePod(outputFile, mapping.byteIndex);
    serialization::writePod(outputFile, mapping.keycode);
    serialization::writePod(outputFile, mapping.targetButton);
  }

  outputFile.close();
}

void BluetoothHIDManager::loadKeyMappings() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("BT", "/.crosspoint/bluetooth_keymap.bin", inputFile)) {
    Serial.printf("BT No saved bluetooth keymap found");
    return;
  }

  uint8_t version = 0;
  serialization::readPod(inputFile, version);
  if (version != 1) {
    Serial.printf("BT Unknown bluetooth_keymap.bin version: %d", version);
    inputFile.close();
    return;
  }

  uint8_t count = 0;
  serialization::readPod(inputFile, count);
  _keyMappings.clear();
  for (uint8_t i = 0; i < count && i < 32; i++) {
    BluetoothKeyMapping mapping;
    serialization::readString(inputFile, mapping.address);
    serialization::readPod(inputFile, mapping.reportLength);
    serialization::readPod(inputFile, mapping.byteIndex);
    serialization::readPod(inputFile, mapping.keycode);
    serialization::readPod(inputFile, mapping.targetButton);
    if (!mapping.address.empty() && mapping.keycode != 0x00 && mapping.targetButton != 0xFF) {
      _keyMappings.push_back(mapping);
    }
  }

  inputFile.close();
  Serial.printf("BT Loaded %u key mappings", static_cast<unsigned>(_keyMappings.size()));
}
