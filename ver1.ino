#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <core_version.h>   // ESP.getCoreVersion()

const char* ssid     = "...";
const char* password = "...";

ESP8266WebServer server(80);

static String htmlPage() {
  IPAddress ip = WiFi.localIP();
  String ipStr = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);

  long rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -999;
  uint32_t up_s = millis() / 1000;

  uint32_t heap = ESP.getFreeHeap();
  uint32_t chipId = ESP.getChipId();
  uint32_t flashId = ESP.getFlashChipId();
  uint32_t flashSize = ESP.getFlashChipRealSize();
  uint32_t cpuMHz = ESP.getCpuFreqMHz();

  String coreVer = ESP.getCoreVersion();   // Arduino core version string
  String sdkVer  = ESP.getSdkVersion();    // SDK version string

  String s;
  s.reserve(1400);

  s += "<!doctype html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  s += "<title>ESP8266 Status</title>";
  s += "<style>body{font-family:ui-monospace,monospace;margin:20px}"
       ".card{max-width:700px;border:1px solid #ddd;border-radius:14px;padding:16px}"
       "h2{margin:0 0 12px}table{width:100%;border-collapse:collapse}"
       "td{padding:6px 0;border-bottom:1px solid #eee}td:first-child{color:#555;width:45%}"
       ".dim{color:#777;font-size:12px;margin-top:10px}"
       "</style></head><body>";
  s += "<div class='card'>";
  s += "<h2>ESP8266 Status</h2>";
  s += "<table>";

  s += "<tr><td>WiFi SSID</td><td>" + String(ssid) + "</td></tr>";
  s += "<tr><td>IP</td><td>" + ipStr + "</td></tr>";
  s += "<tr><td>RSSI</td><td>" + String(rssi) + " dBm</td></tr>";

  s += "<tr><td>Uptime</td><td>" + String(up_s) + " s</td></tr>";
  s += "<tr><td>Free heap</td><td>" + String(heap) + " B</td></tr>";

  s += "<tr><td>CPU frequency</td><td>" + String(cpuMHz) + " MHz</td></tr>";
  s += "<tr><td>Chip ID</td><td>" + String(chipId, HEX) + "</td></tr>";
  s += "<tr><td>Flash ID</td><td>" + String(flashId, HEX) + "</td></tr>";
  s += "<tr><td>Flash size</td><td>" + String(flashSize) + " B</td></tr>";

  s += "<tr><td>SDK</td><td>" + sdkVer + "</td></tr>";
  s += "<tr><td>Arduino core</td><td>" + coreVer + "</td></tr>";

  s += "</table>";
  s += "<div class='dim'>Päivitä sivu (F5) nähdäksesi muuttuvat arvot (uptime, RSSI, heap).</div>";
  s += "</div></body></html>";

  return s;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", htmlPage());
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Yhdistetaan WiFiin");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Avaa selaimessa: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  server.on("/", HTTP_GET, handleRoot);
  server.begin();
  Serial.println("Web-palvelin kaynnissa");
}

void loop() {
  server.handleClient();
}
