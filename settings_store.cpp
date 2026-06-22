#include "settings_store.h"
#include "app_state.h"
#include "config.h"
#include "debug_log.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

void loadDefaults() {
  g_app.settings.ssid = "x";
  g_app.settings.password = "x";
  g_app.settings.area = "FI";
  g_app.settings.thresholdCheap = 3;
  g_app.settings.thresholdModerate = 7;
  g_app.settings.ledBrightness = 3;
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

  g_app.settings.ssid = doc["ssid"].as<String>();
  g_app.settings.password = doc["password"].as<String>();
  g_app.settings.area = doc["area"].as<String>();
  g_app.settings.thresholdCheap = doc["thresholdCheap"] | 2.5;
  g_app.settings.thresholdModerate = doc["thresholdModerate"] | 4.5;
  g_app.settings.ledBrightness = doc["ledBrightness"] | 3;
  if (g_app.settings.area.length() == 0) {
    g_app.settings.area = "SE3";
  }
  return true;
}

void saveSettings() {
  debugLog("Saving settings to file...");
  DynamicJsonDocument doc(2048);
  doc["ssid"] = g_app.settings.ssid;
  doc["password"] = g_app.settings.password;
  doc["area"] = g_app.settings.area;
  doc["thresholdCheap"] = g_app.settings.thresholdCheap;
  doc["thresholdModerate"] = g_app.settings.thresholdModerate;
  doc["ledBrightness"] = g_app.settings.ledBrightness;

  fs::File file = LittleFS.open(SETTINGS_FILE, FILE_WRITE);
  if (!file) {
    debugLog("Failed to write settings file");
    return;
  }
  serializeJson(doc, file);
  file.close();
  debugLog("Settings saved.");
}

void changeThreshold(int index, float delta) {
  if (index == 0) {
    g_app.settings.thresholdCheap = max(0.0f, g_app.settings.thresholdCheap + delta);
    if (g_app.settings.thresholdCheap > g_app.settings.thresholdModerate) {
      g_app.settings.thresholdModerate = g_app.settings.thresholdCheap + 0.5;
    }
  } else if (index == 1) {
    g_app.settings.thresholdModerate = max(g_app.settings.thresholdCheap + 0.1f,
                                             g_app.settings.thresholdModerate + delta);
  } else if (index == 2) {
    int level = g_app.settings.ledBrightness + (delta > 0 ? 1 : -1);
    g_app.settings.ledBrightness = constrain(level, 0, 5);
  }
  saveSettings();
}
