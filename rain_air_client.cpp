#include "rain_air_client.h"
#include "app_state.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <vector>

static const char* forecastHost = "api.open-meteo.com";
static const char* airHost = "air-quality-api.open-meteo.com";
// Reusable static buffers to prevent heap fragmentation
static std::vector<uint8_t> rainBody;
static std::vector<uint8_t> aqBody;

bool fetchRainAndAirQuality() {
  Serial.println("[RAIN_AIR] Starting fetch...");
  WiFiClientSecure client;
  client.setInsecure();
  
  if (!client.connect(forecastHost, 443)) {
    Serial.println("[RAIN_AIR] Connect failed");
    return false;
  }
  
  // Reuse static buffer, clear but preserve allocation
  rainBody.clear();
  rainBody.reserve(6 * 1024);  // Reduced from 8KB
  
  String path = "/v1/forecast?latitude=62.2419&longitude=25.7482&hourly=precipitation_probability,precipitation&forecast_days=1";
  String req = "GET " + path + " HTTP/1.1\r\nHost: " + forecastHost + "\r\nConnection: close\r\nUser-Agent: Badge/1.0\r\n\r\n";
  client.print(req);
  
  unsigned long timeout = millis() + 5000;
  while (millis() < timeout && !client.available()) delay(10);
  
  bool headerDone = false;
  while (client.available() && !headerDone) {
    String line = client.readStringUntil('\n');
    if (line.length() <= 2) headerDone = true;
  }
  
  timeout = millis() + 5000;
  while (client.available() || millis() < timeout) {
    while (client.available()) rainBody.push_back(client.read());
    delay(10);
  }
  rainBody.push_back('\0');
  
  Serial.printf("[RAIN_AIR] Rain response: %d bytes\n", rainBody.size());
  
  g_app.rainCount = 0;
  if (rainBody.size() > 0) {
    const char* ptr = (const char*)rainBody.data();
    const char* hourlyStart = strstr(ptr, "\"hourly\":{");
    if (hourlyStart) {
      const char* precStart = strstr(hourlyStart, "\"precipitation\":[");
      const char* probStart = strstr(hourlyStart, "\"precipitation_probability\":[");
      if (!precStart) {
        Serial.println("[RAIN_AIR] precipitation array missing");
      }
      if (precStart) {
      precStart += 17;
      if (probStart) {
        probStart += 28;
      }
      for (int i = 0; i < 12 && g_app.rainCount < MAX_RAIN_SLOTS; i++) {
        float val = atof(precStart);
        g_app.rainNext60m[g_app.rainCount].precipitationMm = val;
        g_app.rainNext60m[g_app.rainCount].probabilityPercent = probStart ? atof(probStart) : 0.0f;
        g_app.rainCount++;
        precStart = strchr(precStart, ',');
        if (!precStart) break;
        precStart++;
        if (probStart) {
          probStart = strchr(probStart, ',');
          if (probStart) {
            probStart++;
          }
        }
      }
      } else {
        Serial.println("[RAIN_AIR] hourly object missing");
      }
    }
  }
  
  Serial.printf("[RAIN_AIR] Parsed %d rain slots\n", g_app.rainCount);
  
  client.stop();
  delay(500);  // Increased delay
  
  if (!client.connect(airHost, 443)) {
    Serial.println("[RAIN_AIR] Reconnect for AQ failed");
    g_app.lastRainAirFetchMs = millis();
    return g_app.rainCount > 0;
  }
  
  aqBody.clear();
  aqBody.reserve(3 * 1024);  // Reduced from 4KB
  
  path = "/v1/air-quality?latitude=62.2419&longitude=25.7482&current=us_aqi,pm2_5,pm10,nitrogen_dioxide,ozone";
  req = "GET " + path + " HTTP/1.1\r\nHost: " + airHost + "\r\nConnection: close\r\nUser-Agent: Badge/1.0\r\n\r\n";
  client.print(req);
  
  timeout = millis() + 5000;
  while (millis() < timeout && !client.available()) delay(10);
  
  headerDone = false;
  while (client.available() && !headerDone) {
    String line = client.readStringUntil('\n');
    if (line.length() <= 2) headerDone = true;
  }
  
  timeout = millis() + 5000;
  while (client.available() || millis() < timeout) {
    while (client.available()) aqBody.push_back(client.read());
    delay(10);
  }
  aqBody.push_back('\0');
  
  Serial.printf("[RAIN_AIR] AQ response: %d bytes\n", aqBody.size());
  
  client.stop();
  
  if (aqBody.size() > 0) {
    const char* ptr = (const char*)aqBody.data();
    const char* currentPtr = strstr(ptr, "\"current\":{");
    if (!currentPtr) {
      currentPtr = ptr;
      Serial.println("[RAIN_AIR] current object missing");
    }
    const char* aqiPtr = strstr(currentPtr, "\"us_aqi\":");
    if (aqiPtr) g_app.airQuality.aqiUS = atoi(aqiPtr + 9);
    const char* pm25Ptr = strstr(currentPtr, "\"pm2_5\":");
    if (pm25Ptr) g_app.airQuality.pm25 = atof(pm25Ptr + 8);
    const char* pm10Ptr = strstr(currentPtr, "\"pm10\":");
    if (pm10Ptr) g_app.airQuality.pm10 = atof(pm10Ptr + 7);
    const char* no2Ptr = strstr(currentPtr, "\"nitrogen_dioxide\":");
    if (no2Ptr) g_app.airQuality.no2 = atoi(no2Ptr + 19);
    const char* o3Ptr = strstr(currentPtr, "\"ozone\":");
    if (o3Ptr) g_app.airQuality.o3 = atoi(o3Ptr + 8);
    
    Serial.printf("[RAIN_AIR] AQI: %d, PM2.5: %.1f\n", g_app.airQuality.aqiUS, g_app.airQuality.pm25);
  }
  
  g_app.lastRainAirFetchMs = millis();
  Serial.println("[RAIN_AIR] Fetch complete");
  return true;
}
