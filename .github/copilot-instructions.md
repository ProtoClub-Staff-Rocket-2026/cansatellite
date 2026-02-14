# Copilot Instructions for CanSatellite Flight Computer

## Project Overview

This is an ESP8266-based flight computer for a CanSat/rocket that collects telemetry data from a BMP180 sensor and:
1. Sends data to a Ground Control server via HTTP POST
2. Logs all data to CSV files on LittleFS (internal flash storage)
3. Maintains a RAM buffer for recent samples

The flight computer is designed for autonomous operation - it automatically starts logging on boot and continues regardless of server connectivity.

## Platform & Dependencies

**Hardware**: ESP8266 (Dasduino or Generic ESP8266 Module)

**Required Libraries**:
- Adafruit BMP085 Library (install via Arduino IDE Library Manager)
- Built-in: ESP8266WiFi, ESP8266HTTPClient, Wire, LittleFS

**Board Configuration**:
- Flash Size: Must include LittleFS partition (e.g., "4MB (FS:2MB OTA:~1019KB)")
- Upload Speed: 115200 baud

## Build & Upload

**Arduino IDE**:
1. Open `flightcomputer.ino`
2. Configure WiFi credentials and SERVER_URL in code
3. Select board: Generic ESP8266 Module
4. Upload via USB

**Testing**: Open Serial Monitor at 115200 baud to see initialization and runtime logs

## Architecture

### Data Flow Pipeline
```
Sensor Read (BMP180) → LogSample struct → Three destinations:
  1. RAM circular buffer (1200 samples, ~10 min @ 2 Hz)
  2. LittleFS CSV file (persistent, one file per session)
  3. HTTP POST to Ground Control server (best-effort)
```

### Key State Management
- **FlightState enum**: Controls whether logging is active (IDLE/ARMED/LOGGING)
- **Session lifecycle**: Each boot generates unique sessionId and creates new CSV file
- **Timing**: All timestamps use both relative (millis) and absolute (epoch via NTP)

### Session ID Format
`{MAC_3BYTES}_{MILLIS}_{RANDOM}` (e.g., `A1B2C3_0000123F_4A5B6C`)
- Ensures uniqueness across boots without requiring internet connectivity
- Used as CSV filename and identifier in server payloads

### HTTP Client Pattern
- **Endpoint**: `POST /events/` on Ground Control server
- **Payload**: JSON with `{timestamp, identifier, velocity, air_pressure}`
- **Error handling**: "Try once, continue regardless" - server failures don't stop logging
- **Timeout**: 5 seconds per request

### File System
- **Storage**: LittleFS (ESP8266 internal flash)
- **CSV format**: One file per session, persists across reboots
- **Header**: `session,timestamp,identifier,t_ms,ts_epoch,tempC,pressure_hPa,alt_m,velocity,rssi_dBm,cpu`
- **Wear consideration**: ~100K write cycles available; 2 Hz sampling = minimal wear

## Code Conventions

### Configuration Constants
Hardcoded at top of file (lines 10-16):
```cpp
const char* ssid     = "";          // User must set
const char* password = "";          // User must set  
const char* SERVER_URL = "http://...";  // User must set
const float SEA_LEVEL_HPA = 1038;  // Local QNH for altitude calc
```

### Timing Pattern
All periodic operations use static variables with `millis()` comparison:
```cpp
static uint32_t lastMs = 0;
if (nowMs - lastMs < INTERVAL) return;
lastMs = nowMs;
```

### Interrupt-Safe Buffer Access
RAM buffer (`logBuf`) operations wrap critical sections:
```cpp
noInterrupts();
// ... modify logHead/logCount ...
interrupts();
```

### Error Handling Philosophy
- **Sensor init fails**: Block in infinite loop (fatal, can't operate)
- **File system fails**: Log to Serial, continue (data goes to server/RAM)
- **Server fails**: Log to Serial, continue (data goes to file/RAM)
- **WiFi disconnects**: Set RSSI to -999, continue logging to file

### Adding New Sensors
1. Add sensor initialization in `setup()`
2. Read sensor in `maybeLogSample()` 
3. Add fields to `LogSample` struct and CSV header in `resetLogSession()`
4. Update `appendToCsv()` to include new fields
5. Optionally add to server payload in `sendToServer()` if server expects it

Example: **Velocity sensor placeholder**
```cpp
// Placeholder for velocity sensor (future implementation)
float getVelocity() {
  return 0.0f;  // Replace with actual sensor read
}
```

### Sample Rate Adjustment
Modify interval check in `maybeLogSample()`:
- 2 Hz (current): `if (nowMs - lastMs < 500) return;`
- 1 Hz: `< 1000`
- 5 Hz: `< 200`
- 10 Hz: `< 100`

Note: Higher rates increase flash wear and server load.

## Ground Control Server Integration

**Server Repository**: Expected to be a separate FastAPI backend (see README Ground Control section)

**API Contract**:
- Endpoint: `POST /events/`
- Content-Type: `application/json`
- Expected response: `204 No Content` or `200 OK`
- Any other response is logged as failure

**Server Payload Fields**:
- `timestamp`: ISO-8601 UTC string
- `identifier`: Session ID
- `velocity`: Float (currently always 0.00)
- `air_pressure`: Float in hPa

**CSV has additional fields** not sent to server:
- `tempC`, `alt_m`, `rssi_dBm`, `cpu`, `t_ms`, `ts_epoch`

## Common Modifications

### Change Server Endpoint
Edit line 13: `const char* SERVER_URL = "http://...";`

### Change QNH for Altitude Calculation
Edit line 16: `const float SEA_LEVEL_HPA = 1038;`

### Disable Server Communication
Comment out lines 227-230 in `maybeLogSample()`:
```cpp
// if (WiFi.status() == WL_CONNECTED) {
//   sendToServer(s, velocity);
// }
```

### Access CSV Files from Device
Current options:
1. Arduino ESP8266 filesystem uploader tool (browse LittleFS via USB)
2. Add HTTP endpoint for file download (requires code modification)
3. Add Serial commands to dump files (requires code modification)

## Debugging

**Serial output includes**:
- Boot sequence (sensor init, filesystem, WiFi, NTP sync)
- CSV file creation confirmation
- Server POST results (OK with HTTP code, or failed)
- Status every 10 seconds (sample count, session ID, WiFi status)

**Common issues**:
- WiFi fails: Check 2.4GHz network, verify credentials
- LittleFS init fails: Verify board flash size configuration includes LittleFS
- Server POST fails: Check SERVER_URL, ensure device and server on same network
- CSV not created: Check LittleFS init, may be out of flash space

## Safety Notes

- **Flash wear**: LittleFS has ~100K write cycles per sector. At 2 Hz, even continuous operation for hours is safe.
- **Power stability**: Ensure stable power during writes to prevent corruption
- **Storage management**: CSV files persist across boots and accumulate. Monitor available space for long-term deployments.
