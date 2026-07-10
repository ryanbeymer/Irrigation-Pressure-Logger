#pragma once

#include <Arduino.h>
#include <FS.h>

#include "PressureSensor.h"

class LogStorage {
 public:
  bool begin();
  bool append(const String &timestamp, const PressureReading &reading);
  bool isReady() const;
  bool exists(const char *path) const;
  File openRead(const char *path);

 private:
  bool ensureHeader();

  bool ready_ = false;
};
