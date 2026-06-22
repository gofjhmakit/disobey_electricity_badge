#include "weather_client.h"
#include "app_state.h"
#include "config.h"
#include "debug_log.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>

bool fetchWeatherForecast() {
  debugLog("fetchWeatherForecast: starting");
  g_app.lastWeatherFetchMs = millis();
  HTTPClient client;
  String url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude=62.1333&longitude=25.6667";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,wind_speed_10m_max";
  url += "&timezone=Europe%2FHelsinki&forecast_days=7";
  logNetworkRequest("WEATHER", url.c_str());

  client.begin(url);
  client.setTimeout(15000);
  client.useHTTP10(true);
  client.addHeader("Accept", "application/json");
  client.addHeader("Accept-Encoding", "identity");
  int rc = client.GET();
  if (rc != 200) {
    client.end();
    logNetworkResponse("WEATHER", rc, 0);
    Serial.printf("Weather HTTP failed: %d\n", rc);
    String payload = client.getString();
    if (payload.length() > 0) {
      Serial.print("Weather payload begin: ");
      Serial.println(payload.substring(0, min((size_t)200, payload.length())));
    }
    return false;
  }

  String payload = client.getString();
  client.end();
  logNetworkResponse("WEATHER", rc, payload.length());
  DynamicJsonDocument doc(12288);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    debugLog("Weather JSON parse failed:", error.c_str());
    Serial.print("Weather payload begin: ");
    Serial.println(payload.substring(0, min((size_t)200, payload.length())));
    return false;
  }

  JsonObject daily = doc["daily"];
  JsonArray dates = daily["time"].as<JsonArray>();
  JsonArray codes = daily["weather_code"].as<JsonArray>();
  JsonArray maxTemps = daily["temperature_2m_max"].as<JsonArray>();
  JsonArray minTemps = daily["temperature_2m_min"].as<JsonArray>();
  JsonArray precipitation = daily["precipitation_sum"].as<JsonArray>();
  JsonArray precipitationProbability = daily["precipitation_probability_max"].as<JsonArray>();
  JsonArray windSpeed = daily["wind_speed_10m_max"].as<JsonArray>();

  g_app.weatherCount = 0;
  for (int i = 0; i < MAX_WEATHER_DAYS && i < dates.size(); ++i) {
    const char *date = dates[i].as<const char *>();
    if (!date) {
      continue;
    }
    strncpy(g_app.weatherDays[g_app.weatherCount].date, date,
            sizeof(g_app.weatherDays[g_app.weatherCount].date) - 1);
    g_app.weatherDays[g_app.weatherCount].date[sizeof(g_app.weatherDays[g_app.weatherCount].date) - 1] = '\0';
    g_app.weatherDays[g_app.weatherCount].weatherCode = codes[i] | 0;
    g_app.weatherDays[g_app.weatherCount].tempMax = maxTemps[i] | 0.0f;
    g_app.weatherDays[g_app.weatherCount].tempMin = minTemps[i] | 0.0f;
    g_app.weatherDays[g_app.weatherCount].precipitation = precipitation[i] | 0.0f;
    g_app.weatherDays[g_app.weatherCount].precipitationProbability = precipitationProbability[i] | 0;
    g_app.weatherDays[g_app.weatherCount].windSpeed = windSpeed[i] | 0.0f;
    g_app.weatherCount++;
  }

  debugLog("Weather days parsed:", g_app.weatherCount);
  return g_app.weatherCount > 0;
}
