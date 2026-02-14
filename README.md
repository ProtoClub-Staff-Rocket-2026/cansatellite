# Can Satellite Flight Computer

## Overview

The flight computer sends telemetry data to Ground Control server and logs all
data to CSV files on the device's internal flash storage (LittleFS).

This project is built for the Dasduino Connect with ESP8266. 

To install the board in Arduino IDE, add `https://github.com/SolderedElectronics/Dasduino-Board-Definitions-for-Arduino-IDE/raw/master/package_Dasduino_Boards_index.json`
in the `Additional boards manager URLs` and install the version `1.0.1` from the board manager.

## Before Uploading

### 1. Configure WiFi and Server
Edit `flightcomputer.ino` and set these values:

```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* SERVER_URL = "http://192.168.1.100:8000/events/";
```

Replace with your actual WiFi credentials and Ground Control server address.

### 2. Install Required Libraries
Using Arduino IDE Library Manager, install:
- **Adafruit BMP085 Library** (for BMP180 sensor)

Built-in libraries (no installation needed):
- ESP8266WiFi
- ESP8266HTTPClient
- Wire
- LittleFS

### 3. Board Configuration
- Board: **Generic ESP8266 Module** (or your specific Dasduino board)
- Flash Size: Select option with **LittleFS** (e.g., "4MB (FS:2MB OTA:~1019KB)")
- Upload Speed: 115200 (or lower if having issues)

## Upload Process

1. Connect Dasduino via USB
2. Select correct COM port in Arduino IDE
3. Click Upload
4. Wait for compilation and upload to complete

## Testing

### Serial Monitor
Open Serial Monitor at **115200 baud** to see:

```
BMP180 init... OK
LittleFS init... OK
Connecting to WiFi.....
IP: 192.168.1.123
Waiting for NTP sync...
CSV file created: /A1B2C3_0000123F_4A5B6C.csv
Logging started
Server POST OK (204)
Server POST OK (204)
Status: samples=20, session=A1B2C3_0000123F_4A5B6C, WiFi=OK
```

### Expected Behavior
- **Every 500ms (2 Hz)**: Collects sensor data
- **Data saved to**: CSV file on LittleFS (persists across reboots)
- **Data sent to**: Ground Control server via HTTP POST
- **If server fails**: Continues logging to CSV regardless

### Status Messages
- Every **10 seconds**: Prints sample count, session ID, and WiFi status
- Every **sample**: Logs server POST result (OK or failed)

## Ground Control Server

The flight computer sends JSON data to the `/events/` endpoint:

```json
{
  "timestamp": "2026-02-14T18:20:59Z",
  "identifier": "A1B2C3_0000123F_4A5B6C",
  "velocity": 0.00,
  "air_pressure": 1013.25
}
```

Expected response: `204 No Content` or `200 OK`

## CSV File Format

Files are stored in LittleFS root with session ID as filename.

**Example**: `/A1B2C3_0000123F_4A5B6C.csv`

**Header**:
```csv
session,timestamp,identifier,t_ms,ts_epoch,tempC,pressure_hPa,alt_m,velocity,rssi_dBm,cpu
```

**Data includes**:
- All sensor readings (temperature, pressure, altitude)
- WiFi signal strength (RSSI)
- CPU load
- Timestamps (both milliseconds and epoch)
- Velocity (placeholder: 0.00)

## Troubleshooting

### WiFi Connection Fails
- Check SSID and password are correct
- Ensure WiFi network is 2.4GHz (ESP8266 doesn't support 5GHz)
- Check signal strength in deployment location

### LittleFS Init Failed
- Verify board configuration has LittleFS enabled
- Try re-uploading with "Erase Flash: All Flash Contents" option

### Server POST Fails
- Verify Ground Control server is running
- Check SERVER_URL is correct (include port number)
- Ensure Dasduino and server are on same network
- Test with: `curl -X POST http://192.168.1.100:8000/events/ -H "Content-Type: application/json" -d '{"timestamp":"t1","identifier":"test","velocity":0,"air_pressure":1013}'`

### CSV File Not Created
- Check LittleFS initialized successfully
- Flash memory might be full (files persist across boots)
- Access files via Serial file browser or SPIFFS upload tool

## Accessing CSV Files

### Option 1: Add File Download Endpoint (Future)
You could add a simple HTTP endpoint to download CSV files.

### Option 2: Serial File Transfer
Use Arduino ESP8266 filesystem uploader tool to browse LittleFS.

### Option 3: Read via Serial
Add Serial commands to list and dump files (requires code modification).

## Session ID Format

Each boot generates a unique session ID:

**Format**: `{MAC_3BYTES}_{MILLIS}_{RANDOM}`

**Example**: `A1B2C3_0000123F_4A5B6C`

- MAC: Last 3 bytes of device MAC address
- MILLIS: Boot timestamp
- RANDOM: Random number for uniqueness

## Next Steps

### Adding Velocity Sensor
1. Locate the `getVelocity()` function in code
2. Replace `return 0.0f;` with actual sensor reading
3. Add necessary sensor initialization in `setup()`

### Adjusting Sample Rate
Change the interval in `maybeLogSample()`:
```cpp
if (nowMs - lastMs < 500) return;  // 500ms = 2 Hz
```

For 1 Hz: use `1000`
For 5 Hz: use `200`

### Customizing CSV Fields
Modify `appendToCsv()` function to add/remove fields as needed.

## Safety Notes

- **Flash wear**: LittleFS has limited write cycles (~100K). At 2 Hz, a
    1-minute flight = 120 writes to one file = minimal wear.
- **Storage capacity**: Monitor available space, old sessions accumulate.
- **Power stability**: Ensure stable power during writes to prevent file corruption.

## Support

Check Serial Monitor output for detailed error messages. All operations log
their status for debugging.


