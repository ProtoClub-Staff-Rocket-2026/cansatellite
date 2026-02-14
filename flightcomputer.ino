#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <LittleFS.h>
#include <math.h>
#include <time.h>

// ====== DEV MODE ======
// Comment out the next line to disable all Serial output for production
#define DEV_MODE

#ifdef DEV_MODE
  #define SERIAL_BEGIN(baud) Serial.begin(baud)
  #define SERIAL_PRINT(x) Serial.print(x)
  #define SERIAL_PRINTLN(x) Serial.println(x)
#else
  #define SERIAL_BEGIN(baud) ((void)0)
  #define SERIAL_PRINT(x) ((void)0)
  #define SERIAL_PRINTLN(x) ((void)0)
#endif

// ====== WIFI & SERVER ======
const char* ssid     = "";
const char* password = "";
const char* SERVER_URL = "http://192.168.1.100:8000/events/";

// ====== SENSOR / CALIB ======
const float SEA_LEVEL_HPA = 1038;   // QNH (hPa) korkeuden laskentaan

// ====== GLOBALS ======
Adafruit_BMP085 bmp;
WiFiClient wifiClient;

volatile int cpuLoad = 0;

// ====== FLIGHT / LOG STATE ======
enum FlightState : uint8_t { IDLE=0, ARMED=1, LOGGING=2 };
volatile FlightState flightState = IDLE;

String sessionId = "";
uint32_t sessionStartMs = 0;      // millis() at session start
uint32_t sessionStartEpoch = 0;   // epoch seconds at session start (if NTP ok, else 0)
String csvFilename = "";

struct LogSample {
  uint32_t t_ms;          // millis since session start
  uint32_t ts_epoch;      // epoch seconds (0 if unknown)
  float tempC;
  float pressureHpa;
  float altitudeM;
  int16_t rssi;
  uint8_t cpu;
};

static const uint16_t LOG_CAP = 1200; // ~10 min @ 2 Hz (500 ms)
LogSample logBuf[LOG_CAP];
volatile uint16_t logHead = 0;   // next write index
volatile uint16_t logCount = 0;  // how many valid samples (<=LOG_CAP)

// ====== TIME (NTP) ======
static void setupTimeNtp() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  if (t < 1700000000) return 0;
  return (uint32_t)t;
}

