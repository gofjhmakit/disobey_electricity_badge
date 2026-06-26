#include "bus_client.h"
#include "app_state.h"
#include "debug_log.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>

namespace {
const char *kDigitransitGraphqlBase = "https://api.digitransit.fi/routing/v2/waltti/gtfs/v1";

// Muurame
const char *kFromLat = "62.100000";
const char *kFromLon = "25.660000";
// Kauppakatu 31, Jyväskylä
const char *kToLat = "62.242606";
const char *kToLon = "25.746742";

bool hasValidDigitransitApiKey() {
  String key = g_app.settings.digitransitApiKey;
  key.trim();
  return key.length() > 0 && !key.equalsIgnoreCase("x");
}

String extractHHMMFromIso(const char *iso) {
  if (!iso || strlen(iso) < 16) return "--:--";
  char buf[6];
  buf[0] = iso[11];
  buf[1] = iso[12];
  buf[2] = ':';
  buf[3] = iso[14];
  buf[4] = iso[15];
  buf[5] = '\0';
  return String(buf);
}

String currentLocalIso8601() {
  time_t now = time(nullptr);
  tm localTm;
  localtime_r(&now, &localTm);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &localTm);
  String s(buf);
  if (s.length() >= 5) {
    String tz = s.substring(s.length() - 5);  // +0300
    s = s.substring(0, s.length() - 5) + tz.substring(0, 3) + ":" + tz.substring(3, 5);
  }
  return s;
}

bool fetchOneDirection(const char *title, const char *fromLat, const char *fromLon,
                       const char *toLat, const char *toLon, BusTripItem &out) {
  String url = String(kDigitransitGraphqlBase) +
               "?digitransit-subscription-key=" + g_app.settings.digitransitApiKey;
  String nowIso = currentLocalIso8601();
  const char *query =
      "query Plan($fromPlace: PlanLabeledLocationInput!, $toPlace: PlanLabeledLocationInput!, "
      "$modes: PlanModesInput!, $datetime: PlanDateTimeInput!, $first: Int){"
      " plan: planConnection(first:$first, origin:$fromPlace, destination:$toPlace, "
      "modes:$modes, dateTime:$datetime){"
      " edges{ node{ duration start end legs{ mode transitLeg start{scheduledTime} end{scheduledTime} from{name} to{name} route{shortName} } } }"
      " }"
      "}";

  DynamicJsonDocument req(3072);
  req["query"] = query;
  req["variables"]["fromPlace"]["location"]["coordinate"]["latitude"] = atof(fromLat);
  req["variables"]["fromPlace"]["location"]["coordinate"]["longitude"] = atof(fromLon);
  req["variables"]["fromPlace"]["label"] = title;
  req["variables"]["toPlace"]["location"]["coordinate"]["latitude"] = atof(toLat);
  req["variables"]["toPlace"]["location"]["coordinate"]["longitude"] = atof(toLon);
  req["variables"]["toPlace"]["label"] = title;
  req["variables"]["modes"]["directOnly"] = false;
  req["variables"]["modes"]["transitOnly"] = false;
  JsonArray direct = req["variables"]["modes"].createNestedArray("direct");
  direct.add("WALK");
  JsonObject transit = req["variables"]["modes"].createNestedObject("transit");
  JsonArray access = transit.createNestedArray("access");
  access.add("WALK");
  JsonArray transfer = transit.createNestedArray("transfer");
  transfer.add("WALK");
  JsonArray egress = transit.createNestedArray("egress");
  egress.add("WALK");
  JsonArray transitModes = transit.createNestedArray("transit");
  JsonObject modeObj = transitModes.createNestedObject();
  modeObj["mode"] = "BUS";
  req["variables"]["datetime"]["earliestDeparture"] = nowIso;
  req["variables"]["first"] = 1;

  String body;
  serializeJson(req, body);

  logNetworkRequest("BUS", url.c_str());
  HTTPClient client;
  client.begin(url);
  client.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  client.setTimeout(15000);
  client.addHeader("Accept", "application/json");
  client.addHeader("Content-Type", "application/json");
  client.addHeader("Accept-Language", "fi");

  int rc = client.POST(body);
  if (rc != 200) {
    client.end();
    logNetworkResponse("BUS", rc, 0);
    return false;
  }

  String payload = client.getString();
  client.end();
  logNetworkResponse("BUS", rc, payload.length());

  DynamicJsonDocument doc(12288);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    debugLog("BUS JSON parse failed:", err.c_str());
    return false;
  }

  JsonObject itinerary = doc["data"]["plan"]["edges"][0]["node"];
  if (itinerary.isNull()) {
    out.available = false;
    return true;
  }

  const char *startIso = itinerary["start"] | "";
  const char *endIso = itinerary["end"] | "";
  const char *transitStartIso = startIso;
  const char *transitEndIso = endIso;
  float durationSec = itinerary["duration"] | 0.0f;
  JsonArray legs = itinerary["legs"];
  JsonObject leg0 = legs[0];
  for (JsonVariant legVar : legs) {
    JsonObject leg = legVar.as<JsonObject>();
    if (leg["transitLeg"] | false) {
      leg0 = leg;
      transitStartIso = leg["start"]["scheduledTime"] | startIso;
      transitEndIso = leg["end"]["scheduledTime"] | endIso;
      break;
    }
  }

  strncpy(out.title, title, sizeof(out.title) - 1);
  out.title[sizeof(out.title) - 1] = '\0';
  String line = leg0["route"]["shortName"] | "-";
  strncpy(out.line, line.c_str(), sizeof(out.line) - 1);
  out.line[sizeof(out.line) - 1] = '\0';
  String fromName = leg0["from"]["name"] | "";
  String toName = leg0["to"]["name"] | "";
  strncpy(out.fromStop, fromName.c_str(), sizeof(out.fromStop) - 1);
  out.fromStop[sizeof(out.fromStop) - 1] = '\0';
  strncpy(out.toStop, toName.c_str(), sizeof(out.toStop) - 1);
  out.toStop[sizeof(out.toStop) - 1] = '\0';
  String dep = extractHHMMFromIso(transitStartIso);
  String arr = extractHHMMFromIso(transitEndIso);
  strncpy(out.departLocal, dep.c_str(), sizeof(out.departLocal) - 1);
  out.departLocal[sizeof(out.departLocal) - 1] = '\0';
  strncpy(out.arriveLocal, arr.c_str(), sizeof(out.arriveLocal) - 1);
  out.arriveLocal[sizeof(out.arriveLocal) - 1] = '\0';
  out.durationMin = (int)(durationSec / 60.0f + 0.5f);
  out.available = true;
  return true;
}
}

bool fetchBusTrips() {
  g_app.lastBusTripFetchMs = millis();
  if (!hasValidDigitransitApiKey()) {
    debugLog("BUS fetch skipped: invalid API key");
    g_app.busTrips[0].available = false;
    g_app.busTrips[1].available = false;
    return false;
  }

  bool ok1 = fetchOneDirection("Muurame -> JKL", kFromLat, kFromLon, kToLat, kToLon,
                               g_app.busTrips[0]);
  bool ok2 = fetchOneDirection("JKL -> Muurame", kToLat, kToLon, kFromLat, kFromLon, g_app.busTrips[1]);
  return ok1 && ok2;
}
