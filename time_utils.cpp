#include "time_utils.h"

bool timeReached(unsigned long deadline) {
  return (long)(millis() - deadline) >= 0;
}

unsigned long getNextLocalPublishMs(int publishHour, bool allowNow) {
  time_t now = time(nullptr);
  if (now == 0) {
    return millis() + 24UL * 60UL * 60UL * 1000UL;
  }
  tm local;
  localtime_r(&now, &local);
  local.tm_hour = publishHour;
  local.tm_min = 0;
  local.tm_sec = 0;
  time_t target = mktime(&local);
  if (target == (time_t)-1) {
    return millis() + 24UL * 60UL * 60UL * 1000UL;
  }
  if (target <= now) {
    if (allowNow) {
      return millis();
    }
    target += 24UL * 60UL * 60UL;
  }
  long deltaSeconds = (long)(target - now);
  return millis() + (unsigned long)deltaSeconds * 1000UL;
}

unsigned long getNextDayForwardCheckMs() {
  time_t now = time(nullptr);
  if (now == 0) {
    return millis() + 15UL * 60UL * 1000UL;
  }
  tm local;
  localtime_r(&now, &local);
  if (local.tm_hour < 16) {
    return getNextLocalPublishMs(16, true);
  }
  return millis() + 15UL * 60UL * 1000UL;
}

bool sameDate(const tm &lhs, const tm &rhs) {
  return lhs.tm_year == rhs.tm_year && lhs.tm_mon == rhs.tm_mon && lhs.tm_mday == rhs.tm_mday;
}

int getLocalHourSlot(const tm &timeInfo) {
  return timeInfo.tm_yday * 24 + timeInfo.tm_hour;
}

bool waitForLocalTime(unsigned long timeoutMs) {
  unsigned long deadline = millis() + timeoutMs;
  tm timeInfo;
  while (millis() < deadline) {
    if (getLocalTime(&timeInfo)) {
      return true;
    }
    delay(250);
  }
  return false;
}
