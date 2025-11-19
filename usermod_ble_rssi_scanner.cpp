#include "usermod_ble_rssi_scanner.h"

#ifdef ESP32

#include <LittleFS.h>

#define LOG_FILE "/ble_rssi.log"
#define MAX_LOG_SIZE 8192  // Keep log under 8KB

// Callback for BLE scan results
class BLERSSIScannerUsermod::ScanCallback : public NimBLEAdvertisedDeviceCallbacks {
private:
  BLERSSIScannerUsermod* parent;

public:
  ScanCallback(BLERSSIScannerUsermod* p) : parent(p) {}

  void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    // Log that callback was triggered
    char msg[128];
    snprintf(msg, sizeof(msg), "Callback triggered! Device detected");
    parent->logToFile(msg);

    // Only collect data if scan is in progress
    if (!parent->scanInProgress) {
      parent->logToFile("  -> Scan not in progress, ignoring");
      return;
    }

    // Get MAC address (always available)
    std::string address = advertisedDevice->getAddress().toString();
    int rssi = advertisedDevice->getRSSI();

    // Get name (optional)
    std::string name = advertisedDevice->haveName() ?
                       advertisedDevice->getName() : "";

    snprintf(msg, sizeof(msg), "  -> MAC: %s, Name: %s, RSSI: %d",
             address.c_str(), name.empty() ? "(none)" : name.c_str(), rssi);
    parent->logToFile(msg);

    // Store or update device info
    auto& device = parent->devices[address];
    device.address = address;
    if (!name.empty()) {
      device.name = name;  // Update name if available
    }
    device.rssiSamples.push_back(rssi);

    DEBUG_PRINTF_P(PSTR("BLE RSSI: Found device '%s' (%s) with RSSI: %d\n"),
                   address.c_str(), name.c_str(), rssi);
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

void BLERSSIScannerUsermod::logToFile(const char* message) {
  File logFile = LittleFS.open(LOG_FILE, "a");
  if (!logFile) return;

  // Check file size and truncate if too large
  if (logFile.size() > MAX_LOG_SIZE) {
    logFile.close();
    LittleFS.remove(LOG_FILE);
    logFile = LittleFS.open(LOG_FILE, "w");
  }

  char timestamp[32];
  snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
  logFile.print(timestamp);
  logFile.println(message);
  logFile.close();
}

String BLERSSIScannerUsermod::getLogContents() {
  File logFile = LittleFS.open(LOG_FILE, "r");
  if (!logFile) return "Log file not found";

  String contents = logFile.readString();
  logFile.close();
  return contents;
}

void BLERSSIScannerUsermod::initBLE() {
  if (bleInitialized) return;

  logToFile("=== BLE Init Started ===");
  DEBUG_PRINTLN(F("BLE RSSI Scanner: Initializing BLE"));

  char heapMsg[64];
  snprintf(heapMsg, sizeof(heapMsg), "Free heap before init: %d bytes", ESP.getFreeHeap());
  logToFile(heapMsg);
  DEBUG_PRINTF_P(PSTR("Free heap before BLE init: %d bytes\n"), ESP.getFreeHeap());

  // Check if we have enough free memory
  if (ESP.getFreeHeap() < 50000) {
    logToFile("ERROR: Not enough heap!");
    DEBUG_PRINTLN(F("BLE RSSI Scanner: ERROR - Not enough free heap for BLE!"));
    enabled = false;
    return;
  }

  // Need to enable WiFi sleep for BLE+WiFi coexistence
  // This is critical - BLE and WiFi share the 2.4GHz radio
  noWifiSleep = false;
  logToFile("Set noWifiSleep = false");

  // IMPORTANT: BLEDevice::init() can crash if called while WiFi is busy
  // We need to ensure WiFi is in a safe state
  logToFile("Putting WiFi into modem sleep mode...");
  WiFi.setSleep(true);  // Enable WiFi sleep
  delay(100);  // Give WiFi time to enter sleep mode

  // Initialize BLE with device name
  logToFile("Calling NimBLEDevice::init()...");
  DEBUG_PRINTLN(F("BLE RSSI Scanner: Calling NimBLEDevice::init()"));

  // Use a simple device name to avoid issues
  const char* bleName = "WLED-BLE";
  NimBLEDevice::init(bleName);

  logToFile("NimBLEDevice::init() completed");
  DEBUG_PRINTLN(F("BLE RSSI Scanner: NimBLEDevice::init() completed"));

  // Setup advertising
  logToFile("Getting advertising object...");
  pAdvertising = NimBLEDevice::getAdvertising();
  if (!pAdvertising) {
    logToFile("ERROR: Failed to get advertising object");
    DEBUG_PRINTLN(F("BLE RSSI Scanner: ERROR - Failed to get advertising object"));
    enabled = false;
    return;
  }
  logToFile("Got advertising object");

  pAdvertising->addServiceUUID(NimBLEUUID((uint16_t)0x180A));
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  logToFile("Starting advertising...");
  pAdvertising->start();
  logToFile("Advertising started");
  DEBUG_PRINTLN(F("BLE RSSI Scanner: Started advertising"));

  // Setup scanning
  logToFile("Getting scan object...");
  pBLEScan = NimBLEDevice::getScan();
  if (!pBLEScan) {
    logToFile("ERROR: Failed to get scan object");
    DEBUG_PRINTLN(F("BLE RSSI Scanner: ERROR - Failed to get scan object"));
    enabled = false;
    return;
  }
  logToFile("Got scan object");

  scanCallback = new ScanCallback(this);
  pBLEScan->setAdvertisedDeviceCallbacks(scanCallback);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);  // 100ms
  pBLEScan->setWindow(99);     // 99ms
  pBLEScan->setDuplicateFilter(false);  // Allow duplicate advertisements (important for RSSI sampling)

