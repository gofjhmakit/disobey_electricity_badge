#include "stocks_client.h"
#include "app_state.h"
#include "config.h"
#include "debug_log.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>

bool fetchStocks() {
  debugLog("fetchStocks: starting");
  g_app.lastStockFetchMs = millis();

  const char *stockSymbols[] = {
    "ELISA.HE",
    "GOFORE.HE",
    "KESKOB.HE",
    "MANTA.HE",
    "NDA-FI.HE",
    "SAMPO.HE",
    "TIETO.HE",
    "^OMXHPI"
  };
  const char *stockNames[] = {
    "Elisa",
    "Gofore",
    "Kesko B",
    "Mandatum",
    "Nordea",
    "Sampo A",
    "Tieto",
    "OMXHPI"
  };

  bool anySuccess = false;
  g_app.stockCount = 0;

  for (int i = 0; i < MAX_STOCKS; ++i) {
    if (millis() < g_app.stockRateLimitUntilMs) {
      debugLog("Stock fetch rate limited, skipping remaining stocks");
      break;
    }

    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + String(stockSymbols[i]);
    url += "?interval=1d&range=1d";

    logNetworkRequest("STOCK", url.c_str());

    HTTPClient client;
    client.begin(url);
    client.setTimeout(15000);
    client.addHeader("User-Agent", "Mozilla/5.0");

    int rc = client.GET();
    String payload;

    if (rc == 200) {
      payload = client.getString();
      logNetworkResponse("STOCK", rc, payload.length());

      DynamicJsonDocument doc(8192);
      DeserializationError error = deserializeJson(doc, payload);

      if (!error && doc.containsKey("chart") && doc["chart"].containsKey("error")) {
        JsonVariant errorVal = doc["chart"]["error"];
        if (!errorVal.isNull() && errorVal.as<String>().length() > 0) {
          String apiError = errorVal.as<String>();
          debugLog("Yahoo API error for:", stockSymbols[i]);
          debugLog("  API Error:", apiError.c_str());
          client.end();
          continue;
        }
      }

      if (!error) {
        if (doc.containsKey("chart") && doc["chart"].containsKey("result")) {
          JsonArray results = doc["chart"]["result"];
          if (results.size() == 0) {
            debugLog("Empty result array for:", stockSymbols[i]);
            debugLog("  Payload start:",
                     payload.substring(0, min((size_t)200, payload.length())).c_str());
            client.end();
            continue;
          }
          if (!results[0].containsKey("meta")) {
            debugLog("Missing meta in result[0] for:", stockSymbols[i]);
            debugLog("  Payload start:",
                     payload.substring(0, min((size_t)200, payload.length())).c_str());
            client.end();
            continue;
          }
          JsonObject meta = results[0]["meta"];

          if (g_app.lastStockMarketTimestamp == 0) {
            if (meta.containsKey("regularMarketTime")) {
              g_app.lastStockMarketTimestamp = meta["regularMarketTime"] | 0;
            } else if (doc["chart"].containsKey("timestamp") &&
                       doc["chart"]["timestamp"].size() > 0) {
              g_app.lastStockMarketTimestamp = doc["chart"]["timestamp"][0] | 0;
            }
          }

          float price = 0.0f;
          float previousClose = 0.0f;

          if (meta.containsKey("regularMarketPrice")) {
            price = meta["regularMarketPrice"] | 0.0f;
          } else if (meta.containsKey("currentPrice")) {
            price = meta["currentPrice"] | 0.0f;
          } else if (meta.containsKey("lastPrice")) {
            price = meta["lastPrice"] | 0.0f;
          }

          if (meta.containsKey("previousClose")) {
            previousClose = meta["previousClose"] | 0.0f;
          } else if (meta.containsKey("regularMarketPreviousClose")) {
            previousClose = meta["regularMarketPreviousClose"] | 0.0f;
          } else if (meta.containsKey("chartPreviousClose")) {
            previousClose = meta["chartPreviousClose"] | 0.0f;
          } else if (meta.containsKey("open")) {
            previousClose = meta["open"] | 0.0f;
          }

          if (price > 0 && previousClose > 0) {
            g_app.stocks[g_app.stockCount].symbol = stockSymbols[i];
            g_app.stocks[g_app.stockCount].name = stockNames[i];
            g_app.stocks[g_app.stockCount].price = price;
            g_app.stocks[g_app.stockCount].change = price - previousClose;
            g_app.stocks[g_app.stockCount].changePercent =
              ((price - previousClose) / previousClose) * 100.0f;
            g_app.stocks[g_app.stockCount].fetched = true;
            g_app.stockCount++;
            anySuccess = true;
            debugLog("Stock fetched:", stockNames[i]);
            debugLog("  Price:", price);
            debugLog("  Change:", g_app.stocks[g_app.stockCount - 1].change);
            debugLog("  Change%:", g_app.stocks[g_app.stockCount - 1].changePercent);
          } else {
            debugLog("Invalid or missing price data for:", stockSymbols[i]);
            debugLog("  Price:", price);
            debugLog("  PreviousClose:", previousClose);
          }
        } else {
          debugLog("Missing chart.result[0].meta in response for:", stockSymbols[i]);
          if (payload.length() > 0) {
            debugLog("  Payload start:",
                     payload.substring(0, min((size_t)200, payload.length())).c_str());
          }
        }
      } else {
        debugLog("JSON parse failed for:", stockSymbols[i]);
        debugLog("  Error:", error.c_str());
      }
    } else {
      logNetworkResponse("STOCK", rc, 0);
      debugLog("HTTP failed for:", stockSymbols[i]);
      debugLog("  Code:", rc);

      if (rc == 403) {
        debugLog("Rate limited by Yahoo Finance, pausing stock fetches for 1 hour");
        g_app.stockRateLimitUntilMs = millis() + STOCK_RATE_LIMIT_MS;
        break;
      }
      if (rc == 429) {
        debugLog("Too many requests, pausing stock fetches for 1 hour");
        g_app.stockRateLimitUntilMs = millis() + STOCK_RATE_LIMIT_MS;
        break;
      }
    }
    client.end();

    delay(200);
  }

  debugLog("fetchStocks: fetched");
  debugLog("  Count:", g_app.stockCount);
  return anySuccess;
}
