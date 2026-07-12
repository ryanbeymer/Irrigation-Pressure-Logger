#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

constexpr uint32_t SerialBaud = 115200;
constexpr const char *ApSsid = "IrrigationLogger";
constexpr uint8_t I2cSdaPin = 22;
constexpr uint8_t I2cSclPin = 21;
constexpr uint8_t Ads1115DefaultAddress = 0x48;
constexpr uint8_t OledDefaultAddress = 0x3C;
constexpr uint8_t Ds3231DefaultAddress = 0x68;
constexpr int OledWidth = 128;
constexpr int OledHeight = 64;
constexpr unsigned long AdcReadIntervalMs = 2000;
constexpr unsigned long DefaultLogIntervalMs = 60000;  // 1 minute
constexpr unsigned long MinLogIntervalMs = 2000;       // floor at the sample rate
constexpr float DividerScale = 11.0f;
// Calibration for the 0-80 PSI, 0.5-4.5 V sensor (green/black/red). Zero is
// anchored to the measured atmospheric output (0.437 V, vs nominal 0.5 V); the
// nominal 4.0 V span (20 PSI/V) was confirmed by a ~32 PSI reference point.
constexpr float SensorZeroPsiVoltage = 0.437f;
constexpr float SensorFullScaleVoltage = 4.437f;
constexpr float SensorFullScalePsi = 80.0f;

// microSD over the ESP32 default VSPI bus (SCK 18, MISO 19, MOSI 23). Only CS
// needs to be named; SD.begin(CS) uses those default SPI pins.
constexpr uint8_t SdChipSelectPin = 5;
constexpr const char *LogPath = "/pressure_log.csv";
constexpr const char *CsvHeader = "timestamp,millis,pressure_psi,voltage";

WebServer server(80);
Adafruit_SSD1306 display(OledWidth, OledHeight, &Wire, -1);
String i2cDeviceList = "none";
bool ads1115Detected = false;
bool oledDetected = false;
bool oledReady = false;
float latestA0Voltage = 0.0f;
float latestSensorVoltage = 0.0f;
float latestPressurePsi = 0.0f;
bool latestA0Valid = false;
unsigned long lastAdcReadMs = 0;
bool sdReady = false;
unsigned long loggedRowCount = 0;
RTC_DS3231 rtc;
bool rtcReady = false;
Preferences prefs;
unsigned long logIntervalMs = DefaultLogIntervalMs;
unsigned long lastLogMs = 0;

void scanI2cBus() {
  i2cDeviceList = "";
  ads1115Detected = false;

  Serial.println("Scanning I2C bus...");

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();

    if (error == 0) {
      char addressText[8];
      snprintf(addressText, sizeof(addressText), "0x%02X", address);

      if (i2cDeviceList.length() > 0) {
        i2cDeviceList += ",";
      }
      i2cDeviceList += addressText;

      Serial.printf("I2C device found at %s\n", addressText);

      if (address == Ads1115DefaultAddress) {
        ads1115Detected = true;
      }
      if (address == OledDefaultAddress) {
        oledDetected = true;
      }
    }
  }

  if (i2cDeviceList.length() == 0) {
    i2cDeviceList = "none";
    Serial.println("No I2C devices found");
  }

  Serial.printf("ADS1115 at 0x48: %s\n", ads1115Detected ? "detected" : "not detected");
  Serial.printf("OLED at 0x3C: %s\n", oledDetected ? "detected" : "not detected");
}

void updateDisplay() {
  if (!oledReady) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Pressure is the headline value, rendered large at the top.
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.printf("%.1f PSI", latestPressurePsi);

  // Connection info and logging status. Raw diagnostics (Sensor ADC status,
  // A0 and sensor volts) live on the web page now.
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.printf("AP: %s", ApSsid);
  display.setCursor(0, 30);
  display.printf(" %s", WiFi.softAPIP().toString().c_str());
  // Log sits with the timestamp at the bottom, a gap above it.
  display.setCursor(0, 48);
  display.printf("Log: %s (%lu)", sdReady ? "OK" : "NO", loggedRowCount);

  display.setCursor(0, 56);
  if (rtcReady) {
    const DateTime now = rtc.now();
    display.printf("%04d-%02d-%02d %02d:%02d",
                   now.year(), now.month(), now.day(), now.hour(), now.minute());
  } else {
    display.print("Clock: not set");
  }

  display.display();
}