  // Store our own MAC address
  ownMacAddress = NimBLEDevice::getAddress().toString();
  snprintf(heapMsg, sizeof(heapMsg), "Own MAC address: %s", ownMacAddress.c_str());
  logToFile(heapMsg);

  bleInitialized = true;
  snprintf(heapMsg, sizeof(heapMsg), "BLE init SUCCESS. Free heap: %d bytes", ESP.getFreeHeap());
  logToFile(heapMsg);
  DEBUG_PRINTF_P(PSTR("BLE RSSI Scanner: BLE initialized. Free heap: %d bytes\n"), ESP.getFreeHeap());
}

void BLERSSIScannerUsermod::startScan() {
  if (!bleInitialized) {
    initBLE();
  }

  if (scanInProgress) {
    logToFile("Scan already in progress");
    DEBUG_PRINTLN(F("BLE RSSI Scanner: Scan already in progress"));
    return;
  }

  // Clear previous samples
  devices.clear();

  scanInProgress = true;
  scanStartTime = millis();

  logToFile("Starting BLE scan...");

  // Start continuous scan (duration=0 means continuous until stop)
  // Third parameter (is_continue) = false means clear previous results
  bool started = pBLEScan->start(0, nullptr, false);

  char msg[64];
  snprintf(msg, sizeof(msg), "Scan started: %s (duration=%ds)", started ? "SUCCESS" : "FAILED", scanDurationSec);
  logToFile(msg);

  DEBUG_PRINTF_P(PSTR("BLE RSSI Scanner: Started %d second scan\n"), scanDurationSec);
}

void BLERSSIScannerUsermod::stopScan() {
  if (!scanInProgress) return;

  pBLEScan->stop();
  scanInProgress = false;

  char msg[64];
  snprintf(msg, sizeof(msg), "Scan stopped. Devices found: %d", devices.size());
  logToFile(msg);

  DEBUG_PRINTLN(F("BLE RSSI Scanner: Stopped scan"));
}

