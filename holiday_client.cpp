#include "holiday_client.h"
#include "app_state.h"
#include "config.h"
#include "debug_log.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

namespace {
bool fetchHolidayYear(int year, HolidayItem *items, int &count, int maxItems) {
  String url = "https://date.nager.at/api/v3/PublicHolidays/" + String(year) + "/FI";
  logNetworkRequest("HOLIDAY", url.c_str());

  HTTPClient client;
  client.begin(url);
  client.setTimeout(15000);
  client.addHeader("Accept", "application/json");
  client.addHeader("Accept-Encoding", "identity");
  int rc = client.GET();
  if (rc != 200) {
    client.end();
    logNetworkResponse("HOLIDAY", rc, 0);
    return false;
  }

  String payload = client.getString();
  client.end();
  logNetworkResponse("HOLIDAY", rc, payload.length());

  DynamicJsonDocument doc(24576);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    debugLog("Holiday JSON parse failed:", error.c_str());
    return false;
  }

  JsonArray holidays = doc.as<JsonArray>();
  if (holidays.isNull()) {
    return false;
  }

  for (JsonObject holiday : holidays) {
    if (count >= maxItems) {
      break;
    }
    const char *date = holiday["date"] | "";
    const char *localName = holiday["localName"] | "";
    if (!date || strlen(date) != 10 || !localName || localName[0] == '\0') {
      continue;
    }

    strncpy(items[count].date, date, sizeof(items[count].date) - 1);
    items[count].date[sizeof(items[count].date) - 1] = '\0';
    strncpy(items[count].localName, localName, sizeof(items[count].localName) - 1);
    items[count].localName[sizeof(items[count].localName) - 1] = '\0';
    count++;
  }

  return true;
}
}

bool fetchFinlandPublicHolidays() {
  g_app.lastHolidayFetchMs = millis();

  time_t now = time(nullptr);
  if (now <= 0) {
    debugLog("Holiday fetch skipped: no local time");
    return false;
  }
  tm localNow;
  localtime_r(&now, &localNow);
  int year = localNow.tm_year + 1900;

  HolidayItem fetched[MAX_HOLIDAYS];
  int count = 0;
  bool okCurrentYear = fetchHolidayYear(year, fetched, count, MAX_HOLIDAYS);
  bool okNextYear = false;
  if (count < MAX_HOLIDAYS) {
    okNextYear = fetchHolidayYear(year + 1, fetched, count, MAX_HOLIDAYS);
  }
  if (!okCurrentYear && !okNextYear) {
    return false;
  }

  g_app.holidayCount = count;
  for (int i = 0; i < count; ++i) {
    g_app.holidays[i] = fetched[i];
  }
  debugLog("Holiday items parsed:", g_app.holidayCount);
  return g_app.holidayCount > 0;
}
