# Irrigation Pressure Logger

ESP32 (PlatformIO + Arduino) firmware for an irrigation pressure logger. It reads a
0-80 PSI pressure transducer through an ADS1115 ADC, timestamps each reading with a
DS3231 real-time clock, logs it to a microSD card as CSV, shows it on an OLED, and
serves a live web dashboard over its own Wi-Fi access point.

## What it does

- Samples the pressure sensor every ~2 seconds via the ADS1115.
- Logs `timestamp,millis,pressure_psi,voltage` rows to `/pressure_log.csv` on microSD,
  at a web-adjustable interval (saved to flash) while the display stays responsive.
- Keeps real wall-clock time with the DS3231 (battery-backed).
- Hosts a Wi-Fi AP `IrrigationLogger` and serves a dashboard at `http://192.168.4.1`.
- Shows live pressure, connection info, and log status on the SSD1306 OLED.

## Web endpoints (AP `IrrigationLogger`, `http://192.168.4.1`)

| Endpoint | Purpose |
| --- | --- |
| `/` | Live dashboard (pressure gauge, chip-status pills, clock sync, links) |
| `/status` | JSON: pressure, voltages, chip status, logged rows, RTC timestamp |
| `/download` | Download `pressure_log.csv` |
| `/settime?epoch=<local-unix>` | Set the RTC (used by the dashboard "Sync clock" button) |
| `/resetlog` | Erase the CSV and start a fresh file with the header |
| `/setinterval?ms=<n>` | Set how often a row is logged (persisted to flash) |

## Hardware

- **Board:** ESP32 DevKit V1, PlatformIO board `esp32dev`
- **Sensor:** 0-80 PSI transducer, 0.5-4.5 V output (green = signal, red = 5 V, black = GND)
- **ADC:** ADS1115 over I2C (`0x48`)
- **RTC:** DS3231 / HW-084 over I2C (`0x68`; onboard EEPROM at `0x57`)
- **Storage:** HW-125 microSD module over SPI
- **Display:** SSD1306 OLED over I2C (`0x3C`)

### Wiring

| Signal | ESP32 pin |
| --- | --- |
| I2C SDA (ADS1115, DS3231, OLED) | GPIO 22 |
| I2C SCL (ADS1115, DS3231, OLED) | GPIO 21 |
| microSD CS / SCK / MISO / MOSI | GPIO 5 / 18 / 23 / 19 |
| Sensor signal → 10k → ADS1115 A0, A0 → 1k → GND | divider scale 11.0 |

I2C modules (ADS1115, OLED, DS3231) are powered from **3V3**; the sensor and the HW-125
microSD are powered from **VIN (5 V)**.

Full wiring references:
- **`wiring-guide.html`** — pin-by-pin tables for every device, with a verification checklist.
- **`wiring-diagram.html`** — color-coded visual of the board and all connections.
- **`TODO.md`** — confirmed wiring, observed readings, and calibration notes.

## Calibration

The 0-80 PSI sensor idles at ~0.437 V (below the nominal 0.5 V), so zero is anchored to
that measured value; the slope is the nominal 20 PSI/V, confirmed against a ~32 PSI
reference. Constants live at the top of `src/main.cpp`.

## Build layout

`platformio.ini` builds **only `src/main.cpp`** (`build_src_filter = +<main.cpp>`); it is
self-contained. The other files under `src/`/`include/` are unbuilt module stubs for a
future refactor.

## Development commands

```sh
pio run                        # Build
pio run -t upload              # Build + flash over USB
pio device monitor -b 115200   # Serial monitor
```
