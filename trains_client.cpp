#include "trains_client.h"
#include "app_state.h"
#include "config.h"
#include "debug_log.h"

#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

#include <miniz.h>

namespace {
const char *kDigitrafficUrl =
  "https://rata.digitraffic.fi/api/v1/live-trains/station/JY"
  "?version=0&arrived_trains=5&arriving_trains=5&departed_trains=5&departing_trains=5"
  "&include_nonstopping=false";
const char *kDigitrafficHost = "rata.digitraffic.fi";
const uint16_t kDigitrafficPort = 443;
const char *kDigitrafficPath =
  "/api/v1/live-trains/station/JY"
  "?version=0&arrived_trains=5&arriving_trains=5&departed_trains=5&departing_trains=5"
  "&include_nonstopping=false";

const char *kDigitrafficUserHeader = "Nimimerkki/EsimerkkiApp 0.1";

struct TrainCandidate {
  time_t sortEpochUtc = 0;
  String localTime;
  String trainLabel;
  String destination;
  String track;
  int delayMinutes = 0;
  bool isArrival = false;
  bool cancelled = false;
};

static bool fetchRawGzipResponse(int &statusCode, bool &isGzip, std::vector<uint8_t> &body);
static bool gunzipToBuffer(const uint8_t *compressed, size_t compressedLen, std::vector<uint8_t> &out);
static bool parseTrainPayload(const char *payload, size_t payloadLen, TrainItem *outItems, int &outCount);

static bool ensureCapacity(std::vector<uint8_t> &buf, size_t capacity, const char *name) {
  if (buf.capacity() >= capacity) {
    return true;
  }
  try {
    buf.reserve(capacity);
    return true;
  } catch (const std::bad_alloc &) {
    debugLog(name, "reserve OOM");
    return false;
  }
}

static uint32_t readLe32(const uint8_t *src) {
  return (uint32_t)src[0] |
         ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) |
         ((uint32_t)src[3] << 24);
}

static bool readLine(WiFiClientSecure &client, String &line, uint32_t timeoutMs) {
  line = "";
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (client.available()) {
      char c = (char)client.read();
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        return true;
      }
      line += c;
    }
    if (!client.connected()) {
      return line.length() > 0;
    }
    delay(1);
  }
  return false;
}

static bool readBytesBlocking(WiFiClientSecure &client, uint8_t *dst, size_t len, uint32_t timeoutMs) {
  size_t offset = 0;
  uint32_t start = millis();
  while (offset < len && (millis() - start < timeoutMs)) {
    int n = client.read(dst + offset, len - offset);
    if (n > 0) {
      offset += (size_t)n;
      start = millis();
      continue;
    }
    if (!client.connected() && !client.available()) {
      break;
    }
    delay(1);
  }
  return offset == len;
}

static bool readHttpBodyWithLength(WiFiClientSecure &client, size_t length, std::vector<uint8_t> &body) {
  body.resize(length);
  if (length == 0) {
    return true;
  }
  return readBytesBlocking(client, body.data(), length, 20000);
}

static bool readHttpBodyChunked(WiFiClientSecure &client, std::vector<uint8_t> &body) {
  String line;
  while (readLine(client, line, 20000)) {
    int semicolon = line.indexOf(';');
    String sizeHex = semicolon >= 0 ? line.substring(0, semicolon) : line;
    sizeHex.trim();
    size_t chunkSize = strtoul(sizeHex.c_str(), nullptr, 16);
    if (chunkSize == 0) {
      // trailing empty line(s)
      String trailer;
      while (readLine(client, trailer, 2000) && trailer.length() > 0) {}
      return true;
    }
    size_t start = body.size();
    body.resize(start + chunkSize);
    if (!readBytesBlocking(client, body.data() + start, chunkSize, 20000)) {
      return false;
    }
    uint8_t crlf[2];
    if (!readBytesBlocking(client, crlf, 2, 5000)) {
      return false;
    }
  }
  return false;
}

