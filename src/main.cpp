#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

constexpr uint32_t SerialBaud = 115200;
constexpr const char *ApSsid = "IrrigationLogger";
constexpr uint8_t I2cSdaPin = 22;
constexpr uint8_t I2cSclPin = 21;
constexpr uint8_t Ads1115DefaultAddress = 0x48;
constexpr uint8_t OledDefaultAddress = 0x3C;
constexpr int OledWidth = 128;
constexpr int OledHeight = 64;
constexpr unsigned long AdcReadIntervalMs = 2000;
constexpr float DividerScale = 11.0f;
constexpr float SensorZeroPsiVoltage = 0.5f;
constexpr float SensorFullScaleVoltage = 4.5f;
constexpr float SensorFullScalePsi = 100.0f;

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
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Irrigation Logger");
  display.println();
  display.printf("ADS: %s\n", ads1115Detected ? "OK" : "NO");
  display.printf("A0: %.4f V\n", latestA0Voltage);
  display.printf("Sensor: %.3f V\n", latestSensorVoltage);
  display.printf("PSI: %.1f\n", latestPressurePsi);
  display.printf("AP: %s\n", WiFi.softAPIP().toString().c_str());
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
  const char page[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Irrigation Logger Status</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 2rem; line-height: 1.45; }
    main { max-width: 42rem; }
    code { background: #f0f3f5; padding: 0.15rem 0.3rem; border-radius: 3px; }
  </style>
</head>
<body>
  <main>
    <h1>Irrigation Logger</h1>
    <p>Status: booted and serving local Wi-Fi.</p>
    <p>AP SSID: <code>IrrigationLogger</code></p>
    <p>ADS1115 detection and A0 voltage are available in the JSON status endpoint.</p>
    <p><a href="/status">View JSON status</a></p>
  </main>
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
      "\"pressure_sensor\":\"connected_via_10k_1k_divider\"}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(SerialBaud);
  delay(100);
  Serial.println();
  Serial.println("Irrigation Logger minimal firmware starting");

  Wire.begin(I2cSdaPin, I2cSclPin);
  scanI2cBus();
  updateAdcReading();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ApSsid);
  beginDisplay();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
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
}
