#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* ssid = "...";
const char* password = "...";

ESP8266WebServer server(80);

void handleRoot() {
  server.send(200, "text/plain", "Toimii!");
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  server.on("/", handleRoot);
  server.begin();

  Serial.println();
  Serial.print("Avaa selaimessa: http://");
  Serial.println(WiFi.localIP());
  Serial.println("/");
}

void loop() {
  server.handleClient();
}