static bool readHttpBodyToClose(WiFiClientSecure &client, std::vector<uint8_t> &body) {
  const size_t chunkSize = 512;
  uint8_t chunk[chunkSize];
  uint32_t lastDataMs = millis();
  while (client.connected() || client.available()) {
    int n = client.read(chunk, chunkSize);
    if (n > 0) {
      body.insert(body.end(), chunk, chunk + n);
      lastDataMs = millis();
      continue;
    }
    if (millis() - lastDataMs > 20000) {
      break;
    }
    delay(1);
  }
  return !body.empty();
}

static bool fetchRawGzipResponse(int &statusCode, bool &isGzip, std::vector<uint8_t> &body) {
  WiFiClientSecure *client = new WiFiClientSecure();
  if (!client) {
    debugLog("TRAIN client alloc failed");
    return false;
  }

  client->setInsecure();
  client->setTimeout(20000);
  if (!client->connect(kDigitrafficHost, kDigitrafficPort)) {
    debugLog("TRAIN TLS connect failed");
    delete client;
    return false;
  }

  String request =
    String("GET ") + kDigitrafficPath + " HTTP/1.1\r\n" +
    "Host: " + kDigitrafficHost + "\r\n" +
    "Digitraffic-User: " + kDigitrafficUserHeader + "\r\n" +
    "User-Agent: " + kDigitrafficUserHeader + "\r\n" +
    "Accept: */*\r\n" +
    "Accept-Encoding: gzip\r\n" +
    "Connection: close\r\n\r\n";
  client->print(request);

  String statusLine;
  if (!readLine(*client, statusLine, 20000)) {
    debugLog("TRAIN no status line");
    client->stop();
    delete client;
    return false;
  }

  int firstSpace = statusLine.indexOf(' ');
  int secondSpace = statusLine.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) {
    debugLog("TRAIN invalid status line");
    client->stop();
    delete client;
    return false;
  }
  statusCode = statusLine.substring(firstSpace + 1, secondSpace).toInt();

  bool chunked = false;
  int contentLength = -1;
  isGzip = false;

  String headerLine;
  while (readLine(*client, headerLine, 20000)) {
    if (headerLine.length() == 0) {
      break;
    }
    int colon = headerLine.indexOf(':');
    if (colon <= 0) {
      continue;
    }
    String name = headerLine.substring(0, colon);
    String value = headerLine.substring(colon + 1);
    name.trim();
    value.trim();
    name.toLowerCase();
    value.toLowerCase();
    if (name == "content-encoding" && value.indexOf("gzip") >= 0) {
      isGzip = true;
    } else if (name == "transfer-encoding" && value.indexOf("chunked") >= 0) {
      chunked = true;
    } else if (name == "content-length") {
      contentLength = value.toInt();
    }
  }

  bool ok = false;
  if (chunked) {
    ok = readHttpBodyChunked(*client, body);
  } else if (contentLength >= 0) {
    ok = readHttpBodyWithLength(*client, (size_t)contentLength, body);
  } else {
    ok = readHttpBodyToClose(*client, body);
  }
  client->stop();
  delete client;
  return ok;
}

