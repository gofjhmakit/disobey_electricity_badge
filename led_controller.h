#pragma once

#include <FastLED.h>

CRGB colorForValue(float price);
void initLeds();
void updateLEDsForHour(float currentPrice, float nextPrice);
void updatePowerSaveLEDs(float currentPrice, float nextPrice);
void refreshPowerSaveLEDs();
void updateWeatherLEDAnimation();
