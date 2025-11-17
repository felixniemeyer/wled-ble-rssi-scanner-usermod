#include "usermod_ble_rssi_scanner.h"

#ifdef ESP32

// Callback for BLE scan results
class BLERSSIScannerUsermod::ScanCallback : public BLEAdvertisedDeviceCallbacks {
private:
  BLERSSIScannerUsermod* parent;

public:
  ScanCallback(BLERSSIScannerUsermod* p) : parent(p) {}

  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Only collect data if scan is in progress
    if (!parent->scanInProgress) return;

    // Check if device has a name
    if (!advertisedDevice.haveName()) return;

    std::string deviceName = advertisedDevice.getName();
    int rssi = advertisedDevice.getRSSI();

    // Store RSSI sample
    parent->rssiSamples[deviceName].push_back(rssi);

    DEBUG_PRINTF_P(PSTR("BLE RSSI: Found device '%s' with RSSI: %d\n"),
                   deviceName.c_str(), rssi);
  }
};

// Constructor
BLERSSIScannerUsermod::BLERSSIScannerUsermod()
  : enabled(true),
    bleInitialized(false),
    scanInProgress(false),
    scanStartTime(0),
    scanDurationSec(BLE_RSSI_SCAN_DURATION_SEC),
    pBLEScan(nullptr),
    pAdvertising(nullptr),
    scanCallback(nullptr) {
}

// Destructor
BLERSSIScannerUsermod::~BLERSSIScannerUsermod() {
  if (scanCallback) {
    delete scanCallback;
  }
}

void BLERSSIScannerUsermod::initBLE() {
  if (bleInitialized) return;

  DEBUG_PRINTLN(F("BLE RSSI Scanner: Initializing BLE"));

  // Need to enable WiFi sleep for BLE+WiFi coexistence
  noWifiSleep = false;

  // Initialize BLE with device name
  BLEDevice::init(serverDescription);

  // Setup advertising
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x180A)); // Device Information Service
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  // Start advertising so other devices can find us
  BLEDevice::startAdvertising();
  DEBUG_PRINTLN(F("BLE RSSI Scanner: Started advertising"));

  // Setup scanning
  pBLEScan = BLEDevice::getScan();
  scanCallback = new ScanCallback(this);
  pBLEScan->setAdvertisedDeviceCallbacks(scanCallback);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  bleInitialized = true;
  DEBUG_PRINTLN(F("BLE RSSI Scanner: BLE initialized"));
}

void BLERSSIScannerUsermod::startScan() {
  if (!bleInitialized) {
    initBLE();
  }

  if (scanInProgress) {
    DEBUG_PRINTLN(F("BLE RSSI Scanner: Scan already in progress"));
    return;
  }

  // Clear previous samples
  rssiSamples.clear();

  scanInProgress = true;
  scanStartTime = millis();

  // Start continuous scan
  pBLEScan->start(0, nullptr, false);

  DEBUG_PRINTF_P(PSTR("BLE RSSI Scanner: Started %d second scan\n"), scanDurationSec);
}

void BLERSSIScannerUsermod::stopScan() {
  if (!scanInProgress) return;

  pBLEScan->stop();
  scanInProgress = false;

  DEBUG_PRINTLN(F("BLE RSSI Scanner: Stopped scan"));
}

String BLERSSIScannerUsermod::getResultsJSON() {
  DynamicJsonDocument doc(4096);
  JsonArray devices = doc.createNestedArray("devices");

  // Calculate average RSSI for each device
  for (const auto& entry : rssiSamples) {
    const std::string& deviceName = entry.first;
    const std::vector<int>& samples = entry.second;

    if (samples.empty()) continue;

    // Calculate average
    int sum = 0;
    for (int rssi : samples) {
      sum += rssi;
    }
    float avg = (float)sum / samples.size();

    JsonObject device = devices.createNestedObject();
    device["name"] = deviceName;
    device["rssi_avg"] = avg;
    device["sample_count"] = samples.size();
  }

  // Add metadata
  doc["scan_duration_sec"] = scanDurationSec;
  doc["device_count"] = devices.size();

  String output;
  serializeJson(doc, output);
  return output;
}