static bool gunzipToBuffer(const uint8_t *compressed, size_t compressedLen, std::vector<uint8_t> &out) {
  if (compressedLen < 18 || compressed[0] != 0x1f || compressed[1] != 0x8b || compressed[2] != 8) {
    debugLog("Digitraffic gzip: invalid header");
    return false;
  }

  size_t pos = 10;
  uint8_t flags = compressed[3];

  if (flags & 0x04) {
    if (pos + 2 > compressedLen) return false;
    uint16_t extraLen = (uint16_t)compressed[pos] | ((uint16_t)compressed[pos + 1] << 8);
    pos += 2 + extraLen;
  }
  if (flags & 0x08) {
    while (pos < compressedLen && compressed[pos] != 0) pos++;
    pos++;
  }
  if (flags & 0x10) {
    while (pos < compressedLen && compressed[pos] != 0) pos++;
    pos++;
  }
  if (flags & 0x02) {
    pos += 2;
  }

  if (pos >= compressedLen || compressedLen < pos + 8) {
    debugLog("Digitraffic gzip: truncated stream");
    return false;
  }

  const size_t deflateLen = compressedLen - pos - 8;
  const uint8_t *deflateData = compressed + pos;
  const uint32_t expectedCrc = readLe32(compressed + compressedLen - 8);
  const uint32_t expectedSize = readLe32(compressed + compressedLen - 4);

  size_t initialOutCap = expectedSize > 0 ? (size_t)expectedSize : (deflateLen * 4 + 1024);
  if (initialOutCap < 4096) {
    initialOutCap = 4096;
  }
  const size_t maxOutCap = 512 * 1024;
  if (initialOutCap > maxOutCap) {
    initialOutCap = maxOutCap;
  }

  std::vector<uint8_t> inflated;
  inflated.resize(initialOutCap);

  tinfl_decompressor inflator;
  tinfl_init(&inflator);

  size_t inOfs = 0;
  size_t outOfs = 0;
  while (true) {
    if (outOfs >= inflated.size()) {
      if (inflated.size() >= maxOutCap) {
        debugLog("Digitraffic gzip: output too large");
        return false;
      }
      size_t grown = inflated.size() * 2;
      if (grown > maxOutCap) grown = maxOutCap;
      inflated.resize(grown);
    }

    size_t inBytes = deflateLen - inOfs;
    size_t outBytes = inflated.size() - outOfs;
    mz_uint flags = TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
    if (inOfs + inBytes < deflateLen) {
      flags |= TINFL_FLAG_HAS_MORE_INPUT;
    }

    tinfl_status status = tinfl_decompress(&inflator,
                                           deflateData + inOfs, &inBytes,
                                           inflated.data(), inflated.data() + outOfs, &outBytes,
                                           flags);
    inOfs += inBytes;
    outOfs += outBytes;

    if (status == TINFL_STATUS_DONE) {
      break;
    }
    if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
      continue;
    }
    if (status == TINFL_STATUS_NEEDS_MORE_INPUT && inOfs < deflateLen) {
      continue;
    }
    debugLog("Digitraffic gzip: inflate status", (int)status);
    return false;
  }

  inflated.resize(outOfs);

  uint32_t actualCrc = (uint32_t)mz_crc32(MZ_CRC32_INIT, inflated.data(), outOfs);
  if (actualCrc != expectedCrc || ((uint32_t)outOfs != expectedSize)) {
    debugLog("Digitraffic gzip: checksum/size mismatch");
    return false;
  }

  out.swap(inflated);
  return true;
}

static bool parseIsoUtcToEpoch(const char *isoUtc, time_t &outEpoch) {
  if (!isoUtc || strlen(isoUtc) < 19) {
    return false;
  }
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (sscanf(isoUtc, "%4d-%2d-%2dT%2d:%2d:%2d",
             &year, &month, &day, &hour, &minute, &second) != 6) {
    return false;
  }
  tm utc = {};
  utc.tm_year = year - 1900;
  utc.tm_mon = month - 1;
  utc.tm_mday = day;
  utc.tm_hour = hour;
  utc.tm_min = minute;
  utc.tm_sec = second;
  int y = utc.tm_year + 1900;
  int m = utc.tm_mon + 1;
  int d = utc.tm_mday;
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);                     // [0, 399]
  const unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;      // [0, 146096]
  const int daysSinceEpoch = era * 146097 + (int)doe - 719468;         // 1970-01-01 offset
  const int64_t epoch64 = (int64_t)daysSinceEpoch * 86400LL +
                          (int64_t)utc.tm_hour * 3600LL +
                          (int64_t)utc.tm_min * 60LL +
                          (int64_t)utc.tm_sec;
  time_t epoch = (time_t)epoch64;
  if (epoch <= 0) {
    return false;
  }
  outEpoch = epoch;
  return true;
}

