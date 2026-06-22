#include "led_controller.h"
#include "app_state.h"
#include "config.h"
#include "price_cache.h"

#include <FastLED.h>
#include <math.h>

CRGB colorForValue(float price) {
  if (price <= g_app.settings.thresholdCheap) {
    return CRGB::Lime;
  }
  if (price <= g_app.settings.thresholdModerate) {
    return CRGB::Orange;
  }
  return CRGB::Red;
}

static uint8_t scaledLedBrightness() {
  return (uint8_t)(255.0f * pow((float)g_app.settings.ledBrightness / 5.0f, 2.0f));
}

static int weatherCategory(int code) {
  if (code == 0) return WEATHER_CLEAR;
  if (code == 45 || code == 48) return WEATHER_FOG;
  if (code >= 95 && code <= 99) return WEATHER_THUNDER;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WEATHER_RAIN;
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return WEATHER_SNOW;
  return WEATHER_CLOUDY;
}

static bool activeWeatherDayIndex(int &dayIndex) {
  if (g_app.activeScreen == SCREEN_WEATHER_TODAY) {
    dayIndex = 0;
    return true;
  }
  if (g_app.activeScreen == SCREEN_WEATHER_TOMORROW) {
    dayIndex = 1;
    return true;
  }
  return false;
}

static void fillDimSnowLEDs() {
  digitalWrite(LED_ACTIVATE_PIN, HIGH);
  FastLED.setBrightness(min((uint8_t)60, scaledLedBrightness()));
  for (int i = 0; i < LED_AMOUNT; ++i) {
    g_app.leds[i] = CRGB(70, 70, 80);
  }
  FastLED.show();
}

void initLeds() {
  FastLED.addLeds<NEOPIXEL, LED_PIN>(g_app.leds, LED_AMOUNT);
  uint8_t initialBrightness = (uint8_t)(255.0f * pow(1.0f / 5.0f, 2.0f));
  FastLED.setBrightness(initialBrightness);
  for (int i = 0; i < LED_AMOUNT; ++i) {
    g_app.leds[i] = CRGB::SkyBlue;
  }
  FastLED.show();
}

void updateLEDsForHour(float currentPrice, float nextPrice) {
  if (g_app.settings.ledBrightness == 0) {
    digitalWrite(LED_ACTIVATE_PIN, LOW);
    FastLED.clear(true);
    return;
  }

  digitalWrite(LED_ACTIVATE_PIN, HIGH);
  FastLED.setBrightness(scaledLedBrightness());
  CRGB leftColor = colorForValue(currentPrice);
  CRGB rightColor = colorForValue(nextPrice);

  for (int i = 0; i < LED_AMOUNT; ++i) {
    if (i < LED_AMOUNT / 2) {
      g_app.leds[i] = rightColor;
    } else {
      g_app.leds[i] = leftColor;
    }
  }
  FastLED.show();
}

void updatePowerSaveLEDs(float currentPrice, float nextPrice) {
  digitalWrite(LED_ACTIVATE_PIN, HIGH);
  FastLED.setBrightness(POWER_SAVE_LED_BRIGHTNESS);
  for (int i = 0; i < LED_AMOUNT; ++i) {
    g_app.leds[i] = CRGB::Black;
  }

  CRGB currentColor = colorForValue(currentPrice);
  CRGB nextColor = colorForValue(nextPrice);

  g_app.leds[7] = currentColor;
  g_app.leds[2] = nextColor;
  FastLED.show();
}

void refreshPowerSaveLEDs() {
  if (g_app.settings.ledBrightness == 0) {
    digitalWrite(LED_ACTIVATE_PIN, LOW);
    FastLED.clear(true);
    return;
  }

  float currentPrice = 0;
  float nextPrice = 0;
  if (getCurrentAndNextPrices(currentPrice, nextPrice)) {
    updatePowerSaveLEDs(currentPrice, nextPrice);
  } else {
    FastLED.clear(true);
  }
}

void updateWeatherLEDAnimation() {
  if (g_app.powerSaveActive || g_app.showSettings || g_app.settings.ledBrightness == 0) {
    return;
  }

  int dayIndex = 0;
  if (!activeWeatherDayIndex(dayIndex) || dayIndex >= g_app.weatherCount) {
    return;
  }

  int category = weatherCategory(g_app.weatherDays[dayIndex].weatherCode);
  unsigned long now = millis();
  unsigned long frameInterval = category == WEATHER_RAIN ? 120UL : 45UL;
  if (now - g_app.lastWeatherLedFrameMs < frameInterval) {
    return;
  }
  g_app.lastWeatherLedFrameMs = now;
  g_app.weatherLedFrame++;

  digitalWrite(LED_ACTIVATE_PIN, HIGH);
  FastLED.setBrightness(scaledLedBrightness());

  if (category == WEATHER_CLEAR) {
    for (int i = 0; i < LED_AMOUNT; ++i) {
      uint8_t wave = sin8(g_app.weatherLedFrame * 6 + i * 22);
      g_app.leds[i] = blend(CRGB::Orange, CRGB::Yellow, wave);
    }
  } else if (category == WEATHER_CLOUDY || category == WEATHER_FOG) {
    uint8_t pulse = beatsin8(18, 35, category == WEATHER_FOG ? 95 : 135);
    for (int i = 0; i < LED_AMOUNT; ++i) {
      g_app.leds[i] = CRGB(pulse, pulse, pulse);
    }
  } else if (category == WEATHER_RAIN || category == WEATHER_THUNDER) {
    const uint8_t leftFall[] = {7, 8, 9, 6, 5};
    const uint8_t rightFall[] = {4, 3, 2, 1, 0};
    FastLED.clear(false);
    int step = (g_app.weatherLedFrame / 2) % 5;
    CRGB color = category == WEATHER_THUNDER ? CRGB::Purple : CRGB::Blue;
    g_app.leds[leftFall[step]] = color;
    g_app.leds[rightFall[step]] = color;
  } else if (category == WEATHER_SNOW) {
    fillDimSnowLEDs();
    return;
  }

  FastLED.show();
}
