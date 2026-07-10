#pragma once

#include <RTClib.h>

class Clock {
 public:
  bool begin();
  String timestamp();
  bool isReady() const;

 private:
  RTC_DS3231 rtc_;
  bool ready_ = false;
};