static String epochUtcToLocalHHMM(time_t epochUtc) {
  if (epochUtc <= 0) {
    return "--:--";
  }
  tm localTm;
  localtime_r(&epochUtc, &localTm);
  char out[6];
  snprintf(out, sizeof(out), "%02d:%02d", localTm.tm_hour, localTm.tm_min);
  return String(out);
}

static String trainLabelFor(const JsonObject &train) {
  const char *line = train["commuterLineID"] | "";
  if (line && line[0] != '\0') {
    return String(line);
  }
  const char *type = train["trainType"] | "";
  int trainNumber = train["trainNumber"] | 0;
  if (type && type[0] != '\0') {
    return String(type) + String(trainNumber);
  }
  return String(trainNumber);
}

static String terminalStationFor(const JsonArray &rows) {
  String latestIso = "";
  String station = "";
  for (JsonObject row : rows) {
    const char *type = row["type"] | "";
    if (strcmp(type, "ARRIVAL") != 0) {
      continue;
    }
    if ((row["commercialStop"] | true) == false) {
      continue;
    }
    const char *stationCode = row["stationShortCode"] | "";
    const char *scheduled = row["scheduledTime"] | "";
    if (!stationCode || stationCode[0] == '\0' || !scheduled || strlen(scheduled) < 19) {
      continue;
    }
    String scheduledIso = String(scheduled).substring(0, 19);
    if (latestIso.length() == 0 || scheduledIso > latestIso) {
      latestIso = scheduledIso;
      station = String(stationCode);
    }
  }
  return station;
}

static bool eventEpochUtc(const JsonObject &row, time_t &outEpoch) {
  const char *estimate = row["liveEstimateTime"] | "";
  if (estimate && strlen(estimate) >= 19 && parseIsoUtcToEpoch(estimate, outEpoch)) {
    return true;
  }
  const char *scheduled = row["scheduledTime"] | "";
  if (scheduled && strlen(scheduled) >= 19 && parseIsoUtcToEpoch(scheduled, outEpoch)) {
    return true;
  }
  return false;
}

static bool parseTrainPayload(const char *payload, size_t payloadLen, TrainItem *outItems, int &outCount) {
  outCount = 0;
  DynamicJsonDocument doc(65536);
  DeserializationError error = deserializeJson(doc, payload, payloadLen);
  if (error) {
    debugLog("Train JSON parse failed:", error.c_str());
    return false;
  }

  JsonArray trains = doc.as<JsonArray>();
  if (trains.isNull()) {
    debugLog("Train JSON shape invalid");
    return false;
  }

  std::vector<TrainCandidate> candidates;
  candidates.reserve(16);
  const String stationCode = "JY";
  const time_t nowEpoch = time(nullptr);

  for (JsonObject train : trains) {
    JsonArray rows = train["timeTableRows"].as<JsonArray>();
    if (rows.isNull()) {
      continue;
    }
    String destination = terminalStationFor(rows);
    if (destination.length() == 0) {
      destination = "--";
    }
    String label = trainLabelFor(train);

    for (JsonObject row : rows) {
      const char *rowStation = row["stationShortCode"] | "";
      if (!rowStation || stationCode != rowStation) {
        continue;
      }
      if ((row["commercialStop"] | true) == false) {
        continue;
      }
      const char *type = row["type"] | "";
      bool isArrival = strcmp(type, "ARRIVAL") == 0;
      bool isDeparture = strcmp(type, "DEPARTURE") == 0;
      if (!isArrival && !isDeparture) {
        continue;
      }

      time_t eventEpoch = 0;
      if (!eventEpochUtc(row, eventEpoch)) {
        continue;
      }
      if (eventEpoch <= nowEpoch) {
        continue;
      }

      const char *track = row["commercialTrack"] | "";
      TrainCandidate candidate;
      candidate.sortEpochUtc = eventEpoch;
      candidate.localTime = epochUtcToLocalHHMM(eventEpoch);
      candidate.trainLabel = label;
      candidate.destination = destination;
      candidate.track = (track && track[0] != '\0') ? String(track) : "-";
      candidate.delayMinutes = row["differenceInMinutes"] | 0;
      candidate.isArrival = isArrival;
      candidate.cancelled = row["cancelled"] | false;
      candidates.push_back(candidate);
    }
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const TrainCandidate &a, const TrainCandidate &b) {
              return a.sortEpochUtc < b.sortEpochUtc;
            });

  for (size_t i = 0; i < candidates.size() && outCount < MAX_TRAINS; ++i) {
    TrainItem &dst = outItems[outCount];
    strncpy(dst.localTime, candidates[i].localTime.c_str(), sizeof(dst.localTime) - 1);
    dst.localTime[sizeof(dst.localTime) - 1] = '\0';
    strncpy(dst.trainLabel, candidates[i].trainLabel.c_str(), sizeof(dst.trainLabel) - 1);
    dst.trainLabel[sizeof(dst.trainLabel) - 1] = '\0';
    strncpy(dst.destination, candidates[i].destination.c_str(), sizeof(dst.destination) - 1);
    dst.destination[sizeof(dst.destination) - 1] = '\0';
    strncpy(dst.track, candidates[i].track.c_str(), sizeof(dst.track) - 1);
    dst.track[sizeof(dst.track) - 1] = '\0';
    dst.delayMinutes = candidates[i].delayMinutes;
    dst.isArrival = candidates[i].isArrival;
    dst.cancelled = candidates[i].cancelled;
    outCount++;
  }
  return outCount > 0;
}
}

