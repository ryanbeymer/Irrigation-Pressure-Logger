# Irrigation Logger Progress

## Board: LilyGo T-SIM7080G-S3

The project moved from a classic-ESP32 DOIT DevKit to a **LilyGo T-SIM7080G-S3**
(ESP32-S3, onboard AXP2101 PMU, onboard microSD slot, SIM7080G modem — modem/PMU
unused by this firmware so far). This replaces the old board entirely; the wiring
facts below are for the new board only.

Porting from the DOIT DevKit surfaced two ESP32-S3 gotchas worth remembering:
- **GPIO 22-25 don't exist** on the S3 (unlike classic ESP32's continuous GPIO range).
  The old firmware's `Wire.begin(22, 21)` failed with `sda gpio number error`, which
  left the I2C driver in a broken state and hung the first bus-scan transaction
  forever — looked like a boot hang, wasn't actually one.
- **GPIO 19/20 are hardwired to the native USB D-/D+ lines.** Reusing them (e.g. for
  SPI) breaks the USB serial connection itself.

`platformio.ini` targets `board = esp32-s3-devkitc-1` (closest generic match; the
LilyGo isn't in PlatformIO's board list) with `board_upload.flash_size = 16MB` and
`-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` so Serial works over the same
USB port used for flashing.

## Current Working Checkpoint

- ESP32-S3 boots and starts Serial (USB CDC) at 115200 baud.
- Wi-Fi access point starts as `IrrigationLogger-<last 6 hex chars of AP MAC>`
  (e.g. `IrrigationLogger-1B1FEC`), so multiple loggers can be told apart on site.
- A web-based OTA updater is live: the dashboard's "Firmware Update" card uploads a
  `.bin` to `POST /update`, which flashes it via the `Update` library and reboots.
  Requires `board_build.partitions = default_16MB.csv` (adds the ota_0/ota_1 slots
  Update.h needs — the board's default partition table doesn't have them).
- Firmware is versioned from git: `scripts/get_firmware_version.py` (a PlatformIO
  `pre:` extra script) injects `FIRMWARE_VERSION` from `git describe --tags --always
  --dirty` at build time. Shows in the boot log, `/status` (`firmware_version`), and
  the dashboard footer — check it after an OTA flash to confirm the new build landed.
  No tags exist yet, so local builds currently show a bare commit hash (`+"-dirty"` if
  the working tree has uncommitted changes) rather than a version like `v1.0.0`.
- `.github/workflows/release.yml` builds the firmware in CI and publishes it: pushing
  a `v*` tag (e.g. `v1.0.0`) creates a GitHub Release with the `.bin` attached (named
  `irrigation-pressure-logger-<tag>.bin`); a manual run (Actions tab → "Run workflow")
  instead uploads it as a plain build artifact. This is the intended way to get a
  `.bin` onto the OTA page without a local PlatformIO build.
- Local status page is available at `http://192.168.4.1`.
- JSON status is available at `http://192.168.4.1/status`.
- I2C bus scan runs cleanly on GPIO 15 (SDA) / GPIO 7 (SCL) — no ADS1115/OLED
  wired to the new board yet, so both report "not detected" until that's done.
- Onboard microSD (SD_MMC, 1-bit mode) initializes.
- CSV rows (`timestamp,millis,pressure_psi,voltage`) append to `/pressure_log.csv` every ~2 s.
  The `millis` column resets to ~0 on reboot, so a drop in it marks a device restart.
- `http://192.168.4.1/download` returns the CSV log; `/resetlog` starts a fresh file.
- No hardware RTC on this board (the AXP2101 PMU has no calendar/date-time registers,
  only a battery-backed power rail). `/settime` (and the web "Sync clock" button) set
  a **software clock** — an epoch captured against `millis()` — instead. It resets to
  ms-since-boot on every reboot until re-synced from the browser.
- Live web dashboard at `http://192.168.4.1`: pressure gauge, chip-status pills, clock sync, JSON/CSV links.
- Pressure calibrated for the 0-80 PSI sensor: zero anchored at 0.437 V, 20 PSI/V slope
  (calibration itself is unchanged by the board swap — same sensor, same divider math).