String BLERSSIScannerUsermod::getResultsJSON() {
  DynamicJsonDocument doc(4096);

  doc["status"] = "complete";
  doc["scan_duration_sec"] = scanDurationSec;

  // Add reporter's own MAC address (stored during BLE init)
  if (!ownMacAddress.empty()) {
    doc["reporter_mac"] = ownMacAddress.c_str();
  }

  JsonArray devicesArray = doc.createNestedArray("devices");

  // Calculate average RSSI for each device
  for (const auto& entry : devices) {
    const DeviceInfo& deviceInfo = entry.second;
    const std::vector<int>& samples = deviceInfo.rssiSamples;

    if (samples.empty()) continue;

    // Calculate average
    int sum = 0;
    for (int rssi : samples) {
      sum += rssi;
    }
    float avg = (float)sum / samples.size();

    JsonObject device = devicesArray.createNestedObject();
    device["mac"] = deviceInfo.address.c_str();  // Always include MAC
    if (!deviceInfo.name.empty()) {
      device["name"] = deviceInfo.name.c_str();  // Optional name
    }
    device["rssi_avg"] = avg;
    device["sample_count"] = samples.size();
  }

  doc["device_count"] = devicesArray.size();

  String output;
  serializeJson(doc, output);
  return output;
}

void BLERSSIScannerUsermod::registerHTTPHandler() {
  DEBUG_PRINTLN(F("BLE RSSI Scanner: Registering HTTP handler"));

  // Endpoint 1: Start a new scan
  server.on("/api/ble-rssi-start", HTTP_GET, [this](AsyncWebServerRequest *request) {
    DEBUG_PRINTLN(F("BLE RSSI Scanner: Start scan request"));

    // Check for optional duration parameter
    if (request->hasParam("duration")) {
      AsyncWebParameter* p = request->getParam("duration");
      uint32_t duration = p->value().toInt();
      if (duration > 0 && duration <= 300) {
        scanDurationSec = duration;
      }
    }

    // Check if scan is already in progress
    if (scanInProgress) {
      DynamicJsonDocument doc(256);
      doc["status"] = "already_scanning";
      doc["message"] = "Scan already in progress";

      String output;
      serializeJson(doc, output);
      request->send(409, "application/json", output); // 409 Conflict
      return;
    }

    // Start new scan
    startScan();

    // Return immediate response
    DynamicJsonDocument doc(256);
    doc["status"] = "started";
    doc["duration_sec"] = scanDurationSec;
    doc["message"] = "BLE scan started. Call /api/ble-rssi-results after " +
                     String(scanDurationSec) + " seconds.";

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });

  // Endpoint 2: Get scan results or status
  server.on("/api/ble-rssi-results", HTTP_GET, [this](AsyncWebServerRequest *request) {
    DEBUG_PRINTLN(F("BLE RSSI Scanner: Results request"));

    // If still scanning
    if (scanInProgress) {
      uint32_t elapsed = (millis() - scanStartTime) / 1000;
      uint32_t remaining = scanDurationSec > elapsed ? scanDurationSec - elapsed : 0;

      DynamicJsonDocument doc(256);
      doc["status"] = "scanning";
      doc["elapsed_sec"] = elapsed;
      doc["remaining_sec"] = remaining;
      doc["devices_found"] = devices.size();

      String output;
      serializeJson(doc, output);
      request->send(200, "application/json", output);
      return;
    }

    // If no scan has been done yet
    if (devices.empty()) {
      DynamicJsonDocument doc(128);
      doc["status"] = "no_data";
      doc["message"] = "No scan data available. Call /api/ble-rssi-start first.";

      String output;
      serializeJson(doc, output);
      request->send(200, "application/json", output);
      return;
    }

    // Scan complete, return results
    String response = getResultsJSON();
    request->send(200, "application/json", response);
  });

  // Endpoint 3: Get debug log
  server.on("/api/ble-rssi-log", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String log = getLogContents();
    request->send(200, "text/plain", log);
  });

  // Endpoint 4: Clear debug log
  server.on("/api/ble-rssi-log-clear", HTTP_GET, [this](AsyncWebServerRequest *request) {
    LittleFS.remove(LOG_FILE);
    logToFile("Log cleared");
    request->send(200, "text/plain", "Log cleared");
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
  DEBUG_PRINTLN(F("BLE RSSI Scanner: connected() - registering HTTP handlers"));
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
    } else if (!devices.empty()) {
      infoArr.add(String(devices.size()) + " devices found");
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
