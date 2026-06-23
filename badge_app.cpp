#include "badge_app.h"
#include "app_state.h"
#include "buttons.h"
#include "config.h"
#include "debug_log.h"
#include "display.h"
#include "led_controller.h"
#include "news_client.h"
#include "nordpool_client.h"
#include "power_save.h"
#include "price_cache.h"
#include "settings_store.h"
#include "stocks_client.h"
#include "time_utils.h"
#include "trains_client.h"
#include "weather_client.h"
#include "wifi_manager.h"

#include <FFat.h>
#include <WiFi.h>

void badgeAppSetup() {
  pinMode(LED_ACTIVATE_PIN, OUTPUT);
  digitalWrite(LED_ACTIVATE_PIN, HIGH);

  Serial.begin(115200, SERIAL_8N1);
  Serial.setDebugOutput(true);
  delay(500);

  Serial.println("Display init done");
  g_app.tft.fillScreen(TFT_RED);
  delay(1000);
  g_app.tft.fillScreen(TFT_GREEN);
  delay(1000);
  g_app.tft.fillScreen(TFT_BLUE);

  delay(1000);
  Serial.println("\n--- NordpoolPriceBadge startup ---");
  Serial.flush();
  delay(100);

  Serial.println("Initializing FFat...");
  if (!FFat.begin(false)) {
    Serial.println("FFat mount failed");
  } else {
    Serial.println("FFat mounted");
  }

  initAppButtons();
  loadDefaults();
  loadSettings();
  debugLog("Settings after load:");
  debugLog("  SSID:", g_app.settings.ssid.length() ? g_app.settings.ssid.c_str() : "<empty>");
  debugLog("  Area:", g_app.settings.area.c_str());
  debugLog("  Cheap threshold:", g_app.settings.thresholdCheap);
  debugLog("  Moderate threshold:", g_app.settings.thresholdModerate);
  debugLog("  LED brightness:", g_app.settings.ledBrightness);
  setupPins();

  Serial.println("Initializing display...");
  initDisplay();

  Serial.println("Initializing LEDs...");
  initLeds();

  bool hadCache = loadCache();
  Serial.printf("Cache loaded: %d entries\n", g_app.cacheCount);
  if (hadCache && hasTomorrowPrices()) {
    g_app.nextAllowedFetchMs = getNextLocalPublishMs(16);
    debugLog("Tomorrow prices already cached at startup; deferring next fetch until local 16:00.");
  }

  if (connectWiFi()) {
    configTzTime("EET-2EEST,M3.5.0/03:00,M10.5.0/04:00", "pool.ntp.org", "time.google.com");
    g_app.gotTime = waitForLocalTime(15000);
    Serial.printf("NTP time available: %s\n", g_app.gotTime ? "yes" : "no");
    debugLog("WiFi connected successfully.");
    if (!g_app.gotTime) {
      debugLog("NTP sync failed; time-based fetch may not work.");
    }
    if (needsFetchForTomorrow() && shouldAttemptFetch()) {
      int fetched = fetchNordpoolPrices();
      Serial.printf("Fetched %d price points\n", fetched);
      if (fetched <= 0 && !hadCache) {
        debugLog("No data and no cache, entering config mode.");
        drawConfigMode();
        return;
      }
    } else {
      debugLog("Tomorrow prices already cached or fetch throttled.");
    }
    // Fetch trains first while heap is least fragmented.
    fetchTrains();
    fetchWeatherForecast();
    fetchKotimaaNews();
    fetchKeskiSuomiNews();
    if (millis() >= g_app.stockRateLimitUntilMs) {
      fetchStocks();
    }
  } else {
    debugLog("WiFi connection failed.");
    if (!hadCache) {
      debugLog("No WiFi and no cache, entering config mode.");
      drawConfigMode();
      return;
    }
  }

  if (g_app.cacheCount > 0) {
    Serial.println("Displaying current screen");
    drawCurrentScreen();
    if (!g_app.powerSaveActive) {
      float currentPrice = 0;
      float nextPrice = 0;
      if (getCurrentAndNextPrices(currentPrice, nextPrice)) {
        updateLEDsForHour(currentPrice, nextPrice);
      }
    }
  } else {
    Serial.println("No cached data available, config mode");
    drawConfigMode();
  }
  g_app.lastInteractionMs = millis();
}

