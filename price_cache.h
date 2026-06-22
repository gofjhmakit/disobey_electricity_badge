#pragma once

#include <time.h>

bool saveCache();
bool loadCache();
bool hasPricesForDate(time_t target);
bool hasTomorrowPrices();
int findDayRange(int offset, int &startIndex, int &count);
bool findPriceForLocalTime(time_t target, float &price);
bool getCurrentAndNextPrices(float &currentPrice, float &nextPrice);
