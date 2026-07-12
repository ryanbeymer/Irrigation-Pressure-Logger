# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

ESP32 firmware (PlatformIO + Arduino framework) for an irrigation pressure logger. A 0-100 PSI / 0.5-4.5 V pressure transducer feeds an ADS1115 ADC over I2C; the ESP32 hosts a Wi-Fi access point serving a status page. An SSD1306 OLED shows live readings. DS3231 RTC and microSD logging are planned but not yet active.

## Commands

```sh
pio run                        # Build (compiles only src/main.cpp — see below)
pio run -t upload              # Build + flash over USB
pio device monitor -b 115200   # Serial monitor
```

Board: `esp32dev`. No host-side test runner — validation is done on-device via the Serial monitor and the `/status` JSON endpoint.

## Critical: only main.cpp is compiled

`platformio.ini` sets `build_src_filter = +<main.cpp>`, so **only `src/main.cpp` is built.** The other files in `src/` and `include/` (`PressureSensor`, `Clock`, `LogStorage`, `WebPortal`) are design stubs for a future modular milestone and are **not compiled**. Editing them has no effect on the firmware until the build filter and `lib_deps` are changed.

`main.cpp` is deliberately self-contained: it re-declares its own constants, talks to the ADS1115 by writing I2C registers directly (not via a library), and drives the OLED. Treat it as the single source of truth for current behavior.

## Two parallel realities — do not conflate them

There are meaningful discrepancies between the working firmware (`main.cpp`, matches the wired hardware) and the stub modules / `Config.h` (still reflect the original design assumptions). When changing anything, follow `main.cpp` + `TODO.md`, not the stubs:

| Concern | Working (`main.cpp`, `TODO.md`) | Stubs / `Config.h` |
| --- | --- | --- |
| I2C SDA / SCL | GPIO **22 / 21** (physically confirmed) | GPIO 21 / 22 |
| ADC read | Raw I2C register writes, LSB `0.000125 V`, `±4.096V` range | Adafruit_ADS1X15 lib, `GAIN_TWOTHIRDS` |
| Voltage divider | Scale `11.0` (10k/1k divider on A0) | No divider |
| Sample interval | 2 s (`AdcReadIntervalMs`) | 5 min (`SampleIntervalMs`) |
| SD / RTC / CSV | Not present | Implemented in stubs |

`lib_deps` currently lists only `Adafruit SSD1306`. Activating the stub modules would additionally require `Adafruit ADS1X15`, `RTClib`, and `ArduinoJson`, plus updating `build_src_filter`.

## Pressure calculation

Nominal calibration (NOT yet verified against a real gauge — see `TODO.md`):
- `sensor_voltage = adc_a0_voltage * 11.0` (undo the 10k/1k divider)
- `psi = (sensor_voltage - 0.5) * (100 / (4.5 - 0.5))`, clamped at 0

## HTTP endpoints (AP at `http://192.168.4.1`, SSID `IrrigationLogger`)

- `GET /` — HTML status page
- `GET /status` — JSON: AP info, I2C scan, ADS1115/OLED detection, A0 voltage, sensor voltage, PSI
- `GET /download` — planned CSV export (only in the unbuilt `WebPortal` stub)

## Where things stand

`TODO.md` is an active work list, not an append-only log: when a step is done, **remove it** so the file only ever shows outstanding work. Durable facts (confirmed wiring, calibration notes, observed readings) stay. `README.md` describes the intended full logger. When hardware or wiring facts change, update `TODO.md`.
