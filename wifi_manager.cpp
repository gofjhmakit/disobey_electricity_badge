#include "wifi_manager.h"
#include "app_state.h"
#include "debug_log.h"

#include <WiFi.h>

bool connectWiFi() {
  if (g_app.settings.ssid.length() == 0) {
    debugLog("WiFi credentials not provided.");
    return false;
  }
  debugLog("Starting WiFi connection...");
  debugLog("SSID:", g_app.settings.ssid.c_str());
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // Keep PHY active to avoid repeated init/teardown under HTTPS bursts.
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_app.settings.ssid.c_str(), g_app.settings.password.c_str());
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
