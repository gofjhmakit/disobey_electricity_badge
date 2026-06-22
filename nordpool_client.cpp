#include "nordpool_client.h"
#include "app_state.h"
#include "config.h"
#include "debug_log.h"
#include "price_cache.h"
#include "time_utils.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>

bool shouldAttemptFetch() {
  return timeReached(g_app.nextAllowedFetchMs) && timeReached(g_app.rateLimitUntilMs);
}

bool needsFetchForTomorrow() {
  return !hasTomorrowPrices();
}

int fetchNordpoolPrices() {
  debugLog("fetchNordpoolPrices: starting");

  g_app.cacheCount = 0;

  String urls[] = {
    "https://api.spot-hinta.fi/Today?priceResolution=60",
    "https://api.spot-hinta.fi/DayForward?priceResolution=60"
  };

  for (int urlIdx = 0; urlIdx < 2; ++urlIdx) {
    String url = urls[urlIdx];
    logNetworkRequest("PRICE", url.c_str());

    HTTPClient client;
    client.begin(url);
    client.setTimeout(15000);
    int rc = client.GET();
    if (rc != 200) {
      client.end();
      logNetworkResponse("PRICE", rc, 0);
      Serial.printf("HTTP failed: %d\n", rc);
      if (rc == 404) {
        debugLog("Remote API returned 404: prices not available yet.");
        if (urlIdx == 0) {
          continue;
        }
        g_app.nextAllowedFetchMs = getNextDayForwardCheckMs();
        g_app.rateLimitUntilMs = 0;
        return g_app.cacheCount;
      }
      if (rc == 429) {
        debugLog("Remote API returned 429: too many requests.");
        g_app.rateLimitUntilMs = millis() + 60UL * 60UL * 1000UL;
        g_app.nextAllowedFetchMs = g_app.rateLimitUntilMs;
        return -1;
      }
      if (urlIdx == 0) {
        continue;
      }
      return -1;
    }

    String payload = client.getString();
    client.end();
    logNetworkResponse("PRICE", rc, payload.length());
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.print("JSON parse failed: ");
      Serial.println(error.c_str());
      Serial.print("Payload begin: ");
      Serial.println(payload.substring(0, min((size_t)200, payload.length())));
      if (urlIdx == 0) {
        continue;
      }
      g_app.nextAllowedFetchMs = getNextDayForwardCheckMs();
      return -1;
    }

    JsonArray priceArray = doc.as<JsonArray>();
    for (JsonObject item : priceArray) {
      if (g_app.cacheCount >= MAX_PRICE_POINTS) {
        break;
      }

      float price = item["PriceWithTax"].as<float>() * 100.0f;
      const char *dateTime = item["DateTime"].as<const char *>();
      if (!dateTime) {
        dateTime = item["Rank"].as<const char *>();
      }
      if (!dateTime) {
        continue;
      }

      int year, month, day, hour;
      if (sscanf(dateTime, "%d-%d-%dT%d:", &year, &month, &day, &hour) != 4) {
        continue;
      }

      g_app.cacheItems[g_app.cacheCount++] = {year, month, day, hour, price};
    }
  }

  debugLog("Parsed Nordpool price items:", g_app.cacheCount);
  if (g_app.cacheCount > 0) {
    if (saveCache()) {
      debugLog("Saved fetched prices to cache.");
    } else {
      debugLog("Failed to save fetched prices to cache.");
    }
  }

  if (hasTomorrowPrices()) {
    g_app.nextAllowedFetchMs = getNextLocalPublishMs(16);
    g_app.rateLimitUntilMs = 0;
    debugLog("Tomorrow prices cached; next fetch deferred until local 16:00.");
  } else {
    g_app.nextAllowedFetchMs = millis() + 15UL * 60UL * 1000UL;
  }

  return g_app.cacheCount;
}
