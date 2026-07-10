# Irrigation Pressure Logger

ESP32 PlatformIO firmware for an irrigation pressure logger. The current checked-in milestone is intentionally minimal: it boots, starts Serial, creates a local Wi-Fi access point, and serves a status page with hardware marked as stubbed.

## Current Firmware Milestone

- Starts Serial at 115200 baud.
- Starts a Wi-Fi access point named `IrrigationLogger`.
- Serves a local page at `http://192.168.4.1`.
- Exposes `/status` with JSON showing AP information and `"hardware":"stubbed"`.
- Does not initialize ADS1115, DS3231, SD, or the pressure sensor yet.

This lets the ESP32, USB upload path, Serial monitor, Wi-Fi AP, and local web page be tested before wiring sensors.

## Planned Hardware

- Board: ESP32 DevKit, PlatformIO board `esp32dev`
- Pressure transducer: 0-100 PSI, 0.5-4.5 V analog output
- ADC: ADS1115 over I2C
- RTC: DS3231 over I2C
- Storage: microSD module over SPI
- Optional display: SSD1306 OLED over I2C, not used in the first firmware milestone
- Power: 5 V USB

## Planned Wiring

These pins are reserved in `include/Config.h` for the hardware-enabled firmware milestone.

| Device | ESP32 Pin |
| --- | --- |
| I2C SDA for ADS1115, DS3231, optional OLED | GPIO 21 |
| I2C SCL for ADS1115, DS3231, optional OLED | GPIO 22 |
| microSD CS | GPIO 5 |
| microSD SCK | GPIO 18 |
| microSD MISO | GPIO 19 |
| microSD MOSI | GPIO 23 |
| ADS1115 A0 | Pressure transducer analog output |

The pressure transducer calibration is:

- 0.5 V = 0 PSI
- 4.5 V = 100 PSI

## Planned Full Logger Behavior

- Initializes ADS1115, DS3231, and SD card.
- Reads pressure every 5 minutes.
- Appends CSV rows to `/pressure_log.csv`:

```csv
timestamp,pressure_psi,voltage
```

- Exposes `/download` to download the CSV log.
- Exposes `/status` with JSON containing current pressure, voltage, timestamp, SD status, RTC status, and ADC status.

The stub modules for that fuller version are still in the repo, but `platformio.ini` currently builds only `src/main.cpp`.

## Development Commands

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```