bool fetchTrains() {
  debugLog("fetchTrains: starting");
  g_app.lastTrainFetchMs = millis();
  logNetworkRequest("TRAIN", kDigitrafficUrl);

  try {
    int rc = -1;
    bool isGzip = false;
    static std::vector<uint8_t> body;
    static std::vector<uint8_t> inflated;
    body.clear();
    inflated.clear();
    // Keep trains allocations bounded; previous 96/192KB reserve could throw and abort.
    if (!ensureCapacity(body, 48 * 1024, "TRAIN body")) {
      return false;
    }
    if (!ensureCapacity(inflated, 96 * 1024, "TRAIN inflated")) {
      return false;
    }

    if (!fetchRawGzipResponse(rc, isGzip, body)) {
      logNetworkResponse("TRAIN", rc, 0);
      debugLog("fetchTrains: request failed");
      return false;
    }
    if (rc != 200) {
      logNetworkResponse("TRAIN", rc, body.size());
      return false;
    }
    if (body.empty()) {
      logNetworkResponse("TRAIN", rc, 0);
      debugLog("fetchTrains: empty body");
      return false;
    }

    const char *payloadData = nullptr;
    size_t payloadLen = 0;
    if (isGzip) {
      if (!gunzipToBuffer(body.data(), body.size(), inflated)) {
        logNetworkResponse("TRAIN", rc, 0);
        debugLog("fetchTrains: gzip decode failed");
        return false;
      }
      payloadData = reinterpret_cast<const char *>(inflated.data());
      payloadLen = inflated.size();
    } else {
      payloadData = reinterpret_cast<const char *>(body.data());
      payloadLen = body.size();
    }

    TrainItem fetchedTrains[MAX_TRAINS];
    int fetchedCount = 0;
    if (!parseTrainPayload(payloadData, payloadLen, fetchedTrains, fetchedCount)) {
      logNetworkResponse("TRAIN", rc, payloadLen);
      debugLog("fetchTrains: parse failed");
      return false;
    }

    logNetworkResponse("TRAIN", rc, payloadLen);
    g_app.trainCount = 0;
    for (int i = 0; i < fetchedCount && i < MAX_TRAINS; ++i) {
      g_app.trains[g_app.trainCount] = fetchedTrains[i];
      g_app.trainCount++;
    }

    debugLog("fetchTrains parsed:", g_app.trainCount);
    return g_app.trainCount > 0;
  } catch (const std::bad_alloc &) {
    debugLog("fetchTrains: OOM");
    return false;
  }
}
