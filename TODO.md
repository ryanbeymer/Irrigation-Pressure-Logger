# Irrigation Logger Progress

## Current Working Checkpoint

- ESP32 boots and starts Serial at 115200 baud.
- Wi-Fi access point starts as `IrrigationLogger`.
- Local status page is available at `http://192.168.4.1`.
- JSON status is available at `http://192.168.4.1/status`.
- ADS1115 is detected on I2C at `0x48`.
- SSD1306 OLED is detected on I2C at `0x3C`.
- OLED displays live logger values and updates.
- Pressure sensor signal is readable through the ADS1115.
- HW-125 microSD initializes on the SPI bus.
- CSV rows (`timestamp,millis,pressure_psi,voltage`) append to `/pressure_log.csv` every ~2 s.
  The `millis` column resets to ~0 on reboot, so a drop in it marks a device restart.
- `http://192.168.4.1/download` returns the CSV log; `/resetlog` starts a fresh file.
- DS3231 RTC detected on I2C at `0x68`; CSV timestamps are real date/time.
- `/settime` (and the web "Sync clock" button) set the RTC from the browser's local time.
- Live web dashboard at `http://192.168.4.1`: pressure gauge, chip-status pills, clock sync, JSON/CSV links.
- Pressure calibrated for the 0-80 PSI sensor: zero anchored at 0.437 V, 20 PSI/V slope.
- Logging interval is web-adjustable (`/setinterval`, dashboard dropdown), saved to flash
  (NVS) so it survives reboots/reflashes; sampling stays at 2 s, logging decoupled.

## Confirmed Wiring

### I2C Bus

The ESP32 board labeling required the I2C pins swapped from the original assumption:

| Device Pin | ESP32 Pin |
| --- | --- |
| ADS1115 SDA | GPIO 22 |
| ADS1115 SCL | GPIO 21 |
| OLED SDA | GPIO 22 |
| OLED SCL | GPIO 21 |

### ADS1115

| ADS1115 Pin | Connection |
| --- | --- |
| VDD/VCC | ESP32 3V3 |
| GND | ESP32 GND |
| SDA | ESP32 GPIO 22 |
| SCL | ESP32 GPIO 21 |
| ADDR | GND |
| A0 | Pressure sensor divider output |

### Pressure Sensor Divider

Current resistor values:

```text
Sensor green signal -> 10k resistor -> ADS1115 A0
ADS1115 A0          -> 1k resistor  -> GND
Sensor red          -> ESP32 5V/VIN
Sensor black        -> ESP32 GND
```

The firmware currently uses a divider scale of `11.0`.

### microSD (HW-125, SPI)

| HW-125 Pin | ESP32 Pin |
| --- | --- |
| CS | GPIO 5 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| VCC | ESP32 VIN (5V) |
| GND | ESP32 GND |

The HW-125's onboard regulator needs 5V — powering VCC from 3V3 fails to init. Card must be FAT32.

### DS3231 RTC (HW-084, I2C)

| HW-084 Pin | ESP32 Pin |
| --- | --- |
| VCC | ESP32 3V3 |
| GND | ESP32 GND |
| SDA | ESP32 GPIO 22 |
| SCL | ESP32 GPIO 21 |
| 32K / SQW / RST | not connected |

Shares the I2C bus with the ADS1115 and OLED. Powered from 3V3 so the module's bus
pull-ups stay at 3.3V. The onboard AT24C32 EEPROM also appears at `0x57`. Time is
seeded from the firmware build clock only when the coin cell has lost power.

## Observed Readings

- A0 tied to GND read about `0.0000 V`.
- A0 tied to ESP32 3V3 read about `3.34 V`.
- Powered sensor at rest read about:
  - ADS1115 A0: `0.0398 V`
  - Scaled sensor voltage: `0.437 V`
  - Pressure: `0.0 PSI` with nominal calibration
- Blowing into the sensor moved the scaled sensor voltage to about `0.51 V`.

## Next Steps

1. Optional: tighten the slope with a higher, steady known-pressure reading (50-70 PSI).
2. Clean up the firmware into modules after the hardware path is proven.

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
