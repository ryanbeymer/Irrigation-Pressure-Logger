#include "PressureSensor.h"

#include "Config.h"

bool PressureSensor::begin() {
  ready_ = ads_.begin();
  if (!ready_) {
    return false;
  }

  // GAIN_TWOTHIRDS measures up to +/-6.144 V, suitable for the 0.5-4.5 V sensor output.
  ads_.setGain(GAIN_TWOTHIRDS);
  return true;
}

PressureReading PressureSensor::read() {
  PressureReading reading;
  if (!ready_) {
    return reading;
  }

  const int16_t raw = ads_.readADC_SingleEnded(Config::PressureAdcChannel);
  reading.voltage = ads_.computeVolts(raw);

  const float spanVoltage = Config::SensorMaxVoltage - Config::SensorMinVoltage;
  const float spanPsi = Config::SensorMaxPsi - Config::SensorMinPsi;
  reading.psi = ((reading.voltage - Config::SensorMinVoltage) / spanVoltage) * spanPsi + Config::SensorMinPsi;
  reading.psi = constrain(reading.psi, Config::SensorMinPsi, Config::SensorMaxPsi);
  reading.valid = true;
  return reading;
}

bool PressureSensor::isReady() const {
  return ready_;
}
