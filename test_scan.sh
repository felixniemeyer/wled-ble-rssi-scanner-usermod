#!/bin/bash

# Test script for BLE RSSI Scanner
# Usage: ./test_scan.sh <ip_address> [duration_seconds]
#
# Example: ./test_scan.sh 192.168.178.24
#          ./test_scan.sh 192.168.178.24 15

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <ip_address> [duration_seconds]"
    echo "Example: $0 192.168.178.24 10"
    exit 1
fi

IP=$1
DURATION=${2:-10}  # Default 10 seconds if not specified

echo "=== BLE RSSI Scanner Test ==="
echo "Device IP: $IP"
echo "Scan Duration: ${DURATION}s"
echo ""

# Start the scan
echo "1. Starting BLE scan..."
START_RESPONSE=$(curl -s "http://${IP}/api/ble-rssi-start?duration=${DURATION}")
echo "$START_RESPONSE" | jq .

STATUS=$(echo "$START_RESPONSE" | jq -r '.status')

if [ "$STATUS" != "started" ]; then
    echo "Error: Failed to start scan"
    exit 1
fi

echo ""
echo "2. Waiting ${DURATION} seconds for scan to complete..."

# Check status every 2 seconds
for i in $(seq 1 $((DURATION / 2))); do
    sleep 2
    PROGRESS=$(curl -s "http://${IP}/api/ble-rssi-results")
    SCAN_STATUS=$(echo "$PROGRESS" | jq -r '.status')

    if [ "$SCAN_STATUS" = "scanning" ]; then
        ELAPSED=$(echo "$PROGRESS" | jq -r '.elapsed_sec')
        REMAINING=$(echo "$PROGRESS" | jq -r '.remaining_sec')
        DEVICES=$(echo "$PROGRESS" | jq -r '.devices_found')
        echo "   Status: Scanning... ${ELAPSED}s elapsed, ${REMAINING}s remaining, ${DEVICES} devices found"
    elif [ "$SCAN_STATUS" = "complete" ]; then
        echo "   Scan completed early!"
        break
    fi
done

# Add a small buffer
sleep 2

echo ""
echo "3. Fetching final results..."
RESULTS=$(curl -s "http://${IP}/api/ble-rssi-results")
echo "$RESULTS" | jq .

# Summary
echo ""
echo "=== Summary ==="
DEVICE_COUNT=$(echo "$RESULTS" | jq -r '.device_count // 0')
echo "Devices found: $DEVICE_COUNT"

if [ "$DEVICE_COUNT" -gt 0 ]; then
    echo ""
    echo "Device details:"
    echo "$RESULTS" | jq -r '.devices[] | "  - \(.name): \(.rssi_avg) dBm (samples: \(.sample_count))"'
fi

echo ""
echo "=== Test Complete ==="
