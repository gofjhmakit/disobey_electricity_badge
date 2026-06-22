#pragma once

#include <Arduino.h>
#include <time.h>

bool timeReached(unsigned long deadline);
unsigned long getNextLocalPublishMs(int publishHour, bool allowNow = false);
unsigned long getNextDayForwardCheckMs();
bool sameDate(const tm &lhs, const tm &rhs);
int getLocalHourSlot(const tm &timeInfo);
bool waitForLocalTime(unsigned long timeoutMs);
