#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <math.h>

// ====== WIFI ======
const char* ssid     = "";
const char* password = "";

// ====== SENSOR / CALIB ======
const float SEA_LEVEL_HPA = 1038;   // QNH (hPa) korkeuden laskentaan

// ====== GLOBALS ======
ESP8266WebServer server(80);
Adafruit_BMP085 bmp;

volatile int cpuLoad = 0;

// ====== FLIGHT / LOG STATE ======
enum FlightState : uint8_t { IDLE=0, ARMED=1, LOGGING=2 };
volatile FlightState flightState = IDLE;

uint32_t sessionId = 0;
uint32_t sessionStartMs = 0;      // millis() at session start
uint32_t sessionStartEpoch = 0;   // epoch seconds at session start (if NTP ok, else 0)

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
#include <time.h>

static void setupTimeNtp() {
  // NTP: käytä UTC-aikaa, halutessa lisää TZ myöhemmin
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  if (t < 1700000000) return 0; // karkea raja: jos ei ole synkassa, palauta 0
  return (uint32_t)t;
}

static void formatIso8601Z(uint32_t epoch, char* out, size_t outsz) {
  // "YYYY-MM-DDTHH:MM:SSZ"
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

// Arvio CPU-kuormasta loopin sykli-ajasta 
void updateCpuLoad() {
  static unsigned long last = micros();
  unsigned long now = micros();
  unsigned long dt = now - last;
  last = now;

  // Skaalaus: säädä tarvittaessa
  int load = (int)map((long)dt, 2000, 20000, 80, 5);
  if (load < 0) load = 0;
  if (load > 100) load = 100;

  // Kevyt suodatus (ettei heilu liikaa)
  cpuLoad = (cpuLoad * 7 + load * 3) / 10;
}

void resetLogSession() {
  noInterrupts();
  logHead = 0;
  logCount = 0;
  interrupts();

  sessionId++;
  sessionStartMs = millis();
  sessionStartEpoch = nowEpoch(); // 0 jos NTP ei ole vielä synkassa
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

  // Aikaleima: jos sessionStartEpoch tiedossa, lasketaan siitä (voi alkaa kesken session, jos NTP synkkaa myöhässä).
  if (sessionStartEpoch != 0) {
    s.ts_epoch = sessionStartEpoch + (s.t_ms / 1000);
  } else {
    s.ts_epoch = nowEpoch(); // 0 jos ei tiedossa
  }

  s.tempC = bmp.readTemperature();
  s.pressureHpa = bmp.readPressure() / 100.0f;
  s.altitudeM = bmp.readAltitude(SEA_LEVEL_HPA * 100.0f);
  s.rssi = (WiFi.status() == WL_CONNECTED) ? (int16_t)WiFi.RSSI() : (int16_t)-999;
  s.cpu = (uint8_t)cpuLoad;

  noInterrupts();
  logBuf[logHead] = s;
  logHead = (logHead + 1) % LOG_CAP;
  if (logCount < LOG_CAP) logCount++;
  interrupts();
}

String mainPage() {
  return R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GROUND STATION</title>
<style>
:root{--bg:#05070a;--fg:#d7ffe8;--dim:#7fffd480;--acc:#00ffd0;}
*{box-sizing:border-box}
body{margin:0;background:radial-gradient(1200px 700px at 70% 10%, #0b1a22 0%, var(--bg) 55%);color:var(--fg);
font:14px/1.2 ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;}
.wrap{max-width:1050px;margin:auto;padding:18px}
h1{margin:0 0 12px;color:var(--acc);letter-spacing:.22em;font-size:18px}
.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}
.panel{grid-column:span 12;border:1px solid #00ffd033;border-radius:14px;padding:14px 16px;background:linear-gradient(180deg,#071018,#05070a);
box-shadow:0 0 0 1px #00ffd012 inset, 0 18px 50px #00000090;position:relative;overflow:hidden;}
.panel:before{content:"";position:absolute;inset:0;background:linear-gradient(transparent 0,#00ffd008 50%,transparent 100%);
background-size:100% 10px;opacity:.35;mix-blend-mode:screen;pointer-events:none;}
.lbl{color:var(--dim);letter-spacing:.08em}
.val{margin-top:6px;font-size:26px;font-variant-numeric:tabular-nums}
.small{margin-top:8px;color:#7fffd455;font-size:12px;font-variant-numeric:tabular-nums}
.bar{margin-top:10px;height:10px;border-radius:999px;background:#02151d;border:1px solid #00ffd01f;overflow:hidden}
.bar i{display:block;height:100%;background:linear-gradient(90deg,#00ffd0,#7cffb5);width:0%}
button.ctrl{
  margin-top:10px;padding:12px 16px;border-radius:12px;
  border:1px solid #00ffd04a;background:#031821;color:var(--fg);
  font:14px ui-monospace,monospace;letter-spacing:.12em;cursor:pointer;
}
button.ctrl.stop{
  border-color:#ff4d4d55;
  background:#1b0606;
}
button.ctrl:disabled{opacity:.55;cursor:not-allowed}
@media(min-width:900px){
  .span4{grid-column:span 4}
  .span6{grid-column:span 6}
}
</style>
</head>
<body>
<div class="wrap">
  <h1>GROUND STATION / TELEMETRY LINK</h1>

  <div class="grid">

    <div class="panel span12">
      <div class="lbl">FLIGHT CONTROL</div>
      <div class="val" id="state">IDLE</div>

      <button class="ctrl" id="armBtn">ARM / START LOG</button>
      <button class="ctrl stop" id="stopBtn">STOP LOG</button>

      <div class="small" id="sess">SESSION --</div>
      <div class="small" id="t0">T0 --</div>
    </div>

    <div class="panel span4">
      <div class="lbl">TEMPERATURE</div>
      <div class="val" id="temp">--</div>
      <div class="bar"><i id="tbar"></i></div>
    </div>

    <div class="panel span4">
      <div class="lbl">PRESSURE</div>
      <div class="val" id="pressure">--</div>
      <div class="bar"><i id="pbar"></i></div>
    </div>

    <div class="panel span4">
      <div class="lbl">ALTITUDE (QNH)</div>
      <div class="val" id="alt">--</div>
      <div class="small" id="qnh">--</div>
    </div>

    <div class="panel span6">
      <div class="lbl">RF LINK (WIFI)</div>
      <div class="val" id="rssi">--</div>
      <div class="bar"><i id="rbar"></i></div>
      <div class="small" id="ip">--</div>
    </div>

    <div class="panel span6">
      <div class="lbl">LOAD</div>
      <div class="val" id="cpu">--</div>
      <div class="bar"><i id="cbar"></i></div>
      <div class="small" id="loginfo">--</div>
    </div>

  </div>
</div>

<script>
const T_MIN=-10, T_MAX=50;
const P_MIN=950, P_MAX=1050;

// RSSI:  -30 dBm (erinomainen) ... -90 dBm (heikko)
const R_BEST = -30;
const R_WORST = -90;

function clamp(x,a,b){return Math.max(a,Math.min(b,x));}

function armBtn(){ return document.getElementById('armBtn'); }
function stopBtn(){ return document.getElementById('stopBtn'); }

async function armStart(){
  try{
    armBtn().disabled = true;
    const r = await fetch('/arm', {method:'POST', cache:'no-store'});
    const d = await r.json();
    document.getElementById('state').textContent = d.state;
    document.getElementById('sess').textContent  = 'SESSION ' + d.session;
  }catch(e){
  }
}

async function stopLog(){
  try{
    stopBtn().disabled = true;
    const r = await fetch('/stop', {method:'POST', cache:'no-store'});
    const d = await r.json();
    document.getElementById('state').textContent = d.state;
  }catch(e){
  }
}

window.addEventListener('load', ()=>{
  armBtn().addEventListener('click', armStart);
  stopBtn().addEventListener('click', stopLog);
});

async function update(){
  const r = await fetch('/api', {cache:'no-store'});
  const d = await r.json();

  document.getElementById('temp').textContent = d.temp.toFixed(2)+' C';
  document.getElementById('pressure').textContent = d.pressure.toFixed(1)+' hPa';
  document.getElementById('alt').textContent = d.altitude.toFixed(1)+' m';
  document.getElementById('qnh').textContent = 'QNH ' + d.qnh.toFixed(1) + ' hPa';

  document.getElementById('rssi').textContent = d.rssi + ' dBm';
  document.getElementById('ip').textContent = 'IP ' + d.ip;

  document.getElementById('cpu').textContent = 'CPU LOAD ' + d.cpu + ' %';

  document.getElementById('state').textContent = d.state;
  document.getElementById('sess').textContent = 'SESSION ' + d.session + ' / samples ' + d.log_count;
  document.getElementById('loginfo').textContent = 'RAM LOG ' + d.log_count + ' / ' + d.log_cap;
  document.getElementById('t0').textContent = 'T0 ' + d.t0;

  if(d.state === 'LOGGING'){
    armBtn().textContent = 'LOGGING...';
    armBtn().disabled = true;
    stopBtn().disabled = false;
  }else{
    armBtn().textContent = 'ARM / START LOG';
    armBtn().disabled = false;
    stopBtn().disabled = true;
  }

  // Palkit
  let tp = clamp((d.temp-T_MIN)/(T_MAX-T_MIN),0,1)*100;
  let pp = clamp((d.pressure-P_MIN)/(P_MAX-P_MIN),0,1)*100;

  // RSSI palkki: -90 -> 0%, -30 -> 100%
  let rp = clamp((d.rssi - R_WORST) / (R_BEST - R_WORST), 0, 1) * 100;

  document.getElementById('tbar').style.width = tp+'%';
  document.getElementById('pbar').style.width = pp+'%';
  document.getElementById('rbar').style.width = rp+'%';
  document.getElementById('cbar').style.width = clamp(d.cpu,0,100)+'%';
}

// 0.5s paivitys
setInterval(update, 500);
update();
</script>
</body>
</html>
)rawliteral";
}

void handleRoot() {
  server.send(200, "text/html", mainPage());
}

void handleArm() {
  // 1) RAM-logi nollataan
  // 2) flight state -> ARMED
  // 3) logging alkaa
  resetLogSession();
  flightState = ARMED;
  flightState = LOGGING;

  String json;
  json.reserve(120);
  json += "{";
  json += "\"ok\":true,";
  json += "\"state\":\"LOGGING\",";
  json += "\"session\":" + String(sessionId) + ",";
  json += "\"t0_epoch\":" + String(sessionStartEpoch);
  json += "}";

  server.send(200, "application/json", json);
}

void handleStop() {
  // Stop: logging pois, mutta sessio + RAM-data jää talteen ladattavaksi
  flightState = IDLE;

  String json;
  json.reserve(140);
  json += "{";
  json += "\"ok\":true,";
  json += "\"state\":\"IDLE\",";
  json += "\"session\":" + String(sessionId) + ",";
  json += "\"log_count\":" + String((int)logCount);
  json += "}";

  server.send(200, "application/json", json);
}

void handleApi() {
  float tempC = bmp.readTemperature();
  float pressureHpa = bmp.readPressure() / 100.0f;
  float altitudeM = bmp.readAltitude(SEA_LEVEL_HPA * 100.0f);

  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -999;

  IPAddress ip = WiFi.localIP();
  char ipbuf[32];
  snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

  const char* st = (flightState == LOGGING) ? "LOGGING" : (flightState == ARMED) ? "ARMED" : "IDLE";

  // ISO-8601 T0
  char t0buf[32];
  formatIso8601Z(sessionStartEpoch, t0buf, sizeof(t0buf));

  String json;
  json.reserve(420);
  json += "{";
  json += "\"temp\":" + String(tempC, 2) + ",";
  json += "\"pressure\":" + String(pressureHpa, 1) + ",";
  json += "\"altitude\":" + String(altitudeM, 1) + ",";
  json += "\"qnh\":" + String(SEA_LEVEL_HPA, 1) + ",";
  json += "\"rssi\":" + String(rssi) + ",";
  json += "\"ip\":\"" + String(ipbuf) + "\",";
  json += "\"cpu\":" + String(cpuLoad) + ",";
  json += "\"state\":\"" + String(st) + "\",";
  json += "\"session\":" + String(sessionId) + ",";
  json += "\"log_count\":" + String((int)logCount) + ",";
  json += "\"log_cap\":" + String((int)LOG_CAP) + ",";
  json += "\"t0\":\"" + String(t0buf) + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

// --- CSV helper: calculate exact row length using snprintf ---
static size_t csvRowLen(uint32_t sid, const LogSample& s) {
  char line[110];
  // session,ts_epoch,t_ms,tempC,pressure_hPa,alt_m,rssi_dBm,cpu
  int n = snprintf(line, sizeof(line),
    "%lu,%lu,%lu,%.2f,%.1f,%.1f,%d,%u\n",
    (unsigned long)sid,
    (unsigned long)s.ts_epoch,
    (unsigned long)s.t_ms,
    s.tempC,
    s.pressureHpa,
    s.altitudeM,
    (int)s.rssi,
    (unsigned int)s.cpu
  );
  if (n < 0) return 0;
  return (size_t)n;
}

void handleLogCsv() {
  // Ei chunked -> wget toimii luotettavasti
  uint16_t count;
  uint16_t head;
  uint32_t sid;

  noInterrupts();
  count = logCount;
  head  = logHead;
  sid   = sessionId;
  interrupts();

  const char* header = "session,ts_epoch,t_ms,tempC,pressure_hPa,alt_m,rssi_dBm,cpu\n";
  uint16_t start = (count < LOG_CAP) ? 0 : head;

  // PASS 1: laske Content-Length
  size_t total = strlen(header);
  for (uint16_t i = 0; i < count; i++) {
    uint16_t idx = (start + i) % LOG_CAP;

    LogSample s;
    noInterrupts();
    s = logBuf[idx];
    interrupts();

    total += csvRowLen(sid, s);
    yield();
  }

  server.sendHeader("Content-Type", "text/csv");
  server.sendHeader("Content-Disposition", "attachment; filename=\"session.csv\"");
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Connection", "close");
  server.setContentLength(total);
  server.send(200);

  WiFiClient client = server.client();
  client.print(header);

  // PASS 2: lähetä data
  char line[110];
  for (uint16_t i = 0; i < count; i++) {
    uint16_t idx = (start + i) % LOG_CAP;

    LogSample s;
    noInterrupts();
    s = logBuf[idx];
    interrupts();

    int n = snprintf(line, sizeof(line),
      "%lu,%lu,%lu,%.2f,%.1f,%.1f,%d,%u\n",
      (unsigned long)sid,
      (unsigned long)s.ts_epoch,
      (unsigned long)s.t_ms,
      s.tempC,
      s.pressureHpa,
      s.altitudeM,
      (int)s.rssi,
      (unsigned int)s.cpu
    );
    if (n > 0) client.write((const uint8_t*)line, (size_t)n);
    yield();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  // I2C: SDA=D2(GPIO4), SCL=D1(GPIO5)
  Wire.begin(4, 5);

  Serial.print("BMP180 init... ");
  if (!bmp.begin()) {
    Serial.println("EPAONNISTUI");
    while (true) delay(1000);
  }
  Serial.println("OK");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Yhdistetaan WiFiin");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // NTP time
  setupTimeNtp();

  server.on("/", handleRoot);
  server.on("/api", HTTP_GET, handleApi);
  server.on("/arm", HTTP_POST, handleArm);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/log.csv", HTTP_GET, handleLogCsv);

  server.begin();
  Serial.println("Web-palvelin kaynnissa");
}

void loop() {
  updateCpuLoad();
  server.handleClient();
  maybeLogSample();
}
