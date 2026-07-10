#pragma once

#include <Adafruit_ADS1X15.h>

struct PressureReading {
  float voltage = 0.0f;
  float psi = 0.0f;
  bool valid = false;
};

class PressureSensor {
 public:
  bool begin();
  PressureReading read();
  bool isReady() const;

 private:
  Adafruit_ADS1115 ads_;
  bool ready_ = false;
};
