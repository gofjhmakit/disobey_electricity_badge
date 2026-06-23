#include "price_cache.h"
#include "app_state.h"
#include "config.h"
#include "debug_log.h"
#include "time_utils.h"

#include <FFat.h>
#include <ArduinoJson.h>
#include <math.h>

bool saveCache() {
  debugLog("Saving price cache...");
  DynamicJsonDocument fileDoc(8192);
  fileDoc["area"] = g_app.settings.area;
  fileDoc["timestamp"] = time(nullptr);
  JsonArray priceArray = fileDoc.createNestedArray("prices");
  for (int i = 0; i < g_app.cacheCount; ++i) {
    JsonObject item = priceArray.createNestedObject();
    item["year"] = g_app.cacheItems[i].year;
    item["month"] = g_app.cacheItems[i].month;
    item["day"] = g_app.cacheItems[i].day;
    item["hour"] = g_app.cacheItems[i].hour;
    item["price"] = g_app.cacheItems[i].price;
  }

  fs::File file = FFat.open(CACHE_FILE, FILE_WRITE);
  if (!file) {
    debugLog("Failed to open cache file for write");
    return false;
  }
  serializeJson(fileDoc, file);
  file.close();
  debugLog("Price cache saved.");
  return true;
}

bool loadCache() {
  debugLog("Checking for cached price data...");
  if (!FFat.exists(CACHE_FILE)) {
    debugLog("Cache file not found.");
    return false;
  }
  fs::File file = FFat.open(CACHE_FILE, FILE_READ);
  if (!file) {
    debugLog("Failed to open cache file.");
    return false;
  }
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    debugLog("Cached JSON parse failed.");
    return false;
  }
  String cachedArea = doc["area"].as<const char *>();
  debugLog("Cached area:", cachedArea.c_str());
  if (cachedArea != g_app.settings.area) {
    debugLog("Cache area mismatch, ignoring cache.");
    return false;
  }
  JsonArray priceArray = doc["prices"].as<JsonArray>();
  g_app.cacheCount = 0;
  for (JsonObject item : priceArray) {
    if (g_app.cacheCount >= MAX_PRICE_POINTS) break;
    g_app.cacheItems[g_app.cacheCount++] = {
      item["year"].as<int>(),
      item["month"].as<int>(),
      item["day"].as<int>(),
      item["hour"].as<int>(),
      item["price"].as<float>()
    };
  }
  float maxAbsPrice = 0;
  for (int i = 0; i < g_app.cacheCount; ++i) {
    maxAbsPrice = max(maxAbsPrice, fabsf(g_app.cacheItems[i].price));
  }
  if (maxAbsPrice > 0 && maxAbsPrice < 1.0f) {
    debugLog("Cached prices look like EUR/kWh; converting to c/kWh.");
    for (int i = 0; i < g_app.cacheCount; ++i) {
      g_app.cacheItems[i].price *= 100.0f;
    }
    saveCache();
  }
  debugLog("Cache loaded entries:", g_app.cacheCount);
  return g_app.cacheCount > 0;
}

bool hasPricesForDate(time_t target) {
  if (target == 0) {
    return false;
  }
  tm targetTm;
  localtime_r(&target, &targetTm);
  int count = 0;
  for (int i = 0; i < g_app.cacheCount; ++i) {
    if (g_app.cacheItems[i].year == targetTm.tm_year + 1900 &&
        g_app.cacheItems[i].month == targetTm.tm_mon + 1 &&
        g_app.cacheItems[i].day == targetTm.tm_mday) {
      count++;
    }
  }
  return count >= 24;
}

bool hasTomorrowPrices() {
  time_t now = time(nullptr);
  if (now == 0) {
    return false;
  }
  return hasPricesForDate(now + 24UL * 60UL * 60UL);
}

int findDayRange(int offset, int &startIndex, int &count) {
  time_t now = time(nullptr);
  time_t target = now + offset * 86400;
  tm targetTm;
  localtime_r(&target, &targetTm);
  startIndex = -1;
  count = 0;

  for (int i = 0; i < g_app.cacheCount; ++i) {
    tm itemTm;
    itemTm.tm_year = g_app.cacheItems[i].year - 1900;
    itemTm.tm_mon = g_app.cacheItems[i].month - 1;
    itemTm.tm_mday = g_app.cacheItems[i].day;
    if (sameDate(itemTm, targetTm)) {
      if (startIndex < 0) {
        startIndex = i;
      }
      count++;
    }
  }
  return count;
}

bool findPriceForLocalTime(time_t target, float &price) {
  if (target == 0) {
    return false;
  }

  tm targetTm;
  localtime_r(&target, &targetTm);
  for (int i = 0; i < g_app.cacheCount; ++i) {
    if (g_app.cacheItems[i].year == targetTm.tm_year + 1900 &&
        g_app.cacheItems[i].month == targetTm.tm_mon + 1 &&
        g_app.cacheItems[i].day == targetTm.tm_mday &&
        g_app.cacheItems[i].hour == targetTm.tm_hour) {
      price = g_app.cacheItems[i].price;
      return true;
    }
  }
  return false;
}

bool getCurrentAndNextPrices(float &currentPrice, float &nextPrice) {
  time_t now = time(nullptr);
  if (now == 0 || !findPriceForLocalTime(now, currentPrice)) {
    return false;
  }

  if (!findPriceForLocalTime(now + 60UL * 60UL, nextPrice)) {
    nextPrice = currentPrice;
  }
  return true;
}
