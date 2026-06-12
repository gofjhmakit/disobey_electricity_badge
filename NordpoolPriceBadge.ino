#include <WiFi.h>
#include <HTTPClient.h>
#include <LovyanGFX.hpp>
#include <FastLED.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "LGFX_Config.h"

#define TFT_BL 19

#define LED_PIN       18
#define LED_ACTIVATE_PIN 17
#define LED_AMOUNT    10

#define BTN_UP        11
#define BTN_DOWN      1
#define BTN_LEFT      21
#define BTN_RIGHT     2
#define BTN_STICK     14
#define BTN_A         13
#define BTN_B         38
#define BTN_START     12
#define BTN_SELECT    45

#define CACHE_FILE    "/price_cache.json"
#define SETTINGS_FILE "/badge_settings.json"

#define MAX_PRICE_POINTS  72
#define MAX_BAR_COUNT     24

LGFX tft;
CRGB leds[LED_AMOUNT];

void debugLog(const char *msg) {
  Serial.println(msg);
}

void debugLog(const char *msg, const char *value) {
  Serial.print(msg);
  Serial.println(value);
}

void debugLog(const char *msg, int value) {
  Serial.print(msg);
  Serial.println(value);
}

void debugLog(const char *msg, unsigned long value) {
  Serial.print(msg);
  Serial.println(value);
}

void debugLog(const char *msg, float value) {
  Serial.print(msg);
  Serial.println(value);
}

struct HourItem {
  int year;
  int month;
  int day;
  int hour;
  float price;
};

struct BadgeSettings {
  String ssid;
  String password;
  String area;
  float thresholdCheap;
  float thresholdModerate;
  uint8_t ledBrightness;
};

struct ButtonState {
  uint8_t pin;
  bool currentState;
  bool lastReportedState;
  bool last;
  unsigned long lastDebounce;
};

BadgeSettings settings;
HourItem cacheItems[MAX_PRICE_POINTS];
int cacheCount = 0;

ButtonState buttons[] = {
  {BTN_UP, false, false, false, 0},
  {BTN_DOWN, false, false, false, 0},
  {BTN_LEFT, false, false, false, 0},
  {BTN_RIGHT, false, false, false, 0},
  {BTN_STICK, false, false, false, 0},
  {BTN_A, false, false, false, 0},
  {BTN_B, false, false, false, 0},
  {BTN_START, false, false, false, 0},
  {BTN_SELECT, false, false, false, 0},
};

bool showSettings = false;
int settingsIndex = 0;
int selectedDayOffset = 0;
unsigned long lastRefreshMs = 0;
unsigned long lastFetchMs = 0;
unsigned long nextAllowedFetchMs = 0;
unsigned long rateLimitUntilMs = 0;
unsigned long lastInteractionMs = 0;
bool powerSaveActive = false;
const unsigned long POWER_SAVE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
const uint8_t POWER_SAVE_LED_BRIGHTNESS = 10;
bool gotTime = false;

bool timeReached(unsigned long deadline) {
  return (long)(millis() - deadline) >= 0;
}

unsigned long getNextLocalPublishMs(int publishHour, bool allowNow = false) {
  time_t now = time(nullptr);
  if (now == 0) {
    return millis() + 24UL * 60UL * 60UL * 1000UL;
  }
  tm local;
  localtime_r(&now, &local);
  local.tm_hour = publishHour;
  local.tm_min = 0;
  local.tm_sec = 0;
  time_t target = mktime(&local);
  if (target == (time_t)-1) {
    return millis() + 24UL * 60UL * 60UL * 1000UL;
  }
  if (target <= now) {
    if (allowNow) {
      return millis();
    }
    target += 24UL * 60UL * 60UL;
  }
  long deltaSeconds = (long)(target - now);
  return millis() + (unsigned long)deltaSeconds * 1000UL;
}