- Logging interval is web-adjustable (`/setinterval`, dashboard dropdown), saved to flash
  (NVS) so it survives reboots/reflashes; sampling stays at 2 s, logging decoupled.

## Confirmed Wiring

### I2C Bus

Fixed pins on this board (shared with the onboard AXP2101 PMU — do not reassign):

| Device Pin | ESP32-S3 Pin |
| --- | --- |
| ADS1115 SDA | GPIO 15 |
| ADS1115 SCL | GPIO 7 |
| OLED SDA | GPIO 15 |
| OLED SCL | GPIO 7 |

**Not yet physically wired** — OLED and ADS1115 still need to be connected to this bus.

### ADS1115

| ADS1115 Pin | Connection |
| --- | --- |
| VDD/VCC | 3V3 |
| GND | GND |
| SDA | GPIO 15 |
| SCL | GPIO 7 |
| ADDR | GND |
| A0 | Pressure sensor divider output |

### Pressure Sensor Divider

Same divider as before (sensor-to-ADS1115 wiring is independent of the host board):

```text
Sensor green signal -> 10k resistor -> ADS1115 A0
ADS1115 A0          -> 1k resistor  -> GND
Sensor red          -> 5V rail
Sensor black        -> GND
```

The firmware currently uses a divider scale of `11.0`. **Unconfirmed:** which physical
pin on the LilyGo board supplies 5V for the sensor's red wire — needs to be checked
against the board's silkscreen/schematic when wiring it up.

### microSD (onboard, SD_MMC)

Built into the board — no external module. Uses the ESP32-S3 SDMMC peripheral in
1-bit mode (not SPI):

| Signal | ESP32-S3 Pin |
| --- | --- |
| CLK | GPIO 38 |
| CMD | GPIO 39 |
| DATA (D0) | GPIO 40 |

Card must be FAT32.

## Observed Readings

- A0 tied to GND read about `0.0000 V`.
- A0 tied to ESP32 3V3 read about `3.34 V`.
- Powered sensor at rest read about:
  - ADS1115 A0: `0.0398 V`
  - Scaled sensor voltage: `0.437 V`
  - Pressure: `0.0 PSI` with nominal calibration
- Blowing into the sensor moved the scaled sensor voltage to about `0.51 V`.

## Next Steps

1. Wire the OLED and ADS1115 (with pressure sensor) to the new board's I2C bus
   (GPIO 15 SDA / GPIO 7 SCL) and confirm both are detected in the `/status` scan.
2. Confirm the 5V supply pin for the pressure sensor's red wire on this board.
3. Insert a FAT32 microSD card and confirm `/download` returns real logged rows.
4. Confirm the `IrrigationLogger-<MAC suffix>` AP is actually visible over the air
   from a phone/laptop (only checked via serial log + an inconclusive automated
   Wi-Fi scan so far).
5. Test the OTA updater end-to-end from a phone or second device (this Mac can't
   test it directly — joining the AP drops the internet connection this session
   needs).
6. Push a `v0.1.0` tag (or run the release workflow manually) to confirm the GitHub
   Actions release process actually produces a working `.bin` end-to-end.
7. Optional: tighten the slope with a higher, steady known-pressure reading (50-70 PSI).
8. Clean up the firmware into modules after the hardware path is proven.

## Calibration Notes

Active sensor is the **0-80 PSI** unit (green signal / black GND / red 5 V), 0.5-4.5 V output.

```text
0 PSI  = 0.437 V   (measured atmospheric idle; below the nominal 0.5 V)
80 PSI = 4.437 V   (nominal 4.0 V / 20 PSI/V span)
```

Slope confirmed against a ~32 PSI reference. Zero is anchored to the measured idle so
atmospheric reads exactly 0. A higher steady reference would refine the slope further.

The 0-150 PSI ratiometric sensor that was trialed here was **DOA** — powered fine at 5 V
but its output stayed frozen at 0.33 V under pressure. Returned/replaced.

## Firmware Notes

- Large HTML pages served from `handleRoot()` **must** be declared `static const char[]`.
  A ~6 KB non-static local array is built on the loop task's stack and overflows it
  (stack canary panic / reboot on every page load).
