#pragma once

#include <time.h>

// Central application state — replaces scattered global variables.
// All modules access shared data through g_app.

#include "config.h"
#include "data_models.h"
#include "LGFX_Config.h"
#include <FastLED.h>

struct AppState {
  LGFX tft;
  CRGB leds[LED_AMOUNT];

  BadgeSettings settings;

  HourItem cacheItems[MAX_PRICE_POINTS];
  int cacheCount = 0;

  WeatherDay weatherDays[MAX_WEATHER_DAYS];
  int weatherCount = 0;

  NewsItem newsItemsKotimaa[MAX_NEWS_ITEMS];
  int newsCountKotimaa = 0;

  NewsItem newsItemsKeskiSuomi[MAX_NEWS_ITEMS];
  int newsCountKeskiSuomi = 0;

  StockItem stocks[MAX_STOCKS];
  int stockCount = 0;
  unsigned long lastStockFetchMs = 0;
  unsigned long stockRateLimitUntilMs = 0;
  time_t lastStockMarketTimestamp = 0;
  
  TrainItem trains[MAX_TRAINS];
  int trainCount = 0;
  unsigned long lastTrainFetchMs = 0;

  ButtonState buttons[10];

  bool showSettings = false;
  int settingsIndex = 0;
  int activeScreen = SCREEN_PRICE_TODAY;
  unsigned long lastRefreshMs = 0;
  unsigned long lastFetchMs = 0;
  unsigned long lastWeatherFetchMs = 0;
  unsigned long lastNewsFetchMs = 0;
  unsigned long lastNewsKeskiSuomiFetchMs = 0;
  unsigned long lastWeatherLedFrameMs = 0;
  unsigned long nextAllowedFetchMs = 0;
  unsigned long rateLimitUntilMs = 0;
  unsigned long lastInteractionMs = 0;
  bool powerSaveActive = false;
  int lastPowerSaveLedUpdateSlot = -1;
  uint8_t weatherLedFrame = 0;
  bool gotTime = false;
};

extern AppState g_app;

void initAppButtons();