unsigned long getNextDayForwardCheckMs() {
  time_t now = time(nullptr);
  if (now == 0) {
    return millis() + 15UL * 60UL * 1000UL;
  }
  tm local;
  localtime_r(&now, &local);
  if (local.tm_hour < 16) {
    return getNextLocalPublishMs(16, true);
  }
  return millis() + 15UL * 60UL * 1000UL;
}

bool hasPricesForDate(time_t target) {
  if (target == 0) {
    return false;
  }
  tm targetTm;
  localtime_r(&target, &targetTm);
  int count = 0;
  for (int i = 0; i < cacheCount; ++i) {
    if (cacheItems[i].year == targetTm.tm_year + 1900 &&
        cacheItems[i].month == targetTm.tm_mon + 1 &&
        cacheItems[i].day == targetTm.tm_mday) {
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

bool shouldAttemptFetch() {
  return timeReached(nextAllowedFetchMs) && timeReached(rateLimitUntilMs);
}

bool needsFetchForTomorrow() {
  return !hasTomorrowPrices();
}

void setupPins() {
  pinMode(LED_ACTIVATE_PIN, OUTPUT);
  digitalWrite(LED_ACTIVATE_PIN, LOW);

  for (auto &button : buttons) {
    pinMode(button.pin, INPUT_PULLUP);
  }
}

void loadDefaults() {
  settings.ssid = "x";
  settings.password = "x";
  settings.area = "FI";
  settings.thresholdCheap = 3;
  settings.thresholdModerate = 7;
  settings.ledBrightness = 3;
}

bool loadSettings() {
  debugLog("Checking for settings file...");
  if (!LittleFS.exists(SETTINGS_FILE)) {
    debugLog("Settings file not found, loading defaults.");
    loadDefaults();
    return false;
  }

  fs::File file = LittleFS.open(SETTINGS_FILE, FILE_READ);
  if (!file) {
    debugLog("Failed to open settings file, loading defaults.");
    loadDefaults();
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    debugLog("Settings JSON parse failed:", error.c_str());
    loadDefaults();
    return false;
  }
  debugLog("Settings loaded from file.");

  settings.ssid = doc["ssid"].as<String>();
  settings.password = doc["password"].as<String>();
  settings.area = doc["area"].as<String>();
  settings.thresholdCheap = doc["thresholdCheap"] | 2.5;
  settings.thresholdModerate = doc["thresholdModerate"] | 4.5;
  settings.ledBrightness = doc["ledBrightness"] | 3;
  if (settings.area.length() == 0) {
    settings.area = "SE3";
  }
  return true;
}

void saveSettings() {
  debugLog("Saving settings to file...");
  DynamicJsonDocument doc(2048);
  doc["ssid"] = settings.ssid;
  doc["password"] = settings.password;
  doc["area"] = settings.area;
  doc["thresholdCheap"] = settings.thresholdCheap;
  doc["thresholdModerate"] = settings.thresholdModerate;
  doc["ledBrightness"] = settings.ledBrightness;

  fs::File file = LittleFS.open(SETTINGS_FILE, FILE_WRITE);
  if (!file) {
    debugLog("Failed to write settings file");
    return;
  }
  serializeJson(doc, file);
  file.close();
  debugLog("Settings saved.");
}

bool saveCache() {
  debugLog("Saving price cache...");
  DynamicJsonDocument fileDoc(8192);
  fileDoc["area"] = settings.area;
  fileDoc["timestamp"] = time(nullptr);
  JsonArray priceArray = fileDoc.createNestedArray("prices");
  for (int i = 0; i < cacheCount; ++i) {
    JsonObject item = priceArray.createNestedObject();
    item["year"] = cacheItems[i].year;
    item["month"] = cacheItems[i].month;
    item["day"] = cacheItems[i].day;
    item["hour"] = cacheItems[i].hour;
    item["price"] = cacheItems[i].price;
  }

  fs::File file = LittleFS.open(CACHE_FILE, FILE_WRITE);
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
  if (!LittleFS.exists(CACHE_FILE)) {
    debugLog("Cache file not found.");
    return false;
  }
  fs::File file = LittleFS.open(CACHE_FILE, FILE_READ);
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
  String cachedArea = doc["area"].as<const char*>();
  debugLog("Cached area:", cachedArea.c_str());
  if (cachedArea != settings.area) {
    debugLog("Cache area mismatch, ignoring cache.");
    return false;
  }
  JsonArray priceArray = doc["prices"].as<JsonArray>();
  cacheCount = 0;
  for (JsonObject item : priceArray) {
    if (cacheCount >= MAX_PRICE_POINTS) break;
    cacheItems[cacheCount++] = {
      item["year"].as<int>(),
      item["month"].as<int>(),
      item["day"].as<int>(),
      item["hour"].as<int>(),
      item["price"].as<float>()
    };
  }
  debugLog("Cache loaded entries:", cacheCount);
  return cacheCount > 0;
}

bool sameDate(const tm &lhs, const tm &rhs) {
  return lhs.tm_year == rhs.tm_year && lhs.tm_mon == rhs.tm_mon && lhs.tm_mday == rhs.tm_mday;
}

int findDayRange(int offset, int &startIndex, int &count) {
  time_t now = time(nullptr);
  time_t target = now + offset * 86400;
  tm targetTm;
  localtime_r(&target, &targetTm);
  startIndex = -1;
  count = 0;

  for (int i = 0; i < cacheCount; ++i) {
    tm itemTm;
    itemTm.tm_year = cacheItems[i].year - 1900;
    itemTm.tm_mon = cacheItems[i].month - 1;
    itemTm.tm_mday = cacheItems[i].day;
    if (sameDate(itemTm, targetTm)) {
      if (startIndex < 0) {
        startIndex = i;
      }
      count++;
    }
  }
  return count;
}

int fetchNordpoolPrices() {
  debugLog("fetchNordpoolPrices: starting");
  HTTPClient client;

  cacheCount = 0;

  String urls[] = {
    "https://api.spot-hinta.fi/Today?priceResolution=60",
    "https://api.spot-hinta.fi/DayForward?priceResolution=60"
  };

  for (int urlIdx = 0; urlIdx < 2; ++urlIdx) {
    String url = urls[urlIdx];
    debugLog("Fetching prices from:", url.c_str());

    client.begin(url);
    int rc = client.GET();
    if (rc != 200) {
      Serial.printf("HTTP failed: %d\n", rc);
      if (rc == 404) {
        debugLog("Remote API returned 404: prices not available yet.");
        client.end();
        if (urlIdx == 0) {
          continue;
        }
        nextAllowedFetchMs = getNextDayForwardCheckMs();
        rateLimitUntilMs = 0;
        return cacheCount;
      }
      if (rc == 429) {
        debugLog("Remote API returned 429: too many requests.");
        rateLimitUntilMs = millis() + 60UL * 60UL * 1000UL;
        nextAllowedFetchMs = rateLimitUntilMs;
        client.end();
        return -1;
      }
      client.end();
      if (urlIdx == 0) {
        continue;
      }
      return -1;
    }

    String payload = client.getString();
    client.end();
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
        nextAllowedFetchMs = getNextDayForwardCheckMs();
      if (!dateTime) continue;

      int year, month, day, hour;
      if (sscanf(dateTime, "%d-%d-%dT%d:", &year, &month, &day, &hour) != 4) {
        continue;
      }

      cacheItems[cacheCount++] = {year, month, day, hour, price};
    }
  }

  debugLog("Parsed Nordpool price items:", cacheCount);
  if (cacheCount > 0) {
    if (saveCache()) {
      debugLog("Saved fetched prices to cache.");
    } else {
      debugLog("Failed to save fetched prices to cache.");
    }
  }

  if (hasTomorrowPrices()) {
    nextAllowedFetchMs = getNextLocalPublishMs(16);
    rateLimitUntilMs = 0;
    debugLog("Tomorrow prices cached; next fetch deferred until local 16:00.");
  } else {
    nextAllowedFetchMs = millis() + 15UL * 60UL * 1000UL;
  }

  return cacheCount;
}

void drawHeader(const String &subtitle) {
  tft.fillRect(0, 0, 320, 24, TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setCursor(4, 4);
  tft.print("Nordpool Price Badge");
  tft.setCursor(4, 16);
  tft.print(subtitle);
}

void drawFooter(const String &line1, const String &line2) {
  tft.fillRect(0, 150, 320, 20, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setCursor(4, 152);
  tft.print(line1);
  tft.setCursor(4, 162);
  tft.print(line2);
}

CRGB colorForValue(float price) {
  if (price <= settings.thresholdCheap) {
    return CRGB::Lime;
  }
  if (price <= settings.thresholdModerate) {
    return CRGB::Orange;
  }
  return CRGB::Red;
}

void updateLEDsForHour(float currentPrice, float nextPrice) {
  if (settings.ledBrightness == 0) {
    digitalWrite(LED_ACTIVATE_PIN, LOW);
    FastLED.clear(true);
    return;
  }

  digitalWrite(LED_ACTIVATE_PIN, HIGH);
  uint8_t scaledBrightness = (uint8_t)(255.0f * pow((float)settings.ledBrightness / 5.0f, 2.0f));
  FastLED.setBrightness(scaledBrightness);
  CRGB leftColor = colorForValue(currentPrice);
  CRGB rightColor = colorForValue(nextPrice);

  // The LED array is wired so the first 5 LEDs appear on the right side of the screen
  // and the last 5 LEDs appear on the left side. Swap the halves so the left block
  // shows the current hour and the right block shows the next hour.
  for (int i = 0; i < LED_AMOUNT; ++i) {
    if (i < LED_AMOUNT / 2) {
      leds[i] = rightColor;
    } else {
      leds[i] = leftColor;
    }
  }
  FastLED.show();
}

void updatePowerSaveLEDs(float currentPrice, float nextPrice) {
  digitalWrite(LED_ACTIVATE_PIN, HIGH);
  FastLED.setBrightness(POWER_SAVE_LED_BRIGHTNESS);
  for (int i = 0; i < LED_AMOUNT; ++i) {
    leds[i] = CRGB::Black;
  }

  CRGB currentColor = colorForValue(currentPrice);
  CRGB nextColor = colorForValue(nextPrice);

  // Use the central LED of each side for minimal power indication.
  leds[7] = currentColor; // left side center
  leds[2] = nextColor;    // right side center
  FastLED.show();
}

void enterPowerSaveMode() {
  if (powerSaveActive) {
    return;
  }
  powerSaveActive = true;
  digitalWrite(TFT_BL, LOW);

  if (settings.ledBrightness == 0) {
    digitalWrite(LED_ACTIVATE_PIN, LOW);
    FastLED.clear(true);
    return;
  }

  int startIndex = -1;
  int count = 0;
  float currentPrice = 0;
  float nextPrice = 0;
  if (findDayRange(selectedDayOffset, startIndex, count) > 0) {
    time_t now = time(nullptr);
    tm nowTm;
    localtime_r(&now, &nowTm);
    currentPrice = cacheItems[startIndex].price;
    nextPrice = currentPrice;
    for (int i = startIndex; i < startIndex + count; ++i) {
      if (selectedDayOffset == 0 && cacheItems[i].hour == nowTm.tm_hour) {
        currentPrice = cacheItems[i].price;
        if (i + 1 < startIndex + count) {
          nextPrice = cacheItems[i + 1].price;
        }
        break;
      }
    }
    updatePowerSaveLEDs(currentPrice, nextPrice);
  } else {
    FastLED.clear(true);
  }
}

void exitPowerSaveMode() {
  if (!powerSaveActive) {
    return;
  }
  powerSaveActive = false;
  digitalWrite(TFT_BL, HIGH);
  if (showSettings) {
    drawSettingsScreen();
  } else {
    drawMainScreen();
  }
}

void registerInteraction() {
  lastInteractionMs = millis();
  if (powerSaveActive) {
    exitPowerSaveMode();
  }
}

bool consumeAnyButtonPressEvent() {
  for (auto &button : buttons) {
    if (button.currentState && !button.lastReportedState) {
      button.lastReportedState = true;
      return true;
    }
  }
  return false;
}

bool anyButtonIsPressed() {
  for (auto &button : buttons) {
    if (button.currentState) {
      return true;
    }
  }
  return false;
}

void drawMainScreen() {
  debugLog("Drawing main screen...");
  tft.fillScreen(TFT_BLACK);

  tm timeInfo;
  getLocalTime(&timeInfo);
  String dayLabel = selectedDayOffset == 0 ? "Today" : "Tomorrow";
  drawHeader("Wide hourly overview - " + dayLabel);

  char timeText[32];
  sprintf(timeText, "%02d:%02d %04d-%02d-%02d", timeInfo.tm_hour, timeInfo.tm_min,
          timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(4, 28);
  tft.print(timeText);

  int startIndex = -1;
  int count = 0;
  findDayRange(selectedDayOffset, startIndex, count);
  if (count <= 0) {
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(4, 90);
    tft.print("No data for selected day.");
    drawFooter("Press B to refresh.", "Left/Right to change day.");
    digitalWrite(LED_ACTIVATE_PIN, LOW);
    FastLED.clear(true);
    return;
  }

  float cheapest = 1e6;
  float expensive = -1e6;
  float sum = 0;
  for (int i = startIndex; i < startIndex + count; ++i) {
    float value = cacheItems[i].price;
    cheapest = min(cheapest, value);
    expensive = max(expensive, value);
    sum += value;
  }
  float average = sum / count;

  int chartX = 12;
  int chartY = 60;
  int chartW = 296;
  int chartH = 80;
  tft.drawRect(chartX - 2, chartY - 2, chartW + 4, chartH + 4, TFT_WHITE);

  float range = max(0.05f, expensive - cheapest);
  
  // Draw price grid lines and labels
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF);  // Dim gray
  for (int line = 1; line <= 3; ++line) {
    float priceAtLine = cheapest + (range * line / 3.0f);
    int lineY = chartY + chartH - (int)(range > 0 ? (priceAtLine - cheapest) / range * (chartH - 8) : 0);
    // Draw horizontal line
    for (int dx = 0; dx < chartW; dx += 4) {
      tft.drawPixel(chartX + dx, lineY, 0x7BEF);
      tft.drawPixel(chartX + dx + 1, lineY, 0x7BEF);
    }
    // Draw price label
    tft.setCursor(chartX - 28, lineY - 3);
    tft.printf("%.1f c", priceAtLine);
  }
  
  int barCount = min(count, MAX_BAR_COUNT);
  int barWidth = max(8, chartW / barCount);
  int x = chartX;
  int currentBarX = -1;

  for (int i = startIndex; i < startIndex + barCount && i < cacheCount; ++i) {
    float value = cacheItems[i].price;
    int barHeight = (int)((value - cheapest) / range * (chartH - 8));
    int by = chartY + chartH - barHeight;
    CRGB rgb = colorForValue(value);
    uint16_t color = tft.color565(rgb.r, rgb.g, rgb.b);
    tft.fillRect(x, by, barWidth - 2, barHeight, color);
    if (selectedDayOffset == 0 && cacheItems[i].hour == timeInfo.tm_hour) {
      tft.drawRect(x - 1, by - 1, barWidth, barHeight + 2, TFT_WHITE);
    }
    if (cacheItems[i].hour % 6 == 0) {
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(x, chartY + chartH + 2);
      tft.printf("%02d", cacheItems[i].hour);
    }
    x += barWidth;
  }

  String lowHighText = "Low: " + String(cheapest, 2) + " c  High: " + String(expensive, 2) + " c";
  String averageText = "Avg: " + String(average, 2) + " c/kWh";
  drawFooter(lowHighText, averageText);

  float currentPrice = cacheItems[startIndex].price;
  float nextPrice = currentPrice;
  bool currentFound = false;
  for (int i = startIndex; i < startIndex + count; ++i) {
    if (selectedDayOffset == 0 && cacheItems[i].hour == timeInfo.tm_hour) {
      currentPrice = cacheItems[i].price;
      if (i + 1 < startIndex + count) {
        nextPrice = cacheItems[i + 1].price;
      }
      currentFound = true;
      break;
    }
  }
  if (!currentFound && count > 1) {
    currentPrice = cacheItems[startIndex].price;
    nextPrice = cacheItems[startIndex + 1].price;
  }
  updateLEDsForHour(currentPrice, nextPrice);
}

void drawSettingsScreen() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Threshold settings");
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(4, 40);
  tft.print("Cheap <=");
  tft.print(settings.thresholdCheap, 2);
  tft.setCursor(4, 70);
  tft.print("Moderate <=");
  tft.print(settings.thresholdModerate, 2);
  tft.setCursor(4, 100);
  tft.print("Brightness: ");
  tft.print(settings.ledBrightness);

  int y = 40 + settingsIndex * 30;
  tft.drawRect(2, y - 2, 316, 26, TFT_YELLOW);
  drawFooter("Up/Down to choose value.", "Left/Right to change.");
}

void drawConfigMode() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Config AP Mode");
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(4, 40);
  tft.print("SSID: NordpoolBadge_Config");
  tft.setCursor(4, 70);
  tft.print("No WiFi credentials");
  tft.setCursor(4, 100);
  tft.print("Use Serial or save settings");
  drawFooter("Set SSID/password via code.", "Restart after updating.");
}

void showDayHint() {
  String dayLabel = selectedDayOffset == 0 ? "Today" : "Tomorrow";
  tft.setCursor(220, 4);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.print(dayLabel);
}

bool connectWiFi() {
  if (settings.ssid.length() == 0) {
    debugLog("WiFi credentials not provided.");
    return false;
  }
  debugLog("Starting WiFi connection...");
  debugLog("SSID:", settings.ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
  unsigned long deadline = millis() + 12000;
  while (millis() < deadline && WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();
  bool connected = WiFi.status() == WL_CONNECTED;
  debugLog("WiFi connected:", connected ? "yes" : "no");
  if (connected) {
    debugLog("Local IP:", WiFi.localIP().toString().c_str());
  }
  return connected;
}

bool waitForLocalTime(unsigned long timeoutMs) {
  unsigned long deadline = millis() + timeoutMs;
  tm timeInfo;
  while (millis() < deadline) {
    if (getLocalTime(&timeInfo)) {
      return true;
    }
    delay(250);
  }
  return false;
}

void setup() {
  pinMode(LED_ACTIVATE_PIN, OUTPUT);
  digitalWrite(LED_ACTIVATE_PIN, HIGH);

  Serial.begin(115200, SERIAL_8N1);
  Serial.setDebugOutput(true);
  delay(500);

  Serial.println("Display init done");
  tft.fillScreen(TFT_RED);
  delay(1000);
  tft.fillScreen(TFT_GREEN);
  delay(1000);
  tft.fillScreen(TFT_BLUE);

  delay(1000);
  Serial.println("\n--- NordpoolPriceBadge startup ---");
  Serial.flush();
  delay(100);

  Serial.println("Initializing LittleFS...");
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
  } else {
    Serial.println("LittleFS mounted");
  }

  loadDefaults();
  loadSettings();
  debugLog("Settings after load:");
  debugLog("  SSID:", settings.ssid.length() ? settings.ssid.c_str() : "<empty>");
  debugLog("  Area:", settings.area.c_str());
  debugLog("  Cheap threshold:", settings.thresholdCheap);
  debugLog("  Moderate threshold:", settings.thresholdModerate);
  debugLog("  LED brightness:", settings.ledBrightness);
  setupPins();

  Serial.println("Initializing display...");
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  Serial.println("Initializing LEDs...");
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_AMOUNT);
  uint8_t initialBrightness = (uint8_t)(255.0f * pow(1.0f / 5.0f, 2.0f));
  FastLED.setBrightness(initialBrightness);
  for (int i = 0; i < LED_AMOUNT; ++i) {
    leds[i] = CRGB::SkyBlue;
  }
  FastLED.show();

  bool hadCache = loadCache();
  Serial.printf("Cache loaded: %d entries\n", cacheCount);
  if (hadCache && hasTomorrowPrices()) {
    nextAllowedFetchMs = getNextLocalPublishMs(16);
    debugLog("Tomorrow prices already cached at startup; deferring next fetch until local 16:00.");
  }

  if (connectWiFi()) {
    configTzTime("EET-2EEST,M3.5.0/03:00,M10.5.0/04:00", "pool.ntp.org", "time.google.com");
    gotTime = waitForLocalTime(15000);
    Serial.printf("NTP time available: %s\n", gotTime ? "yes" : "no");
    debugLog("WiFi connected successfully.");
    if (!gotTime) {
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
  } else {
    debugLog("WiFi connection failed.");
    if (!hadCache) {
      debugLog("No WiFi and no cache, entering config mode.");
      drawConfigMode();
      return;
    }
  }

  if (cacheCount > 0) {
    Serial.println("Displaying main screen");
    drawMainScreen();
    // Ensure LEDs are refreshed after startup and after the first data display.
    if (!powerSaveActive) {
      time_t now = time(nullptr);
      tm nowTm;
      localtime_r(&now, &nowTm);
      int startIndex = -1;
      int count = 0;
      if (findDayRange(selectedDayOffset, startIndex, count) > 0) {
        float currentPrice = cacheItems[startIndex].price;
        float nextPrice = currentPrice;
        for (int i = startIndex; i < startIndex + count; ++i) {
          if (selectedDayOffset == 0 && cacheItems[i].hour == nowTm.tm_hour) {
            currentPrice = cacheItems[i].price;
            if (i + 1 < startIndex + count) {
              nextPrice = cacheItems[i + 1].price;
            }
            break;
          }
        }
        updateLEDsForHour(currentPrice, nextPrice);
      }
    }
  } else {
    Serial.println("No cached data available, config mode");
    drawConfigMode();
  }
  lastInteractionMs = millis();
}

void updateButtonStates() {
  unsigned long now = millis();
  for (auto &button : buttons) {
    bool reading = digitalRead(button.pin) == LOW;
    if (reading != button.last) {
      button.lastDebounce = now;
    }
    if ((now - button.lastDebounce) > 50) {
      button.currentState = reading;
    }
    button.last = reading;
  }
}

bool buttonPressedOnce(uint8_t pin) {
  for (auto &button : buttons) {
    if (button.pin == pin) {
      if (button.currentState && !button.lastReportedState) {
        button.lastReportedState = true;
        return true;
      }
      if (!button.currentState) {
        button.lastReportedState = false;
      }
      return false;
    }
  }
  return false;
}

void changeThreshold(int index, float delta) {
  if (index == 0) {
    settings.thresholdCheap = max(0.0f, settings.thresholdCheap + delta);
    if (settings.thresholdCheap > settings.thresholdModerate) {
      settings.thresholdModerate = settings.thresholdCheap + 0.5;
    }
  } else if (index == 1) {
    settings.thresholdModerate = max(settings.thresholdCheap + 0.1f, settings.thresholdModerate + delta);
  } else if (index == 2) {
    int level = settings.ledBrightness + (delta > 0 ? 1 : -1);
    settings.ledBrightness = constrain(level, 0, 5);
  }
  saveSettings();
}

void loop() {
  updateButtonStates();

  if (anyButtonIsPressed()) {
    lastInteractionMs = millis();
  }

  if (powerSaveActive && consumeAnyButtonPressEvent()) {
    registerInteraction();
    return;
  }

  if (buttonPressedOnce(BTN_SELECT)) {
    debugLog("BTN_SELECT pressed: cycling brightness.");
    settings.ledBrightness = (settings.ledBrightness + 1) % 6;
    saveSettings();
    drawMainScreen();
  }

  if (buttonPressedOnce(BTN_START)) {
    debugLog("BTN_START pressed: toggling settings screen.");
    showSettings = !showSettings;
    settingsIndex = 0;
    if (showSettings) {
      drawSettingsScreen();
    } else {
      drawMainScreen();
    }
  }

  if (showSettings) {
    if (buttonPressedOnce(BTN_UP)) {
      debugLog("BTN_UP pressed: previous settings option.");
      settingsIndex = max(0, settingsIndex - 1);
      drawSettingsScreen();
    }
    if (buttonPressedOnce(BTN_DOWN)) {
      debugLog("BTN_DOWN pressed: next settings option.");
      settingsIndex = min(2, settingsIndex + 1);
      drawSettingsScreen();
    }
    if (buttonPressedOnce(BTN_LEFT)) {
      debugLog("BTN_LEFT pressed: decrease threshold value.");
      changeThreshold(settingsIndex, settingsIndex == 2 ? -1 : -0.1);
      drawSettingsScreen();
    }
    if (buttonPressedOnce(BTN_RIGHT)) {
      debugLog("BTN_RIGHT pressed: increase threshold value.");
      changeThreshold(settingsIndex, settingsIndex == 2 ? 1 : 0.1);
      drawSettingsScreen();
    }
    if (buttonPressedOnce(BTN_A) || buttonPressedOnce(BTN_B)) {
      debugLog("BTN_A or BTN_B pressed: exit settings.");
      showSettings = false;
      drawMainScreen();
    }
    return;
  }

  if (buttonPressedOnce(BTN_LEFT)) {
    debugLog("BTN_LEFT pressed: previous day.");
    selectedDayOffset = max(0, selectedDayOffset - 1);
    drawMainScreen();
  }
  if (buttonPressedOnce(BTN_RIGHT)) {
    debugLog("BTN_RIGHT pressed: next day.");
    selectedDayOffset++;
    drawMainScreen();
  }
  if (buttonPressedOnce(BTN_STICK)) {
    debugLog("BTN_STICK pressed: toggle today/tomorrow view.");
    selectedDayOffset = (selectedDayOffset == 0) ? 1 : 0;
    drawMainScreen();
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
      drawMainScreen();
    } else {
      tft.fillRect(0, 90, 320, 30, TFT_BLACK);
      tft.setTextColor(TFT_YELLOW);
      tft.setTextSize(2);
      tft.setCursor(4, 92);
      tft.print("Unable to refresh data.");
    }
  }
  if (buttonPressedOnce(BTN_A)) {
    debugLog("BTN_A pressed: redraw main screen.");
    drawMainScreen();
  }

  unsigned long now = millis();
  if (!powerSaveActive && now - lastInteractionMs >= POWER_SAVE_TIMEOUT_MS) {
    enterPowerSaveMode();
  }

  if (now - lastFetchMs > 15UL * 60UL * 1000UL) {
    if (WiFi.status() == WL_CONNECTED && needsFetchForTomorrow() && shouldAttemptFetch()) {
      if (fetchNordpoolPrices() > 0) {
        if (!powerSaveActive) {
          drawMainScreen();
        } else {
          enterPowerSaveMode();
        }
      }
    }
    lastFetchMs = now;
  }
}
