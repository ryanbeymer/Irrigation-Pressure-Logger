# Irrigation Pressure Logger

ESP32-S3 (PlatformIO + Arduino) firmware for an irrigation pressure logger, running on
a LilyGo T-SIM7080G-S3 board. It reads a 0-80 PSI pressure transducer through an
ADS1115 ADC, logs it to the board's onboard microSD card as CSV, shows it on an OLED,
and serves a live web dashboard — including over-the-air firmware updates — over its
own Wi-Fi access point.

## What it does

- Samples the pressure sensor every ~2 seconds via the ADS1115.
- Logs `timestamp,millis,pressure_psi,voltage` rows to `/pressure_log.csv` on the
  onboard microSD, at a web-adjustable interval (saved to flash) while the display
  stays responsive.
- Keeps wall-clock time via a software clock synced from the browser (`/settime`) —
  this board has no hardware RTC, so it resets to ms-since-boot until re-synced.
- Hosts a Wi-Fi AP named `IrrigationLogger-<last 6 hex chars of its MAC>` (unique per
  board) and serves a dashboard at `http://192.168.4.1`.
- Shows live pressure, connection info, and log status on the SSD1306 OLED.
- Updates itself over the air: upload a `.bin` through the dashboard's "Firmware
  Update" card, no USB/PlatformIO required.

## Web endpoints (AP `IrrigationLogger-<MAC suffix>`, `http://192.168.4.1`)

| Endpoint | Purpose |
| --- | --- |
| `/` | Live dashboard (pressure gauge, chip-status pills, clock sync, firmware update, links) |
| `/status` | JSON: pressure, voltages, chip status, logged rows, clock/firmware-version status |
| `/download` | Download `pressure_log.csv` |
| `/settime?epoch=<local-unix>` | Set the software clock (used by the dashboard "Sync clock" button) |
| `/resetlog` | Erase the CSV and start a fresh file with the header |
| `/setinterval?ms=<n>` | Set how often a row is logged (persisted to flash) |
| `/update` (POST) | OTA firmware upload — dashboard's "Firmware Update" card |

## Hardware

- **Board:** LilyGo T-SIM7080G-S3 (ESP32-S3, onboard AXP2101 PMU, onboard microSD
  slot, SIM7080G modem — modem/PMU unused by this firmware so far), built via
  PlatformIO board `esp32-s3-devkitc-1` (closest generic match)
- **Sensor:** 0-80 PSI transducer, 0.5-4.5 V output (green = signal, red = 5 V, black = GND)
- **ADC:** ADS1115 over I2C (`0x48`)
- **Storage:** onboard microSD slot, SDMMC 1-bit mode (not SPI, no external module)
- **Display:** SSD1306 OLED over I2C (`0x3C`)

### Wiring

| Signal | ESP32-S3 pin |
| --- | --- |
| I2C SDA (ADS1115, OLED — shares the bus with the onboard PMU) | GPIO 15 |
| I2C SCL (ADS1115, OLED) | GPIO 7 |
| microSD CLK / CMD / DATA (onboard, SDMMC) | GPIO 38 / 39 / 40 |
| Sensor signal → 10k → ADS1115 A0, A0 → 1k → GND | divider scale 11.0 |

I2C modules (ADS1115, OLED) are powered from **3V3**.

Full wiring references:
- **`wiring-guide.html`** — pin-by-pin tables (predates the LilyGo port; check `TODO.md` first for current pins).
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

## Versioning & releases

Every build embeds a version string from `git describe --tags --always --dirty`
(visible in the boot log, `/status`, and the dashboard footer) — see
`scripts/get_firmware_version.py`.

To ship a release (which also produces a `.bin` ready for the OTA update page,
without needing a local PlatformIO build):

```sh
git tag -a vX.Y.Z -m "short summary of what changed"
git push origin vX.Y.Z
```

Pushing a `v*` tag triggers `.github/workflows/release.yml`, which builds the
firmware in CI and publishes a [GitHub Release](../../releases) with
`irrigation-pressure-logger-vX.Y.Z.bin` attached. A manual run from the Actions tab
(no tag needed) instead uploads the `.bin` as a plain build artifact — useful for a
one-off test build.
