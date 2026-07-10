#include "WebPortal.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "Config.h"

WebPortal::WebPortal(PressureSensor &sensor, Clock &clock, LogStorage &storage)
    : server_(80), sensor_(sensor), clock_(clock), storage_(storage) {}

void WebPortal::begin() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(Config::ApSsid);

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/download", HTTP_GET, [this]() { handleDownload(); });
  server_.begin();
}

void WebPortal::handleClient() {
  server_.handleClient();
}

void WebPortal::setLatestReading(const PressureReading &reading, const String &timestamp) {
  latestReading_ = reading;
  latestTimestamp_ = timestamp;
}

void WebPortal::handleRoot() {
  const char page[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Irrigation Pressure Logger</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 2rem; line-height: 1.4; }
    main { max-width: 42rem; }
    code, pre { background: #f3f3f3; padding: 0.15rem 0.3rem; }
    a { display: inline-block; margin-top: 1rem; }
  </style>
</head>
<body>
  <main>
    <h1>Irrigation Pressure Logger</h1>
    <p>Connect to the <code>IrrigationLogger</code> Wi-Fi network and open <code>http://192.168.4.1</code>.</p>
    <p>Status JSON is available at <a href="/status">/status</a>.</p>
    <a href="/download">Download pressure_log.csv</a>
  </main>
</body>
</html>
)HTML";

  server_.send_P(200, "text/html", page);
}

void WebPortal::handleStatus() {
  server_.send(200, "application/json", statusJson());
}

void WebPortal::handleDownload() {
  if (!storage_.exists(Config::LogPath)) {
    server_.send(404, "text/plain", "pressure_log.csv not found");
    return;
  }

  File file = storage_.openRead(Config::LogPath);
  if (!file) {
    server_.send(500, "text/plain", "failed to open pressure_log.csv");
    return;
  }

  server_.sendHeader("Content-Disposition", "attachment; filename=pressure_log.csv");
  server_.streamFile(file, "text/csv");
  file.close();
}

String WebPortal::statusJson() const {
  JsonDocument doc;
  doc["pressure_psi"] = latestReading_.valid ? latestReading_.psi : 0.0f;
  doc["voltage"] = latestReading_.valid ? latestReading_.voltage : 0.0f;
  doc["timestamp"] = latestTimestamp_;
  doc["pressure_valid"] = latestReading_.valid;
  doc["sd_status"] = storage_.isReady() ? "ok" : "unavailable";
  doc["rtc_status"] = clock_.isReady() ? "ok" : "unavailable";
  doc["adc_status"] = sensor_.isReady() ? "ok" : "unavailable";
  doc["ap_ssid"] = Config::ApSsid;
  doc["ap_ip"] = WiFi.softAPIP().toString();

  String json;
  serializeJson(doc, json);
  return json;
}
