#pragma once

#include <Arduino.h>

namespace Config {

constexpr uint32_t SerialBaud = 115200;

constexpr const char *ApSsid = "IrrigationLogger";
constexpr const char *LogPath = "/pressure_log.csv";

// I2C pins for common ESP32 DevKit boards. Change here if your board is wired differently.
constexpr uint8_t I2cSdaPin = 21;
constexpr uint8_t I2cSclPin = 22;

// microSD SPI chip select. Typical modules use GPIO 5 on ESP32 DevKit examples.
constexpr uint8_t SdChipSelectPin = 5;

// ADS1115 input channel connected to the pressure transducer output.
constexpr uint8_t PressureAdcChannel = 0;

// Pressure transducer calibration: 0.5 V = 0 PSI, 4.5 V = 100 PSI.
constexpr float SensorMinVoltage = 0.5f;
constexpr float SensorMaxVoltage = 4.5f;
constexpr float SensorMinPsi = 0.0f;
constexpr float SensorMaxPsi = 100.0f;

constexpr unsigned long SampleIntervalMs = 5UL * 60UL * 1000UL;

}  // namespace Config
