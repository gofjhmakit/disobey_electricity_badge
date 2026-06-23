#pragma once

// Hardware pins
#define TFT_BL 19

#define LED_PIN          18
#define LED_ACTIVATE_PIN 17
#define LED_AMOUNT       10

#define BTN_UP     11
#define BTN_DOWN   1
#define BTN_LEFT   21
#define BTN_RIGHT  2
#define BTN_STICK  14
#define BTN_A      13
#define BTN_B      38
#define BTN_START  12
#define BTN_SELECT 45

// LittleFS paths
#define CACHE_FILE    "/price_cache.json"
#define SETTINGS_FILE "/badge_settings.json"

// Data limits
#define MAX_PRICE_POINTS 72
#define MAX_BAR_COUNT    24
#define MAX_WEATHER_DAYS 7
#define MAX_NEWS_ITEMS   5
#define MAX_STOCKS       8
#define MAX_TRAINS       6
#define MAX_HOLIDAYS     20
#define MAX_RAIN_SLOTS   12

// Screen IDs
#define SCREEN_PRICE_TODAY        0
#define SCREEN_PRICE_TOMORROW     1
#define SCREEN_WEATHER_TODAY      2
#define SCREEN_WEATHER_TOMORROW   3
#define SCREEN_WEATHER_WEEK       4
#define SCREEN_NEWS_KOTIMAA       5
#define SCREEN_NEWS_KESKI_SUOMI   6
#define SCREEN_STOCKS             7
#define SCREEN_TRAINS             8
#define SCREEN_CALENDAR           9
#define SCREEN_SYSTEM_HEALTH      10
#define SCREEN_RAIN_AIR_QUALITY   11
#define SCREEN_COUNT              12

// Weather categories (for LED animations)
#define WEATHER_CLEAR   0
#define WEATHER_CLOUDY  1
#define WEATHER_FOG     2
#define WEATHER_RAIN    3
#define WEATHER_SNOW    4
#define WEATHER_THUNDER 5

// Fetch intervals
#define STOCK_FETCH_INTERVAL_MS (60UL * 60UL * 1000UL)
#define STOCK_RATE_LIMIT_MS     (60UL * 60UL * 1000UL)
#define TRAIN_FETCH_INTERVAL_MS (30UL * 60UL * 1000UL)
#define HOLIDAY_FETCH_INTERVAL_MS (12UL * 60UL * 60UL * 1000UL)
#define RAIN_AIR_FETCH_INTERVAL_MS (30UL * 60UL * 1000UL)

// Power save
#define POWER_SAVE_TIMEOUT_MS       (10UL * 60UL * 1000UL)
#define POWER_SAVE_LED_BRIGHTNESS   10
