#pragma once

#include "wled.h"

#ifdef ESP32

#include <NimBLEDevice.h>
#include <NimBLEAdvertising.h>
#include <NimBLEScan.h>
#include <map>
#include <vector>
#include <string>

// Default scan duration in seconds
#ifndef BLE_RSSI_SCAN_DURATION_SEC
  #define BLE_RSSI_SCAN_DURATION_SEC 10
#endif

class BLERSSIScannerUsermod : public Usermod {
private:
  bool enabled;
  bool bleInitialized;
  bool scanInProgress;
  uint32_t scanStartTime;
  uint32_t scanDurationSec;

  NimBLEScan* pBLEScan;
  NimBLEAdvertising* pAdvertising;

  // Store RSSI samples: device name -> vector of RSSI values
  std::map<std::string, std::vector<int>> rssiSamples;

  // Forward declaration
  class ScanCallback;
  ScanCallback* scanCallback;

  // Helper methods
  void initBLE();
  void startScan();
  void stopScan();
  String getResultsJSON();
  void registerHTTPHandler();
  void logToFile(const char* message);
  String getLogContents();

public:
  BLERSSIScannerUsermod();
  ~BLERSSIScannerUsermod();

  // Usermod interface
  void setup() override;
  void connected() override;
  void loop() override;
  void addToJsonInfo(JsonObject& root) override;
  void addToConfig(JsonObject& root) override;
  bool readFromConfig(JsonObject& root) override;
  void appendConfigData() override;
  uint16_t getId() override;
};

#else
// Stub for non-ESP32 platforms
class BLERSSIScannerUsermod : public Usermod {
public:
  void setup() override {}
  void loop() override {}
  uint16_t getId() override { return USERMOD_ID_BLE_RSSI_SCANNER; }
};
#endif
