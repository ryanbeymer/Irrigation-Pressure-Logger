#include "LogStorage.h"

#include <SD.h>

#include "Config.h"

bool LogStorage::begin() {
  ready_ = SD.begin(Config::SdChipSelectPin);
  if (!ready_) {
    return false;
  }

  return ensureHeader();
}

bool LogStorage::append(const String &timestamp, const PressureReading &reading) {
  if (!ready_ || !reading.valid) {
    return false;
  }

  File file = SD.open(Config::LogPath, FILE_APPEND);
  if (!file) {
    return false;
  }

  file.print(timestamp);
  file.print(',');
  file.print(reading.psi, 2);
  file.print(',');
  file.println(reading.voltage, 4);
  file.close();
  return true;
}

bool LogStorage::isReady() const {
  return ready_;
}

bool LogStorage::exists(const char *path) const {
  return ready_ && SD.exists(path);
}

File LogStorage::openRead(const char *path) {
  if (!ready_) {
    return File();
  }

  return SD.open(path, FILE_READ);
}

bool LogStorage::ensureHeader() {
  if (SD.exists(Config::LogPath)) {
    return true;
  }

  File file = SD.open(Config::LogPath, FILE_WRITE);
  if (!file) {
    ready_ = false;
    return false;
  }

  file.println("timestamp,pressure_psi,voltage");
  file.close();
  return true;
}
