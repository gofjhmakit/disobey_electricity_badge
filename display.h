#pragma once

#include <Arduino.h>
#include "data_models.h"

void initDisplay();
void drawHeader(const String &subtitle);
void drawFooter(const String &line1, const String &line2);
void drawMainScreen();
void drawWeatherDayScreen(int dayIndex);
void drawWeatherWeekScreen();
void drawNewsScreenList(const String &header, NewsItem *items, int count);
void drawKotimaaNewsScreen();
void drawKeskiSuomiNewsScreen();
void drawStocksScreen();
void drawTrainsScreen();
void drawCalendarScreen();
void drawSystemHealthScreen();
void drawRainAirQualityScreen();
void drawOnThisDayScreen();
void drawBusTripsScreen();
void drawCurrentScreen();
void drawSettingsScreen();
void drawConfigMode();
void drawRefreshError();
