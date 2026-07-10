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

## Observed Readings

- A0 tied to GND read about `0.0000 V`.
- A0 tied to ESP32 3V3 read about `3.34 V`.
- Powered sensor at rest read about:
  - ADS1115 A0: `0.0398 V`
  - Scaled sensor voltage: `0.437 V`
  - Pressure: `0.0 PSI` with nominal calibration
- Blowing into the sensor moved the scaled sensor voltage to about `0.51 V`.

## Next Steps

1. Add and test microSD card support.
2. Append CSV rows with voltage and PSI readings.
3. Add `/download` for CSV export.
4. Add DS3231 RTC support for real timestamps.
5. Calibrate pressure once a real gauge or known pressure source is available.
6. Clean up the firmware into modules after the hardware path is proven.

## Calibration Notes

Nominal sensor calibration is still:

```text
0.5 V = 0 PSI
4.5 V = 100 PSI
```

Do not finalize calibration until comparing against a real pressure gauge.
