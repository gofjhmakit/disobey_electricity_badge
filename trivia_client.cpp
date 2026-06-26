#include "trivia_client.h"
#include "app_state.h"
#include "debug_log.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>

namespace {
void compactWhitespace(String &text) {
  text.replace("\n", " ");
  text.replace("\r", " ");
  text.replace("\t", " ");
  while (text.indexOf("  ") >= 0) {
    text.replace("  ", " ");
  }
  text.trim();
}
}

bool fetchOnThisDayTrivia() {
  g_app.lastTriviaFetchMs = millis();

  time_t now = time(nullptr);
  if (now <= 0) {
    debugLog("Trivia fetch skipped: no local time");
    return false;
  }

  tm localNow;
  localtime_r(&now, &localNow);
  char mm[3];
  char dd[3];
  strftime(mm, sizeof(mm), "%m", &localNow);
  strftime(dd, sizeof(dd), "%d", &localNow);

  String url = "https://api.wikimedia.org/feed/v1/wikipedia/en/onthisday/selected/" +
               String(mm) + "/" + String(dd);
  logNetworkRequest("TRIVIA", url.c_str());

  HTTPClient client;
  client.begin(url);
  client.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  client.setTimeout(15000);
  client.addHeader("Accept", "application/json");
  client.addHeader("User-Agent", "NordpoolPriceBadge/1.0");

  int rc = client.GET();
  if (rc != 200) {
    client.end();
    logNetworkResponse("TRIVIA", rc, 0);
    return false;
  }

  StaticJsonDocument<256> filter;
  filter["selected"][0]["year"] = true;
  filter["selected"][0]["text"] = true;

  DynamicJsonDocument doc(4096);
  String payload = client.getString();
  client.end();
  logNetworkResponse("TRIVIA", rc, payload.length());
  DeserializationError error =
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    debugLog("Wikimedia trivia parse failed, trying fallback:", error.c_str());
    String fallbackUrl = "https://byabbe.se/on-this-day/" + String(localNow.tm_mon + 1) +
                         "/" + String(localNow.tm_mday) + "/events.json";
    logNetworkRequest("TRIVIA", fallbackUrl.c_str());
    HTTPClient fallback;
    fallback.begin(fallbackUrl);
    fallback.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    fallback.setTimeout(15000);
    fallback.addHeader("Accept", "application/json");
    int frc = fallback.GET();
    if (frc != 200) {
      fallback.end();
      logNetworkResponse("TRIVIA", frc, 0);
      return false;
    }
    String fpayload = fallback.getString();
    fallback.end();
    logNetworkResponse("TRIVIA", frc, fpayload.length());
    StaticJsonDocument<256> ffilter;
    ffilter["events"][0]["year"] = true;
    ffilter["events"][0]["description"] = true;
    DynamicJsonDocument fdoc(4096);
    DeserializationError ferr =
        deserializeJson(fdoc, fpayload, DeserializationOption::Filter(ffilter));
    if (ferr) {
      debugLog("Fallback trivia parse failed:", ferr.c_str());
      return false;
    }
    JsonObject fevent = fdoc["events"][0];
    if (fevent.isNull()) {
      debugLog("Fallback trivia response missing events");
      return false;
    }
    int fyear = atoi(String((const char *)(fevent["year"] | "0")).c_str());
    const char *ftext = fevent["description"] | "";
    if (!ftext || ftext[0] == '\0') {
      debugLog("Fallback trivia response missing text");
      return false;
    }
    String fcleaned = String(ftext);
    compactWhitespace(fcleaned);
    g_app.onThisDay.year = fyear;
    strncpy(g_app.onThisDay.text, fcleaned.c_str(), sizeof(g_app.onThisDay.text) - 1);
    g_app.onThisDay.text[sizeof(g_app.onThisDay.text) - 1] = '\0';
    g_app.hasOnThisDay = true;
    return true;
  }

  JsonObject event = doc["selected"][0];
  if (event.isNull()) {
    debugLog("Trivia response missing events");
    return false;
  }

  int year = event["year"] | 0;
  const char *text = event["text"] | "";
  if (!text || text[0] == '\0') {
    debugLog("Trivia response missing text");
    return false;
  }

  String cleaned = String(text);
  compactWhitespace(cleaned);

  g_app.onThisDay.year = year;
  strncpy(g_app.onThisDay.text, cleaned.c_str(), sizeof(g_app.onThisDay.text) - 1);
  g_app.onThisDay.text[sizeof(g_app.onThisDay.text) - 1] = '\0';
  g_app.hasOnThisDay = true;
  return true;
}