static void formatIso8601Z(uint32_t epoch, char* out, size_t outsz) {
  if (epoch == 0) {
    snprintf(out, outsz, "--");
    return;
  }
  time_t t = (time_t)epoch;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  snprintf(out, outsz, "%04d-%02d-%02dT%02d:%02d:%02dZ",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

void updateCpuLoad() {
  static unsigned long last = micros();
  unsigned long now = micros();
  unsigned long dt = now - last;
  last = now;

  int load = (int)map((long)dt, 2000, 20000, 80, 5);
  if (load < 0) load = 0;
  if (load > 100) load = 100;

  cpuLoad = (cpuLoad * 7 + load * 3) / 10;
}

// Placeholder for velocity sensor (future implementation)
float getVelocity() {
  return 0.0f;
}

String generateSessionId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  uint32_t now = millis();
  uint32_t rnd = random(0xFFFFFF);
  
  char buf[32];
  snprintf(buf, sizeof(buf), "%02X%02X%02X_%08lX_%06lX",
           mac[3], mac[4], mac[5],
           (unsigned long)now,
           (unsigned long)rnd);
  return String(buf);
}

void resetLogSession() {
  noInterrupts();
  logHead = 0;
  logCount = 0;
  interrupts();

  sessionId = generateSessionId();
  sessionStartMs = millis();
  sessionStartEpoch = nowEpoch();
  
  // Create new CSV file
  csvFilename = "/" + sessionId + ".csv";
  File f = LittleFS.open(csvFilename, "w");
  if (f) {
    f.println("session,timestamp,identifier,t_ms,ts_epoch,tempC,pressure_hPa,alt_m,velocity,rssi_dBm,cpu");
    f.close();
    SERIAL_PRINT("CSV file created: ");
    SERIAL_PRINTLN(csvFilename);
  } else {
    SERIAL_PRINTLN("ERROR: Failed to create CSV file");
  }
}

void appendToCsv(const LogSample& s, float velocity) {
  File f = LittleFS.open(csvFilename, "a");
  if (!f) {
    SERIAL_PRINTLN("ERROR: Failed to open CSV for append");
    return;
  }
  
  char tsBuf[32];
  formatIso8601Z(s.ts_epoch, tsBuf, sizeof(tsBuf));
  
  char line[256];
  snprintf(line, sizeof(line),
    "%s,%s,%s,%lu,%lu,%.2f,%.1f,%.1f,%.2f,%d,%u\n",
    sessionId.c_str(),
    tsBuf,
    sessionId.c_str(),
    (unsigned long)s.t_ms,
    (unsigned long)s.ts_epoch,
    s.tempC,
    s.pressureHpa,
    s.altitudeM,
    velocity,
    (int)s.rssi,
    (unsigned int)s.cpu
  );
  
  f.print(line);
  f.close();
}

bool sendToServer(const LogSample& s, float velocity) {
  HTTPClient http;
  
  char tsBuf[32];
  formatIso8601Z(s.ts_epoch, tsBuf, sizeof(tsBuf));
  
  String jsonPayload;
  jsonPayload.reserve(200);
  jsonPayload += "{";
  jsonPayload += "\"timestamp\":\"" + String(tsBuf) + "\",";
  jsonPayload += "\"identifier\":\"" + sessionId + "\",";
  jsonPayload += "\"velocity\":" + String(velocity, 2) + ",";
  jsonPayload += "\"air_pressure\":" + String(s.pressureHpa, 2);
  jsonPayload += "}";
  
  http.begin(wifiClient, SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  
  int httpCode = http.POST(jsonPayload);
  http.end();
  
  if (httpCode == 204 || httpCode == 200) {
    SERIAL_PRINT("Server POST OK (");
    SERIAL_PRINT(httpCode);
    SERIAL_PRINTLN(")");
    return true;
  } else {
    SERIAL_PRINT("Server POST failed: ");
    SERIAL_PRINTLN(httpCode);
    return false;
  }
}

void maybeLogSample() {
  static uint32_t lastMs = 0;
  uint32_t nowMs = millis();

  // loggaa 2 Hz (500 ms) kun LOGGING
  if (flightState != LOGGING) return;
  if (nowMs - lastMs < 500) return;
  lastMs = nowMs;

  LogSample s;
  s.t_ms = nowMs - sessionStartMs;

  if (sessionStartEpoch != 0) {
    s.ts_epoch = sessionStartEpoch + (s.t_ms / 1000);
  } else {
    s.ts_epoch = nowEpoch();
  }

  s.tempC = bmp.readTemperature();
  s.pressureHpa = bmp.readPressure() / 100.0f;
  s.altitudeM = bmp.readAltitude(SEA_LEVEL_HPA * 100.0f);
  s.rssi = (WiFi.status() == WL_CONNECTED) ? (int16_t)WiFi.RSSI() : (int16_t)-999;
  s.cpu = (uint8_t)cpuLoad;
  
  float velocity = getVelocity();

  // Store in RAM buffer
  noInterrupts();
  logBuf[logHead] = s;
  logHead = (logHead + 1) % LOG_CAP;
  if (logCount < LOG_CAP) logCount++;
  interrupts();
  
  // Write to CSV file
  appendToCsv(s, velocity);
  
  // Send to server (try once, continue regardless)
  if (WiFi.status() == WL_CONNECTED) {
    sendToServer(s, velocity);
  }
}

void setup() {
  SERIAL_BEGIN(115200);
  SERIAL_PRINTLN();

  // I2C: SDA=D2(GPIO4), SCL=D1(GPIO5)
  Wire.begin(4, 5);

  SERIAL_PRINT("BMP180 init... ");
  if (!bmp.begin()) {
    SERIAL_PRINTLN("FAILED");
    while (true) delay(1000);
  }
  SERIAL_PRINTLN("OK");

  // Initialize LittleFS
  SERIAL_PRINT("LittleFS init... ");
  if (!LittleFS.begin()) {
    SERIAL_PRINTLN("FAILED");
    while (true) delay(1000);
  }
  SERIAL_PRINTLN("OK");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  SERIAL_PRINT("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    SERIAL_PRINT(".");
  }
  SERIAL_PRINTLN();
  SERIAL_PRINT("IP: ");
  SERIAL_PRINTLN(WiFi.localIP());

  // NTP time
  setupTimeNtp();
  SERIAL_PRINTLN("Waiting for NTP sync...");
  delay(2000);

  // Start logging automatically on boot
  resetLogSession();
  flightState = LOGGING;
  SERIAL_PRINTLN("Logging started");
}

void loop() {
  updateCpuLoad();
  maybeLogSample();
  
  // Periodic status output
  static uint32_t lastStatusMs = 0;
  if (millis() - lastStatusMs > 10000) {
    lastStatusMs = millis();
    SERIAL_PRINT("Status: samples=");
    SERIAL_PRINT(logCount);
    SERIAL_PRINT(", session=");
    SERIAL_PRINT(sessionId);
    SERIAL_PRINT(", WiFi=");
    SERIAL_PRINTLN(WiFi.status() == WL_CONNECTED ? "OK" : "DISCONNECTED");
  }
}
