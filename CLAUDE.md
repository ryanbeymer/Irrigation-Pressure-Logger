# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

ESP32-S3 firmware (PlatformIO + Arduino framework) for an irrigation pressure logger, running on a **LilyGo T-SIM7080G-S3** board. A 0-80 PSI / 0.5-4.5 V pressure transducer feeds an ADS1115 ADC over I2C; the board hosts a Wi-Fi access point serving a live dashboard. An SSD1306 OLED shows live readings, and the CSV log is written to the board's onboard microSD slot. There is no hardware RTC — timestamps come from a software clock synced from the browser via `/settime` (see "Two parallel realities" below).

## Commands

```sh
pio run                        # Build (compiles only src/main.cpp — see below)
pio run -t upload              # Build + flash over USB
pio device monitor -b 115200   # Serial monitor (see caveat below)
```

Board: `esp32-s3-devkitc-1` (generic S3 target; the LilyGo isn't in PlatformIO's board list, so pins are set explicitly in `main.cpp` rather than via board macros). No host-side test runner — validation is done on-device via the Serial monitor and the `/status` JSON endpoint.

**`pio device monitor` needs a real TTY** and will fail with a `termios` error if run from a non-interactive shell (e.g. a backgrounded/piped command). In that case read the port directly instead, e.g. with a short Python `pyserial` script or `stty <port> 115200 && cat <port>`.

## Critical: only main.cpp is compiled

`platformio.ini` sets `build_src_filter = +<main.cpp>`, so **only `src/main.cpp` is built.** The other files in `src/` and `include/` (`PressureSensor`, `Clock`, `LogStorage`, `WebPortal`) are design stubs for a future modular milestone and are **not compiled**. Editing them has no effect on the firmware until the build filter and `lib_deps` are changed.

`main.cpp` is deliberately self-contained: it re-declares its own constants, talks to the ADS1115 by writing I2C registers directly (not via a library), and drives the OLED. Treat it as the single source of truth for current behavior.

## Two parallel realities — do not conflate them

There are meaningful discrepancies between the working firmware (`main.cpp`, matches the wired hardware) and the stub modules / `Config.h` (still reflect an earlier ESP32 classic + external-module design). When changing anything, follow `main.cpp` + `TODO.md`, not the stubs:

| Concern | Working (`main.cpp`, `TODO.md`) | Stubs / `Config.h` |
| --- | --- | --- |
| Chip | ESP32-**S3** (LilyGo T-SIM7080G-S3) | Classic ESP32 assumptions |
| I2C SDA / SCL | GPIO **15 / 7** (board's shared I2C bus — also used by the onboard AXP2101 PMU) | GPIO 21 / 22 |
| ADC read | Raw I2C register writes, LSB `0.000125 V`, `±4.096V` range | Adafruit_ADS1X15 lib, `GAIN_TWOTHIRDS` |
| Voltage divider | Scale `11.0` (10k/1k divider on A0) | No divider |
| Sample interval | 2 s (`AdcReadIntervalMs`) | 5 min (`SampleIntervalMs`) |
| microSD | Onboard slot via **SD_MMC** (1-bit mode, pins 38/39/40) — **not** SPI, **not** an external HW-125 module | Implemented in stubs (SPI-based) |
| RTC | **None.** No hardware RTC on this board — `/settime` sets a software clock (epoch + `millis()` offset) that resets on reboot until re-synced | Implemented in stubs (assumes DS3231) |

Some ESP32-S3 GPIO numbers that matter if you touch pin assignments: **GPIO 22-25 don't exist** on the S3 (unlike classic ESP32), and **GPIO 19/20 are hardwired to the native USB D-/D+ lines** — reusing them for anything else breaks the USB serial connection. Both were hit while porting this firmware from the original classic-ESP32 DOIT DevKit board.

`lib_deps` currently lists only `Adafruit SSD1306` (`SD_MMC` and `Preferences` are built into the ESP32 Arduino core, no separate dependency). Activating the stub modules would additionally require `Adafruit ADS1X15` and `ArduinoJson`, plus updating `build_src_filter`.

## Pressure calculation

Calibrated for the 0-80 PSI sensor (zero anchored at 0.437 V idle, 20 PSI/V slope) — see `TODO.md` for the full calibration notes.

## HTTP endpoints (AP at `http://192.168.4.1`, SSID `IrrigationLogger-<last 6 MAC hex chars>`)

The SSID gets a per-board suffix (e.g. `IrrigationLogger-1B1FEC`) computed from the AP
MAC in `setup()`, so multiple loggers on the same site can be told apart. `/status`
reports the full `ap_ssid` string.

- `GET /` — live HTML dashboard (pressure gauge, status pills, clock sync, log controls)
- `GET /status` — JSON: AP info, I2C scan, ADS1115/OLED detection, A0/sensor voltage, PSI, SD + clock status, logged row count
- `GET /download` — CSV export of the log file
- `GET /settime?epoch=<unix seconds>` — sets the software clock (browser sends local wall-clock time, already timezone-shifted)
- `GET /resetlog` — truncates the CSV log and restarts the row counter
- `GET /setinterval?ms=<n>` — sets the logging interval (persisted to flash via `Preferences`)
- `POST /update` — OTA firmware upload (multipart `.bin`, dashboard's "Firmware Update" card); flashes via `Update.h` and reboots

## Firmware versioning & releases

`scripts/get_firmware_version.py` (a PlatformIO `pre:` extra script, wired in via
`extra_scripts` in `platformio.ini`) injects a `FIRMWARE_VERSION` macro from `git
describe --tags --always --dirty` at build time — visible in the boot log, `/status`,
and the dashboard footer. Since builds run through this script, don't add a second,
manually-maintained version constant.

`.github/workflows/release.yml` builds the firmware in CI on every push of a `v*`
tag and publishes a GitHub Release with the `.bin` attached (via
`softprops/action-gh-release`); a manual `workflow_dispatch` run instead uploads a
plain build artifact. `board_build.partitions = default_16MB.csv` in `platformio.ini`
is required for OTA (`Update.h` needs the `ota_0`/`ota_1` slots that partition table
provides — the board's default one doesn't have them).

## Cutting a release

This is part of the normal workflow, not a one-off: when a meaningful chunk of work
lands on `main` and the user has confirmed it works (a hardware port, a new feature,
a real bug fix — not every small commit), tag it and push the tag to trigger
`.github/workflows/release.yml`:

```sh
git tag -a vX.Y.Z -m "short summary of what changed"
git push origin vX.Y.Z
```

There's no strict SemVer contract here yet — bump patch for fixes, minor for new
features/hardware support, major for breaking wiring/pin changes. **Proactively
suggest cutting a release** once a change is committed and confirmed working, rather
than waiting for the user to ask — the release page is the intended way to get a
`.bin` onto the OTA update page (see "Firmware versioning & releases" above), so a
feature isn't really "done" for OTA purposes until it's tagged. Still confirm with
the user before tagging/pushing, same as any other push.

## Where things stand

`TODO.md` is an active work list, not an append-only log: when a step is done, **remove it** so the file only ever shows outstanding work. Durable facts (confirmed wiring, calibration notes, observed readings) stay. `README.md` describes the intended full logger. When hardware or wiring facts change, update `TODO.md`.
