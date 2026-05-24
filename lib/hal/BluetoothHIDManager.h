#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include "DeviceProfiles.h"

// Forward declarations
class NimBLEClient;
class NimBLERemoteCharacteristic;
class NimBLEAdvertisedDevice;

struct BluetoothDevice {
  std::string address;
  std::string name;
  int rssi;
  uint8_t addressType = 0;
  bool isHID = false;
};

struct ConnectedDevice {
  std::string address;
  std::string name;
  NimBLEClient* client = nullptr;
  std::vector<NimBLERemoteCharacteristic*> reportChars;
  bool subscribed = false;
  unsigned long lastActivityTime = 0;  // Timestamp of last HID report received
  uint8_t lastHIDKeycode = 0x00;       // Track last keycode to detect press/release transitions
  unsigned long lastInjectionTime = 0; // Cooldown for button injection to prevent flooding
  bool wasConnected = false;           // Track if this device was previously connected for auto-reconnect
  bool lastButtonState = false;        // Track button pressed state (from byte[0])
  const DeviceProfiles::DeviceProfile* profile = nullptr;  // Device-specific HID profile
};

struct BluetoothKeyMapping {
  std::string address;
  uint8_t reportLength = 0;
  uint8_t byteIndex = 0;
  uint8_t keycode = 0;
  uint8_t targetButton = 0xFF;
};

class BluetoothHIDManager {
public:
  // Singleton access
  static BluetoothHIDManager& getInstance();

  // Lifecycle
  bool enable();
  bool disable();
  bool isEnabled() const { return _enabled; }

  // Scanning
  void startScan(uint32_t durationMs = 10000);
  void stopScan();
  bool isScanning() const { return _scanning; }
  std::vector<BluetoothDevice> getDiscoveredDevicesSnapshot() const;
  void releaseTransientCaches();

  // Connection
  bool connectToDevice(const std::string& address);
  /**
   * Attempt to connect up to `maxAttempts` times before giving up.
   * This is useful for UI code that may try to connect to non‑responsive
   * devices and must not crash the system.
   */
  bool connectToDeviceWithRetries(const std::string& address, int maxAttempts = 3);
  bool disconnectFromDevice(const std::string& address);
  bool isConnected(const std::string& address) const;
  std::vector<std::string> getConnectedDevices() const;

  // Input handling
  void processInputEvents();
  void setInputCallback(std::function<void(uint16_t keycode)> callback);
  void setButtonInjector(std::function<void(uint8_t buttonIndex)> injector);
  void beginKeymapLearning(uint8_t targetButton);
  void cancelKeymapLearning();
  bool isKeymapLearning() const;
  bool consumeLastLearnedMapping(BluetoothKeyMapping& mapping);
  std::vector<BluetoothKeyMapping> getKeyMappings() const;
  void clearKeyMappings();
  void updateActivity();  // Call periodically to check inactivity timeout
  void checkAutoReconnect();  // Auto-reconnect to previously connected devices if disconnected
  
  // Check if BLE has had activity recently (within last 4 minutes)
  // Used by power manager to prevent sleep during BLE use
  bool hasRecentActivity() const;

  // State persistence
  void saveState();
  void loadState();
  void saveLastConnectedDevice(const std::string& address, const std::string& name);
  bool loadLastConnectedDevice(std::string& address, std::string& name);
  void saveKeyMappings();
  void loadKeyMappings();

  std::string lastError;
  //防止崩潰，讓外部可讀取資訊
  void onScanResult(NimBLEAdvertisedDevice* advertisedDevice);
  void handleClientDisconnect(NimBLEClient* pClient, int reason);

private:

  // BLE callbacks (public for NimBLE callbacks)
  
  static void onHIDNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);

private:
  BluetoothHIDManager();
  ~BluetoothHIDManager();
  BluetoothHIDManager(const BluetoothHIDManager&) = delete;
  BluetoothHIDManager& operator=(const BluetoothHIDManager&) = delete;

  void cleanup();
  bool lockState(TickType_t ticks = portMAX_DELAY) const;
  void unlockState() const;
  uint16_t parseHIDReport(uint8_t* data, size_t length);
  ConnectedDevice* findConnectedDevice(const std::string& address);
  uint8_t mapKeycodeToButton(const ConnectedDevice& device, uint8_t reportLength, uint8_t byteIndex, uint8_t keycode);
  const BluetoothKeyMapping* findKeyMapping(const std::string& address, uint8_t reportLength, uint8_t byteIndex,
                                            uint8_t keycode) const;
  void learnKeyMapping(const std::string& address, uint8_t reportLength, uint8_t byteIndex, uint8_t keycode);

  bool _enabled = false;
  bool _scanning = false;
  unsigned long _scanEndTime = 0;
  std::vector<BluetoothDevice> _discoveredDevices;
  std::vector<ConnectedDevice> _connectedDevices;
  std::vector<BluetoothKeyMapping> _keyMappings;
  // 從 bt_state.bin 還原的待重連清單（address, name）
  // checkAutoReconnect 會從這裡取出並嘗試連線
  std::vector<std::pair<std::string, std::string>> _restoredDevices;
  mutable SemaphoreHandle_t _stateMutex = nullptr;
  std::function<void(uint16_t)> _inputCallback;
  std::function<void(uint8_t)> _buttonInjector;
  uint8_t _learningTargetButton = 0xFF;
  bool _hasLastLearnedMapping = false;
  BluetoothKeyMapping _lastLearnedMapping;

  // Inactivity timeout (milliseconds)
  static constexpr unsigned long INACTIVITY_TIMEOUT_MS = 120000;  // 2 minutes
  unsigned long lastMaintenanceCheck = 0;
};
