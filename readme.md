# BLE RSSI Scanner Usermod

This usermod enables ESP32 devices to perform Bluetooth Low Energy (BLE) RSSI measurements for relative position estimation between multiple WLED devices.

## Features

- **Simultaneous BLE advertising and scanning**: Device advertises itself while scanning for other devices
- **RSSI measurement**: Collects signal strength samples over a configurable time window
- **HTTP API**: Simple REST endpoint to trigger scans and retrieve results
- **Averaging**: Returns average RSSI per device to reduce noise

## Hardware Requirements

### Supported Platforms
- ✅ ESP32 (Classic Bluetooth + BLE)
- ✅ ESP32-S3 (Classic Bluetooth + BLE)
- ✅ ESP32-C3 (BLE only)
- ❌ ESP32-S2 (No BLE hardware)
- ❌ ESP8266 (No BLE support)

## Use Case

This usermod is designed for **relative position estimation** between multiple WLED devices using RSSI (Received Signal Strength Indicator) measurements. By measuring the signal strength between devices, you can estimate their relative distances and positions.

### Typical Workflow

1. Client calls `/api/ble-rssi-scan` on all ESP32 devices simultaneously
2. Each device:
   - Starts advertising its name via BLE
   - Scans for other BLE devices for the configured duration (default: 15 seconds)
   - Collects RSSI samples from all detected devices
3. Client retrieves results from each device via the same endpoint or `/api/ble-rssi-results`
4. Client processes RSSI data for position estimation algorithms

## Installation

### Using PlatformIO

1. Copy this folder to `WLED/usermods/ble_rssi_scanner/`

2. Create or modify `platformio_override.ini` in your WLED root directory:

```ini
[env:my_esp32_board]
extends = env:esp32dev  # or your target environment
build_flags = ${common.build_flags} ${esp32.build_flags}
  -D USERMOD_BLE_RSSI_SCANNER
  # Optional: Set custom scan duration (default is 15 seconds)
  # -D BLE_RSSI_SCAN_DURATION_SEC=20
lib_deps = ${esp32.lib_deps}
  ESP32 BLE Arduino
```

3. Register the usermod in `usermods_list.cpp`:

```cpp
#ifdef USERMOD_BLE_RSSI_SCANNER
  #include "../usermods/ble_rssi_scanner/usermod_ble_rssi_scanner.h"
#endif
```

4. Build and upload to your ESP32 device

## Configuration

### Compile-Time Options

- `USERMOD_BLE_RSSI_SCANNER`: Enable this usermod (required)
- `BLE_RSSI_SCAN_DURATION_SEC`: Default scan duration in seconds (default: 15)

### Runtime Configuration

Configuration can be set via WLED's usermod config interface or by editing `cfg.json`:

```json
{
  "BLE_RSSI_Scanner": {
    "enabled": true,
    "scan_duration_sec": 15
  }
}
```

**Parameters:**
- `enabled`: Enable/disable the usermod (default: `true`)
- `scan_duration_sec`: Duration of each scan in seconds (1-300, default: 15)

## API Endpoints

### Start Scan: `GET /api/ble-rssi-start`

Starts a new BLE RSSI scan. Returns immediately while scan runs in background.

**Query Parameters:**
- `duration` (optional): Override scan duration in seconds (1-300, default: 10)

**Response (Started - HTTP 200):**
```json
{
  "status": "started",
  "duration_sec": 10,
  "message": "BLE scan started. Call /api/ble-rssi-results after 10 seconds."
}
```

**Response (Already Scanning - HTTP 409):**
```json
{
  "status": "already_scanning",
  "message": "Scan already in progress"
}
```

### Get Results: `GET /api/ble-rssi-results`

Gets the current scan status and results.

**Response (Still Scanning - HTTP 200):**
```json
{
  "status": "scanning",
  "elapsed_sec": 5,
  "remaining_sec": 5,
  "devices_found": 2
}
```

**Response (Scan Complete - HTTP 200):**
```json
{
  "status": "complete",
  "scan_duration_sec": 10,
  "device_count": 2,
  "devices": [
    {
      "name": "WLED-Device1",
      "rssi_avg": -65.4,
      "sample_count": 142
    },
    {
      "name": "WLED-Device2",
      "rssi_avg": -78.2,
      "sample_count": 98
    }
  ]
}
```

**Response (No Data - HTTP 200):**
```json
{
  "status": "no_data",
  "message": "No scan data available. Call /api/ble-rssi-start first."
}
```

## Usage Examples

### Basic Usage

1. **Start scan on all devices** (from your client application):
   ```bash
   curl http://wled-device1.local/api/ble-rssi-start
   curl http://wled-device2.local/api/ble-rssi-start
   curl http://wled-device3.local/api/ble-rssi-start
   ```

