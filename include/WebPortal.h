#pragma once

#include <WebServer.h>

#include "Clock.h"
#include "LogStorage.h"
#include "PressureSensor.h"

class WebPortal {
 public:
  WebPortal(PressureSensor &sensor, Clock &clock, LogStorage &storage);

  void begin();
  void handleClient();
  void setLatestReading(const PressureReading &reading, const String &timestamp);

 private:
  void handleRoot();
  void handleStatus();
  void handleDownload();
  String statusJson() const;

  WebServer server_;
  PressureSensor &sensor_;
  Clock &clock_;
  LogStorage &storage_;
  PressureReading latestReading_;
  String latestTimestamp_ = "unavailable";
};
