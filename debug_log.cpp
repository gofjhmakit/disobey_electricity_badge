#include "debug_log.h"

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

void logNetworkRequest(const char *tag, const char *url) {
  Serial.print("[NET] ");
  Serial.print(tag);
  Serial.print(" -> GET ");
  Serial.println(url);
}

void logNetworkResponse(const char *tag, int code, size_t len) {
  Serial.print("[NET] ");
  Serial.print(tag);
  Serial.print(" <- ");
  Serial.print(code);
  Serial.print(" (");
  Serial.print(len);
  Serial.println(" B)");
}