2. **Wait for scan duration** (e.g., 10 seconds)

3. **Retrieve results**:
   ```bash
   curl http://wled-device1.local/api/ble-rssi-results
   curl http://wled-device2.local/api/ble-rssi-results
   curl http://wled-device3.local/api/ble-rssi-results
   ```

### Using the Test Script

A test script is included that automates the scan process:

```bash
cd usermods/ble_rssi_scanner
./test_scan.sh 192.168.1.100      # Use default 10s duration
./test_scan.sh 192.168.1.100 15   # Use custom 15s duration
```

The script will:
- Start the scan
- Poll for status every 2 seconds
- Display progress and final results

### Custom Scan Duration

Trigger a 30-second scan:
```bash
curl "http://wled-device1.local/api/ble-rssi-start?duration=30"
```

### Python Example

```python
import requests
import time

devices = [
    "http://wled-device1.local",
    "http://wled-device2.local",
    "http://wled-device3.local"
]

# Start scans on all devices
duration = 15
for device in devices:
    response = requests.get(f"{device}/api/ble-rssi-scan?duration={duration}")
    print(f"Started scan on {device}: {response.json()}")

# Wait for scan to complete
time.sleep(duration + 2)  # Add 2 seconds buffer

# Retrieve results
results = {}
for device in devices:
    response = requests.get(f"{device}/api/ble-rssi-results")
    results[device] = response.json()
    print(f"Results from {device}:")
    for found_device in results[device]["devices"]:
        print(f"  {found_device['name']}: {found_device['rssi_avg']} dBm")
```

## Technical Details

### BLE Implementation

- **Library**: ESP32 BLE Arduino (official ESP-IDF wrapper)
- **Advertising**: Broadcasts device name using Device Information Service (UUID 0x180A)
- **Scanning**: Active scan with 100ms interval, 99ms window for optimal detection
- **WiFi Coexistence**: Automatically enables WiFi sleep mode for BLE+WiFi compatibility

### Device Identification

Devices are identified by their **advertised BLE name**, which corresponds to WLED's `serverDescription` setting (default: "WLED-" + chip ID).

### RSSI Averaging

- All RSSI samples for each device are collected during the scan window
- Average is calculated as: `rssi_avg = sum(samples) / sample_count`
- Sample count indicates reliability (more samples = more reliable measurement)

### Performance Considerations

- **Memory**: Uses dynamic memory for storing samples (approximately 20-30 bytes per detected device)
- **CPU**: BLE scanning runs in background tasks, minimal impact on LED rendering
- **Range**: Typical BLE range is 10-100m depending on environment and antenna

## Position Estimation

RSSI values can be used for trilateration or other positioning algorithms. Note that:

- **RSSI is not distance**: Signal strength varies with obstacles, interference, and antenna orientation
- **Calibration recommended**: Measure RSSI at known distances in your environment
- **Use averaging**: Longer scan durations provide more stable measurements
- **Multiple measurements**: Perform several scans and average for better accuracy

### RSSI to Distance (Approximate)

A rough approximation using the log-distance path loss model:
```
distance (meters) ≈ 10 ^ ((TxPower - RSSI) / (10 * n))
```
Where:
- `TxPower` = RSSI at 1 meter (typically -59 to -69 dBm)
- `n` = path loss exponent (2 for free space, 3-4 for indoor environments)

**Note**: This is very approximate and should be calibrated for your specific environment.

## Troubleshooting

### Scan returns no devices

- Ensure all devices have BLE enabled and are advertising
- Check that devices are within BLE range (typically 10-50m)
- Verify WiFi sleep mode is enabled (`noWifiSleep = false`)
- Try increasing scan duration

### "BLE not supported on this hardware" message

Your ESP32 variant doesn't support BLE (e.g., ESP32-S2). Use ESP32, ESP32-S3, or ESP32-C3 instead.

### Heap allocation errors

BLE requires significant memory. Ensure your ESP32 has sufficient free heap (check WLED's info page). Consider:
- Reducing LED count
- Disabling other memory-intensive usermods
- Using a variant with PSRAM (ESP32-WROVER, ESP32-S3 with PSRAM)

### Interference with WiFi

BLE and WiFi share the 2.4GHz band. If you experience connectivity issues:
- Keep scan duration short (15-30 seconds)
- Don't run continuous scans
- Use WiFi channels 1, 6, or 11 (non-overlapping)

## Credits

- Inspired by the `pixels_dice_tray` usermod's BLE implementation
- Uses the ESP32 BLE Arduino library

## License

This usermod is part of WLED and follows the same license (MIT).