void badgeAppLoop() {
  updateButtonStates();

  if (anyButtonIsPressed()) {
    g_app.lastInteractionMs = millis();
  }

  if (g_app.powerSaveActive && consumeAnyButtonPressEvent()) {
    registerInteraction();
    return;
  }

  if (buttonPressedOnce(BTN_SELECT)) {
    debugLog("BTN_SELECT pressed: cycling brightness.");
    g_app.settings.ledBrightness = (g_app.settings.ledBrightness + 1) % 6;
    saveSettings();
    drawCurrentScreen();
  }

  if (buttonPressedOnce(BTN_START)) {
    debugLog("BTN_START pressed: toggling settings screen.");
    g_app.showSettings = !g_app.showSettings;
    g_app.settingsIndex = 0;
    if (g_app.showSettings) {
      drawSettingsScreen();
    } else {
      drawCurrentScreen();
    }
  }

  if (g_app.showSettings) {
    if (buttonPressedOnce(BTN_UP)) {
      debugLog("BTN_UP pressed: previous settings option.");
      g_app.settingsIndex = max(0, g_app.settingsIndex - 1);
      drawSettingsScreen();
    }
    if (buttonPressedOnce(BTN_DOWN)) {
      debugLog("BTN_DOWN pressed: next settings option.");
      g_app.settingsIndex = min(2, g_app.settingsIndex + 1);
      drawSettingsScreen();
    }
    if (buttonPressedOnce(BTN_LEFT)) {
      debugLog("BTN_LEFT pressed: decrease threshold value.");
      changeThreshold(g_app.settingsIndex, g_app.settingsIndex == 2 ? -1 : -0.1);
      drawSettingsScreen();
    }
    if (buttonPressedOnce(BTN_RIGHT)) {
      debugLog("BTN_RIGHT pressed: increase threshold value.");
      changeThreshold(g_app.settingsIndex, g_app.settingsIndex == 2 ? 1 : 0.1);
      drawSettingsScreen();
    }
    if (buttonPressedOnce(BTN_A) || buttonPressedOnce(BTN_B)) {
      debugLog("BTN_A or BTN_B pressed: exit settings.");
      g_app.showSettings = false;
      drawCurrentScreen();
    }
    return;
  }

  if (buttonPressedOnce(BTN_LEFT)) {
    debugLog("BTN_LEFT pressed: previous screen.");
    g_app.activeScreen = max(0, g_app.activeScreen - 1);
    drawCurrentScreen();
  }
  if (buttonPressedOnce(BTN_RIGHT)) {
    debugLog("BTN_RIGHT pressed: next screen.");
    g_app.activeScreen = min(SCREEN_COUNT - 1, g_app.activeScreen + 1);
    drawCurrentScreen();
  }
  if (buttonPressedOnce(BTN_STICK)) {
    debugLog("BTN_STICK pressed: toggle price today/tomorrow view.");
    g_app.activeScreen = g_app.activeScreen == SCREEN_PRICE_TODAY ? SCREEN_PRICE_TOMORROW
                                                                  : SCREEN_PRICE_TODAY;
    drawCurrentScreen();
  }
  if (buttonPressedOnce(BTN_B)) {
    debugLog("BTN_B pressed: manual refresh.");
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }
    if (WiFi.status() == WL_CONNECTED) {
      if (needsFetchForTomorrow() && shouldAttemptFetch()) {
        fetchNordpoolPrices();
      } else {
        debugLog("Refresh skipped: data already current or fetch throttled.");
      }
      fetchTrains();
      fetchWeatherForecast();
      fetchKotimaaNews();
      fetchKeskiSuomiNews();
      if (millis() >= g_app.stockRateLimitUntilMs) {
        fetchStocks();
      } else {
        debugLog("Stock refresh skipped: rate limited");
      }
      drawCurrentScreen();
    } else {
      drawRefreshError();
    }
  }
  if (buttonPressedOnce(BTN_A)) {
    debugLog("BTN_A pressed: redraw current screen.");
    drawCurrentScreen();
  }

  unsigned long now = millis();
  if (!g_app.powerSaveActive && now - g_app.lastInteractionMs >= POWER_SAVE_TIMEOUT_MS) {
    enterPowerSaveMode();
  }
  updatePowerSaveLEDsOnHour();
  updateWeatherLEDAnimation();

  if (now - g_app.lastFetchMs > 15UL * 60UL * 1000UL) {
    if (WiFi.status() == WL_CONNECTED && needsFetchForTomorrow() && shouldAttemptFetch()) {
      if (fetchNordpoolPrices() > 0) {
        if (!g_app.powerSaveActive) {
          drawCurrentScreen();
        } else {
          refreshPowerSaveLEDs();
        }
      }
    }
    g_app.lastFetchMs = now;
  }

  if (now - g_app.lastWeatherFetchMs > 3UL * 60UL * 60UL * 1000UL) {
    if (WiFi.status() == WL_CONNECTED && fetchWeatherForecast() && !g_app.powerSaveActive &&
        g_app.activeScreen >= SCREEN_WEATHER_TODAY) {
      drawCurrentScreen();
    }
  }

  if (now - g_app.lastNewsFetchMs > 60UL * 60UL * 1000UL) {
    if (WiFi.status() == WL_CONNECTED && fetchKotimaaNews() && !g_app.powerSaveActive &&
        g_app.activeScreen == SCREEN_NEWS_KOTIMAA) {
      drawCurrentScreen();
    }
  }

  if (now - g_app.lastNewsKeskiSuomiFetchMs > 60UL * 60UL * 1000UL) {
    if (WiFi.status() == WL_CONNECTED && fetchKeskiSuomiNews() && !g_app.powerSaveActive &&
        g_app.activeScreen == SCREEN_NEWS_KESKI_SUOMI) {
      drawCurrentScreen();
    }
  }

  if (now - g_app.lastStockFetchMs > STOCK_FETCH_INTERVAL_MS &&
      millis() >= g_app.stockRateLimitUntilMs) {
    if (WiFi.status() == WL_CONNECTED && !g_app.powerSaveActive &&
        g_app.activeScreen == SCREEN_STOCKS) {
      fetchStocks();
      drawCurrentScreen();
    }
  }

  if (now - g_app.lastTrainFetchMs > TRAIN_FETCH_INTERVAL_MS) {
    if (WiFi.status() == WL_CONNECTED && fetchTrains() && !g_app.powerSaveActive &&
        g_app.activeScreen == SCREEN_TRAINS) {
      drawCurrentScreen();
    }
  }
}
