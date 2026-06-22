#pragma once

#include <Arduino.h>

void debugLog(const char *msg);
void debugLog(const char *msg, const char *value);
void debugLog(const char *msg, int value);
void debugLog(const char *msg, unsigned long value);
void debugLog(const char *msg, float value);

void logNetworkRequest(const char *tag, const char *url);
void logNetworkResponse(const char *tag, int code, size_t len);