void beginDisplay() {
  if (!oledDetected) {
    Serial.println("OLED display not detected; skipping display init");
    return;
  }

  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OledDefaultAddress);
  if (!oledReady) {
    Serial.println("OLED display init failed");
    return;
  }

  Serial.println("OLED display initialized");
  updateDisplay();
}

bool readAds1115A0(float &voltage) {
  if (!ads1115Detected) {
    return false;
  }

  // Single-shot conversion: A0 to GND, +/-4.096 V range, 128 samples/sec.
  constexpr uint16_t config = 0xC383;

  Wire.beginTransmission(Ads1115DefaultAddress);
  Wire.write(0x01);
  Wire.write(config >> 8);
  Wire.write(config & 0xFF);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(10);

  Wire.beginTransmission(Ads1115DefaultAddress);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  if (Wire.requestFrom(Ads1115DefaultAddress, static_cast<uint8_t>(2)) != 2) {
    return false;
  }

  const int16_t raw = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
  voltage = raw * 0.000125f;
  return true;
}

String rtcTimestamp() {
  if (!rtcReady) {
    // Fall back to ms-since-boot so a missing RTC still yields a value.
    return String(millis());
  }

  const DateTime now = rtc.now();
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return String(buffer);
}

void beginRtc() {
  if (!rtc.begin()) {
    Serial.println("DS3231 not detected on I2C");
    rtcReady = false;
    return;
  }

  rtcReady = true;

  // The DS3231 keeps time on its coin cell. Only (re)set the clock when it has
  // actually lost power, using the firmware build time as the seed.
  if (rtc.lostPower()) {
    Serial.println("DS3231 lost power; setting time from build clock");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Serial.printf("DS3231 initialized; time is %s\n", rtcTimestamp().c_str());
}

void beginSd() {
  sdReady = SD.begin(SdChipSelectPin);
  if (!sdReady) {
    Serial.println("microSD init failed (card missing or wiring issue)");
    return;
  }

  Serial.println("microSD initialized");

  // Write the CSV header once, when the log file does not yet exist.
  if (!SD.exists(LogPath)) {
    File file = SD.open(LogPath, FILE_WRITE);
    if (file) {
      file.println(CsvHeader);
      file.close();
      Serial.printf("Created log file %s\n", LogPath);
    } else {
      Serial.printf("Failed to create log file %s\n", LogPath);
    }
  }
}

void appendLogRow() {
  if (!sdReady || !latestA0Valid) {
    return;
  }

  File file = SD.open(LogPath, FILE_APPEND);
  if (!file) {
    Serial.println("microSD append failed to open log file");
    return;
  }

  // millis() is logged alongside the real timestamp: it resets to ~0 on every
  // boot, so a drop in this column marks a device restart.
  const String timestamp = rtcTimestamp();
  file.printf("%s,%lu,%.1f,%.4f\n", timestamp.c_str(), millis(),
              latestPressurePsi, latestSensorVoltage);
  file.close();
  loggedRowCount++;
}

void updateAdcReading() {
  float voltage = 0.0f;
  latestA0Valid = readAds1115A0(voltage);

  if (latestA0Valid) {
    latestA0Voltage = voltage;
    latestSensorVoltage = latestA0Voltage * DividerScale;

    latestPressurePsi =
        (latestSensorVoltage - SensorZeroPsiVoltage) *
        (SensorFullScalePsi / (SensorFullScaleVoltage - SensorZeroPsiVoltage));

    if (latestPressurePsi < 0.0f) {
      latestPressurePsi = 0.0f;
    }

    Serial.printf("ADS1115 A0: %.4f V | sensor: %.4f V | pressure: %.1f PSI\n",
                  latestA0Voltage,
                  latestSensorVoltage,
                  latestPressurePsi);
  } else {
    Serial.println("ADS1115 A0: read failed");
  }
}

void handleRoot() {
  // 'static' keeps this ~6 KB page in flash. Without it the array is built on
  // the loop task's stack, overflowing it and tripping the stack canary.
  static const char page[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Irrigation Logger</title>
<style>
  :root{
    --bg:#0e1517; --card:#16211f; --card2:#1b2825; --line:#26332f;
    --ink:#e9f1ef; --muted:#8fa4a0; --accent:#2fb6a3;
    --good:#46c06e; --bad:#e5565d;
    --mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  }
  *{box-sizing:border-box;}
  body{margin:0;background:var(--bg);color:var(--ink);line-height:1.5;
    font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-font-smoothing:antialiased;}
  .wrap{max-width:34rem;margin:0 auto;padding:1.25rem 1rem 3rem;}
  header{display:flex;align-items:center;justify-content:space-between;margin-bottom:1.25rem;}
  header h1{font-size:1.15rem;margin:0;}
  .conn{display:flex;align-items:center;gap:.4rem;font-size:.8rem;color:var(--muted);}
  .conn .dot{width:.6rem;height:.6rem;border-radius:50%;background:var(--bad);}
  .conn.live .dot{background:var(--good);}
  .card{background:var(--card);border:1px solid var(--line);border-radius:14px;
    padding:1.1rem 1.15rem;margin-bottom:1rem;}
  .hero{text-align:center;padding:1.5rem 1.15rem;}
  .psi{font-family:var(--mono);font-size:3.4rem;font-weight:700;line-height:1;letter-spacing:-.02em;}
  .psi span{font-size:1.1rem;color:var(--muted);font-weight:600;margin-left:.35rem;}
  .bar{height:.55rem;background:var(--card2);border-radius:100px;overflow:hidden;margin:1.1rem 0 .3rem;}
  .bar>i{display:block;height:100%;width:0;border-radius:100px;
    background:linear-gradient(90deg,var(--accent),#54d1c0);transition:width .4s ease;}
  .scale{display:flex;justify-content:space-between;font-size:.7rem;color:var(--muted);}
  .subs{display:flex;gap:.75rem;margin-top:1.25rem;}
  .subs>div{flex:1;background:var(--card2);border-radius:10px;padding:.6rem .7rem;}
  .subs .k{font-size:.68rem;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);}
  .subs .v{font-family:var(--mono);font-size:1.05rem;font-weight:600;margin-top:.15rem;}
  .pills{display:flex;flex-wrap:wrap;gap:.5rem;align-items:center;}
  .pill{display:inline-flex;align-items:center;gap:.4rem;font-size:.8rem;font-weight:600;
    padding:.35rem .7rem;border-radius:100px;background:var(--card2);border:1px solid var(--line);}
  .pill .dot{width:.55rem;height:.55rem;border-radius:50%;background:var(--muted);}
  .pill.on .dot{background:var(--good);} .pill.off .dot{background:var(--bad);}
  .rows{margin-left:auto;font-size:.8rem;color:var(--muted);}
  .rows b{color:var(--ink);font-family:var(--mono);}
  h2{font-size:.72rem;text-transform:uppercase;letter-spacing:.1em;color:var(--muted);margin:0 0 .8rem;}
  table.clocks{border-collapse:collapse;width:100%;}
  table.clocks td{padding:.35rem .2rem;}
  table.clocks .lbl{color:var(--muted);text-align:right;white-space:nowrap;padding-right:.9rem;}
  table.clocks .val{font-family:var(--mono);font-weight:700;font-size:1.1rem;}
  button{font:inherit;font-weight:600;color:#04211d;background:var(--accent);border:0;
    border-radius:9px;padding:.6rem 1rem;cursor:pointer;margin-top:.9rem;}
  button:active{transform:translateY(1px);}
  button.ghost{background:transparent;color:var(--muted);border:1px solid var(--line);}
  .msg{font-size:.8rem;color:var(--muted);margin-left:.6rem;}
  .loglbl{font-size:.85rem;color:var(--muted);display:block;margin-bottom:.4rem;}
  select{font:inherit;color:var(--ink);background:var(--card2);border:1px solid var(--line);
    border-radius:9px;padding:.55rem .7rem;}
  .links{display:flex;gap:.6rem;}
  .links a{flex:1;text-align:center;text-decoration:none;color:var(--ink);font-weight:600;
    background:var(--card2);border:1px solid var(--line);border-radius:10px;padding:.7rem;}
  .links a:active{background:var(--line);}
  footer{margin-top:1.5rem;text-align:center;font-size:.7rem;color:var(--muted);font-family:var(--mono);}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <h1>Irrigation Logger</h1>
    <span class="conn" id="conn"><span class="dot"></span><span id="conntxt">connecting</span></span>
  </header>

  <div class="card hero">
    <div class="psi"><span id="psi">--</span><span>PSI</span></div>
    <div class="bar"><i id="bar"></i></div>
    <div class="scale"><span>0</span><span>80 PSI</span></div>
    <div class="subs">
      <div><div class="k">Sensor</div><div class="v"><span id="sv">--</span> V</div></div>
      <div><div class="k">Raw A0</div><div class="v"><span id="a0">--</span> V</div></div>
    </div>
  </div>

  <div class="card">
    <div class="pills">
      <span class="pill" id="p-adc"><span class="dot"></span>Sensor ADC</span>
      <span class="pill" id="p-oled"><span class="dot"></span>OLED</span>
      <span class="pill" id="p-sd"><span class="dot"></span>Log</span>
      <span class="pill" id="p-rtc"><span class="dot"></span>Clock</span>
      <span class="rows">Rows: <b id="rows">--</b></span>
    </div>
  </div>

  <div class="card">
    <h2>Clock</h2>
    <table class="clocks">
      <tr><td class="lbl">This device</td><td class="val" id="devtime">&mdash;</td></tr>
      <tr><td class="lbl">Logger</td><td class="val" id="logtime">&mdash;</td></tr>
    </table>
    <button id="sync" type="button">Sync clock to this device</button>
    <span class="msg" id="syncmsg"></span>
  </div>

  <div class="card">
    <h2>Data</h2>
    <div class="links">
      <a href="/status">JSON</a>
      <a href="/download">CSV</a>
    </div>
    <button class="ghost" id="newlog" type="button">Start new log</button>
    <span class="msg" id="newlogmsg"></span>
  </div>

  <div class="card">
    <h2>Logging</h2>
    <label class="loglbl" for="loginterval">Save a reading to the log every</label>
    <select id="loginterval">
      <option value="2000">2 seconds</option>
      <option value="10000">10 seconds</option>
      <option value="30000">30 seconds</option>
      <option value="60000">1 minute</option>
      <option value="300000">5 minutes</option>
      <option value="900000">15 minutes</option>
    </select>
    <span class="msg" id="intervalmsg"></span>
  </div>

  <footer>IrrigationLogger &middot; 192.168.4.1</footer>
</div>
<script>
  function pad(n){return String(n).padStart(2,"0");}
  function fmt(d){return d.getFullYear()+"-"+pad(d.getMonth()+1)+"-"+pad(d.getDate())+" "+
    pad(d.getHours())+":"+pad(d.getMinutes())+":"+pad(d.getSeconds());}
  function setPill(id,on){document.getElementById(id).className="pill "+(on?"on":"off");}
  function tickDevice(){document.getElementById("devtime").textContent=fmt(new Date());}
  var intervalLoaded=false;

  function apply(j){
    document.getElementById("psi").textContent=Number(j.pressure_psi).toFixed(1);
    document.getElementById("sv").textContent=Number(j.sensor_voltage).toFixed(3);
    document.getElementById("a0").textContent=Number(j.ads1115_a0_voltage).toFixed(4);
    var p=Math.max(0,Math.min(100,Number(j.pressure_psi)/80*100));
    document.getElementById("bar").style.width=p+"%";
    setPill("p-adc",j.ads1115_detected);
    setPill("p-oled",j.oled_ready);
    setPill("p-sd",j.sd_ready);
    setPill("p-rtc",j.rtc_ready);
    document.getElementById("rows").textContent=j.logged_rows;
    document.getElementById("logtime").textContent=j.timestamp;
    // Sync the dropdown to the saved interval once, so we don't fight the user.
    if(!intervalLoaded && j.log_interval_ms){
      document.getElementById("loginterval").value=String(j.log_interval_ms);
      intervalLoaded=true;
    }
    document.getElementById("conn").className="conn live";
    document.getElementById("conntxt").textContent="live";
  }
  function refresh(){
    fetch("/status").then(function(r){return r.json();}).then(apply)
      .catch(function(){document.getElementById("conn").className="conn";
        document.getElementById("conntxt").textContent="offline";});
  }
  document.getElementById("sync").addEventListener("click",function(){
    var msg=document.getElementById("syncmsg");
    var epoch=Math.floor(Date.now()/1000)-new Date().getTimezoneOffset()*60;
    msg.textContent="Setting...";
    fetch("/settime?epoch="+epoch).then(function(r){return r.text();})
      .then(function(t){msg.textContent=t;refresh();})
      .catch(function(e){msg.textContent="Error: "+e;});
  });
  (function(){
    var btn=document.getElementById("newlog");
    var msg=document.getElementById("newlogmsg");
    var armed=false, timer=null;
    btn.addEventListener("click",function(){
      // Two-tap confirm (no confirm() dialog; those are unreliable on mobile).
      if(!armed){
        armed=true;
        btn.textContent="Tap again to erase";
        msg.textContent="";
        timer=setTimeout(function(){armed=false;btn.textContent="Start new log";},4000);
        return;
      }
      clearTimeout(timer);armed=false;btn.textContent="Start new log";
      msg.textContent="Working...";
      fetch("/resetlog").then(function(r){return r.text();})
        .then(function(t){msg.textContent=t;refresh();})
        .catch(function(e){msg.textContent="Error: "+e;});
    });
  })();
  document.getElementById("loginterval").addEventListener("change",function(e){
    var msg=document.getElementById("intervalmsg");msg.textContent="Saving...";
    fetch("/setinterval?ms="+e.target.value).then(function(r){return r.text();})
      .then(function(){msg.textContent="Saved";})
      .catch(function(err){msg.textContent="Error: "+err;});
  });
  tickDevice();setInterval(tickDevice,1000);
  refresh();setInterval(refresh,2000);
</script>
</body>
</html>
)HTML";
  server.send_P(200, "text/html", page);
}

void handleStatus() {
  const String json =
      String("{\"status\":\"ok\",") +
      "\"ap_ssid\":\"" + ApSsid + "\"," +
      "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"," +
      "\"i2c_sda\":" + String(I2cSdaPin) + "," +
      "\"i2c_scl\":" + String(I2cSclPin) + "," +
      "\"i2c_devices\":\"" + i2cDeviceList + "\"," +
      "\"ads1115_detected\":" + String(ads1115Detected ? "true" : "false") + "," +
      "\"oled_detected\":" + String(oledDetected ? "true" : "false") + "," +
      "\"oled_ready\":" + String(oledReady ? "true" : "false") + "," +
      "\"ads1115_a0_valid\":" + String(latestA0Valid ? "true" : "false") + "," +
      "\"ads1115_a0_voltage\":" + String(latestA0Voltage, 4) + "," +
      "\"sensor_voltage\":" + String(latestSensorVoltage, 4) + "," +
      "\"pressure_psi\":" + String(latestPressurePsi, 1) + "," +
      "\"sd_ready\":" + String(sdReady ? "true" : "false") + "," +
      "\"log_path\":\"" + LogPath + "\"," +
      "\"logged_rows\":" + String(loggedRowCount) + "," +
      "\"log_interval_ms\":" + String(logIntervalMs) + "," +
      "\"rtc_ready\":" + String(rtcReady ? "true" : "false") + "," +
      "\"timestamp\":\"" + rtcTimestamp() + "\"," +
      "\"pressure_sensor\":\"connected_via_10k_1k_divider\"}";
  server.send(200, "application/json", json);
}

void handleDownload() {
  if (!sdReady) {
    server.send(503, "text/plain", "microSD not ready");
    return;
  }

  File file = SD.open(LogPath, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "No log file yet");
    return;
  }

  server.sendHeader("Content-Disposition", "attachment; filename=pressure_log.csv");
  server.streamFile(file, "text/csv");
  file.close();
}

void handleSetTime() {
  if (!rtcReady) {
    server.send(503, "text/plain", "RTC not ready");
    return;
  }
  if (!server.hasArg("epoch")) {
    server.send(400, "text/plain", "missing epoch parameter");
    return;
  }

  // The browser sends local wall-clock time as a Unix-style epoch (already
  // shifted for the timezone), so the DS3231 stores local time directly.
  const uint32_t epoch = strtoul(server.arg("epoch").c_str(), nullptr, 10);
  rtc.adjust(DateTime(epoch));

  Serial.printf("Clock set via /settime to %s\n", rtcTimestamp().c_str());
  server.send(200, "text/plain", "Clock set to " + rtcTimestamp());
}

void handleResetLog() {
  if (!sdReady) {
    server.send(503, "text/plain", "microSD not ready");
    return;
  }

  SD.remove(LogPath);
  loggedRowCount = 0;

  File file = SD.open(LogPath, FILE_WRITE);
  if (!file) {
    server.send(500, "text/plain", "Failed to recreate log file");
    return;
  }
  file.println(CsvHeader);
  file.close();

  Serial.println("Log reset via /resetlog");
  server.send(200, "text/plain", "New log started");
}

void handleSetInterval() {
  if (!server.hasArg("ms")) {
    server.send(400, "text/plain", "missing ms parameter");
    return;
  }

  unsigned long ms = strtoul(server.arg("ms").c_str(), nullptr, 10);
  if (ms < MinLogIntervalMs) {
    ms = MinLogIntervalMs;
  }

  logIntervalMs = ms;
  prefs.putULong("logMs", ms);  // persists across reboots and reflashes
  lastLogMs = millis();

  Serial.printf("Log interval set via /setinterval to %lu ms\n", logIntervalMs);
  server.send(200, "text/plain", "Log interval set to " + String(logIntervalMs) + " ms");
}

void setup() {
  Serial.begin(SerialBaud);
  delay(100);
  Serial.println();
  Serial.println("Irrigation Logger minimal firmware starting");

  prefs.begin("logger", false);
  logIntervalMs = prefs.getULong("logMs", DefaultLogIntervalMs);
  if (logIntervalMs < MinLogIntervalMs) {
    logIntervalMs = MinLogIntervalMs;
  }
  Serial.printf("Log interval: %lu ms\n", logIntervalMs);

  Wire.begin(I2cSdaPin, I2cSclPin);
  scanI2cBus();
  beginRtc();
  beginSd();
  updateAdcReading();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ApSsid);
  beginDisplay();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/settime", HTTP_GET, handleSetTime);
  server.on("/resetlog", HTTP_GET, handleResetLog);
  server.on("/setinterval", HTTP_GET, handleSetInterval);
  server.begin();

  Serial.printf("Wi-Fi AP started: %s\n", ApSsid);
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("Open http://192.168.4.1");
}

void loop() {
  server.handleClient();

  const unsigned long now = millis();
  if (now - lastAdcReadMs >= AdcReadIntervalMs) {
    lastAdcReadMs = now;
    updateAdcReading();
    updateDisplay();
  }

  // Logging runs on its own (web-adjustable) interval, independent of the
  // faster sampling that keeps the display and dashboard responsive.
  if (now - lastLogMs >= logIntervalMs) {
    lastLogMs = now;
    appendLogRow();
  }
}
