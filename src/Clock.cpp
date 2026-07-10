#include "Clock.h"

bool Clock::begin() {
  ready_ = rtc_.begin();
  return ready_;
}

String Clock::timestamp() {
  if (!ready_) {
    return "unavailable";
  }

  const DateTime now = rtc_.now();
  char buffer[25];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return String(buffer);
}

bool Clock::isReady() const {
  return ready_;
}
