#pragma once

#include <Arduino.h>

struct HourItem {
  int year;
  int month;
  int day;
  int hour;
  float price;
};

struct WeatherDay {
  char date[11];
  int weatherCode;
  float tempMin;
  float tempMax;
  float precipitation;
  int precipitationProbability;
  float windSpeed;
};

struct NewsItem {
  String title;
  String pubDate;
};

struct StockItem {
  String symbol;
  String name;
  float price;
  float change;
  float changePercent;
  bool fetched;
};

struct TrainItem {
  char localTime[6];
  char trainLabel[12];
  char destination[12];
  char track[6];
  int delayMinutes;
  bool isArrival;
  bool cancelled;
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