void BLERSSIScannerUsermod::registerHTTPHandler() {
  DEBUG_PRINTLN(F("BLE RSSI Scanner: Registering HTTP handler"));

  server.on("/api/ble-rssi-scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
    DEBUG_PRINTLN(F("BLE RSSI Scanner: HTTP request received"));

    // Check for optional duration parameter
    if (request->hasParam("duration")) {
      AsyncWebParameter* p = request->getParam("duration");
      uint32_t duration = p->value().toInt();
      if (duration > 0 && duration <= 300) {
        scanDurationSec = duration;
      }
    }

    // If scan is already complete, return results
    if (!scanInProgress && !rssiSamples.empty()) {
      String response = getResultsJSON();
      request->send(200, "application/json", response);
      return;
    }

    // Start new scan
    startScan();

    // Return immediate response indicating scan started
    DynamicJsonDocument doc(256);
    doc["status"] = "scanning";
    doc["duration_sec"] = scanDurationSec;
    doc["message"] = "BLE scan started. Call this endpoint again after " +
                     String(scanDurationSec) + " seconds to get results.";

    String output;
    serializeJson(doc, output);
    request->send(202, "application/json", output);
  });

  server.on("/api/ble-rssi-results", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (rssiSamples.empty()) {
      request->send(404, "application/json", "{\"error\":\"No scan data available\"}");
      return;
    }

    String response = getResultsJSON();
    request->send(200, "application/json", response);
  });
}

void BLERSSIScannerUsermod::setup() {
  DEBUG_PRINTLN(F("BLE RSSI Scanner: setup()"));

  #if !defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32C3)
    DEBUG_PRINTLN(F("BLE RSSI Scanner: BLE not supported on this hardware"));
    enabled = false;
    return;
  #endif

  #if defined(CONFIG_IDF_TARGET_ESP32S2)
    DEBUG_PRINTLN(F("BLE RSSI Scanner: ESP32-S2 does not support BLE"));
    enabled = false;
    return;
  #endif
}

void BLERSSIScannerUsermod::connected() {
  if (!enabled) return;
  registerHTTPHandler();
}

void BLERSSIScannerUsermod::loop() {
  if (!enabled || !scanInProgress) return;

  // Check if scan duration has elapsed
  if (millis() - scanStartTime >= scanDurationSec * 1000) {
    stopScan();
  }
}

void BLERSSIScannerUsermod::addToJsonInfo(JsonObject& root) {
  JsonObject user = root["u"];
  if (user.isNull()) {
    user = root.createNestedObject("u");
  }

  JsonArray infoArr = user.createNestedArray("BLE RSSI Scanner");
  if (enabled) {
    if (scanInProgress) {
      infoArr.add(F("Scanning..."));
    } else if (!rssiSamples.empty()) {
      infoArr.add(String(rssiSamples.size()) + " devices found");
    } else {
      infoArr.add(F("Ready"));
    }
  } else {
    infoArr.add(F("Disabled"));
  }
}

void BLERSSIScannerUsermod::addToConfig(JsonObject& root) {
  JsonObject top = root.createNestedObject("BLE_RSSI_Scanner");
  top["enabled"] = enabled;
  top["scan_duration_sec"] = scanDurationSec;
}

bool BLERSSIScannerUsermod::readFromConfig(JsonObject& root) {
  JsonObject top = root["BLE_RSSI_Scanner"];
  if (top.isNull()) {
    DEBUG_PRINTLN(F("BLE RSSI Scanner: No config found. Using defaults."));
    return false;
  }

  enabled = top["enabled"] | enabled;
  scanDurationSec = top["scan_duration_sec"] | scanDurationSec;

  // Clamp duration to reasonable values
  if (scanDurationSec < 1) scanDurationSec = 1;
  if (scanDurationSec > 300) scanDurationSec = 300;

  return true;
}

void BLERSSIScannerUsermod::appendConfigData() {
  oappend(SET_F("addInfo('BLE_RSSI_Scanner:enabled',1,'<br>Enable/disable BLE RSSI scanning');"));
  oappend(SET_F("addInfo('BLE_RSSI_Scanner:scan_duration_sec',1,'Duration in seconds (1-300)<br>"));
  oappend(SET_F("API: <code>GET /api/ble-rssi-scan</code>');"));
}

uint16_t BLERSSIScannerUsermod::getId() {
  return USERMOD_ID_BLE_RSSI_SCANNER;
}

// Register the usermod
static BLERSSIScannerUsermod blerssiScanner;
REGISTER_USERMOD(blerssiScanner);

#endif // ESP32
