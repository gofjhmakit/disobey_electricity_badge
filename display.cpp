#include "display.h"
#include "app_state.h"
#include "config.h"
#include "debug_log.h"
#include "led_controller.h"
#include "price_cache.h"

#include <FastLED.h>
#include <LovyanGFX.hpp>
#include <math.h>
#include <time.h>
#include <WiFi.h>
#include <Esp.h>

struct AirQualityLevel {
  const char *label;
  const char *emoji;
  uint16_t color;
};

static AirQualityLevel classifyAqi(int aqi) {
  if (aqi <= 25) return {"EXCELLENT", ":D", TFT_GREEN};
  if (aqi <= 50) return {"GOOD", ":)", TFT_CYAN};
  if (aqi <= 100) return {"OK", ":|", TFT_YELLOW};
  if (aqi <= 150) return {"BAD", ":(", 0xFD20};  // orange
  return {"HORRIBLE", "X(", TFT_RED};
}

void initDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  g_app.tft.init();
  g_app.tft.setRotation(1);
  g_app.tft.fillScreen(TFT_BLACK);
}

void drawHeader(const String &subtitle) {
  g_app.tft.fillRect(0, 0, 320, 24, TFT_NAVY);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setTextDatum(TL_DATUM);
  g_app.tft.setTextSize(1);
  g_app.tft.setCursor(4, 4);
  g_app.tft.print("Nordpool Price Badge");
  g_app.tft.setCursor(4, 16);
  g_app.tft.print(subtitle);
}

void drawFooter(const String &line1, const String &line2) {
  g_app.tft.fillRect(0, 150, 320, 20, TFT_DARKGREY);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setTextDatum(TL_DATUM);
  g_app.tft.setCursor(4, 152);
  g_app.tft.print(line1);
  g_app.tft.setCursor(4, 162);
  g_app.tft.print(line2);
}

void drawMainScreen() {
  debugLog("Drawing main screen...");
  g_app.tft.fillScreen(TFT_BLACK);

  tm timeInfo;
  getLocalTime(&timeInfo);
  int selectedDayOffset = g_app.activeScreen == SCREEN_PRICE_TOMORROW ? 1 : 0;
  String dayLabel = selectedDayOffset == 0 ? "Today" : "Tomorrow";
  drawHeader("Wide hourly overview - " + dayLabel);

  char timeText[32];
  sprintf(timeText, "%02d:%02d %04d-%02d-%02d", timeInfo.tm_hour, timeInfo.tm_min,
          timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setTextSize(2);
  g_app.tft.setCursor(4, 28);
  g_app.tft.print(timeText);

  int startIndex = -1;
  int count = 0;
  findDayRange(selectedDayOffset, startIndex, count);
  if (count <= 0) {
    g_app.tft.setTextSize(2);
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setCursor(4, 90);
    g_app.tft.print("No data for selected day.");
    drawFooter("Press B to refresh.", "Left/Right to change day.");
    digitalWrite(LED_ACTIVATE_PIN, LOW);
    FastLED.clear(true);
    return;
  }

  float cheapest = 1e6;
  float expensive = -1e6;
  float sum = 0;
  for (int i = startIndex; i < startIndex + count; ++i) {
    float value = g_app.cacheItems[i].price;
    cheapest = min(cheapest, value);
    expensive = max(expensive, value);
    sum += value;
  }
  float average = sum / count;

  int chartX = 12;
  int chartY = 60;
  int chartW = 296;
  int chartH = 80;
  g_app.tft.drawRect(chartX - 2, chartY - 2, chartW + 4, chartH + 4, TFT_WHITE);

  float range = max(0.05f, expensive - cheapest);

  g_app.tft.setTextSize(1);
  g_app.tft.setTextColor(0x7BEF);
  for (int line = 1; line <= 3; ++line) {
    float priceAtLine = cheapest + (range * line / 3.0f);
    int lineY = chartY + chartH - (int)(range > 0 ? (priceAtLine - cheapest) / range * (chartH - 8) : 0);
    for (int dx = 0; dx < chartW; dx += 4) {
      g_app.tft.drawPixel(chartX + dx, lineY, 0x7BEF);
      g_app.tft.drawPixel(chartX + dx + 1, lineY, 0x7BEF);
    }
    g_app.tft.setCursor(chartX - 28, lineY - 3);
    g_app.tft.printf("%.1f c", priceAtLine);
  }

  int barCount = min(count, MAX_BAR_COUNT);
  int barWidth = max(8, chartW / barCount);
  int x = chartX;

  for (int i = startIndex; i < startIndex + barCount && i < g_app.cacheCount; ++i) {
    float value = g_app.cacheItems[i].price;
    int barHeight = (int)((value - cheapest) / range * (chartH - 8));
    int by = chartY + chartH - barHeight;
    CRGB rgb = colorForValue(value);
    uint16_t color = g_app.tft.color565(rgb.r, rgb.g, rgb.b);
    g_app.tft.fillRect(x, by, barWidth - 2, barHeight, color);
    if (selectedDayOffset == 0 && g_app.cacheItems[i].hour == timeInfo.tm_hour) {
      g_app.tft.drawRect(x - 1, by - 1, barWidth, barHeight + 2, TFT_WHITE);
    }
    if (g_app.cacheItems[i].hour % 6 == 0) {
      g_app.tft.setTextSize(1);
      g_app.tft.setTextColor(TFT_WHITE);
      g_app.tft.setCursor(x, chartY + chartH + 2);
      g_app.tft.printf("%02d", g_app.cacheItems[i].hour);
    }
    x += barWidth;
  }

  String lowHighText = "Low: " + String(cheapest, 2) + " c  High: " + String(expensive, 2) + " c";
  String averageText = "Avg: " + String(average, 2) + " c/kWh";
  drawFooter(lowHighText, averageText);

  float currentPrice = g_app.cacheItems[startIndex].price;
  float nextPrice = currentPrice;
  bool currentFound = false;
  for (int i = startIndex; i < startIndex + count; ++i) {
    if (selectedDayOffset == 0 && g_app.cacheItems[i].hour == timeInfo.tm_hour) {
      currentPrice = g_app.cacheItems[i].price;
      if (i + 1 < startIndex + count) {
        nextPrice = g_app.cacheItems[i + 1].price;
      }
      currentFound = true;
      break;
    }
  }
  if (!currentFound && count > 1) {
    currentPrice = g_app.cacheItems[startIndex].price;
    nextPrice = g_app.cacheItems[startIndex + 1].price;
  }
  updateLEDsForHour(currentPrice, nextPrice);
}

static const char *weatherDescription(int code) {
  if (code == 0) return "Clear";
  if (code == 1 || code == 2) return "Partly cloudy";
  if (code == 3) return "Cloudy";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code >= 85 && code <= 86) return "Snow showers";
  if (code >= 95 && code <= 99) return "Thunder";
  return "Mixed";
}

static uint16_t weatherColor(int code) {
  if (code == 0) return TFT_YELLOW;
  if (code <= 3) return TFT_CYAN;
  if (code == 45 || code == 48) return 0xBDF7;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return TFT_BLUE;
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return TFT_WHITE;
  if (code >= 95) return TFT_ORANGE;
  return TFT_LIGHTGREY;
}

static void drawWeatherIcon(int x, int y, int code, int scale) {
  uint16_t color = weatherColor(code);
  if (code == 0) {
    g_app.tft.fillCircle(x, y, 10 * scale, color);
    for (int i = 0; i < 8; ++i) {
      float angle = i * PI / 4.0f;
      int x1 = x + cos(angle) * 14 * scale;
      int y1 = y + sin(angle) * 14 * scale;
      int x2 = x + cos(angle) * 20 * scale;
      int y2 = y + sin(angle) * 20 * scale;
      g_app.tft.drawLine(x1, y1, x2, y2, color);
    }
    return;
  }

  if (code == 45 || code == 48) {
    g_app.tft.fillCircle(x - 9 * scale, y - 6 * scale, 9 * scale, TFT_LIGHTGREY);
    g_app.tft.fillCircle(x + 3 * scale, y - 10 * scale, 12 * scale, TFT_LIGHTGREY);
    g_app.tft.fillCircle(x + 14 * scale, y - 5 * scale, 8 * scale, TFT_LIGHTGREY);
    g_app.tft.fillRect(x - 16 * scale, y - 5 * scale, 34 * scale, 10 * scale, TFT_LIGHTGREY);
    for (int i = 0; i < 4; ++i) {
      int yy = y + (8 + i * 5) * scale;
      g_app.tft.drawFastHLine(x - 22 * scale, yy, 44 * scale, 0xBDF7);
    }
    return;
  }

  if (code >= 95) {
    g_app.tft.fillTriangle(x - 8 * scale, y - 16 * scale, x + 7 * scale, y - 16 * scale,
                           x - 1 * scale, y + 2 * scale, color);
    g_app.tft.fillTriangle(x - 1 * scale, y - 2 * scale, x + 11 * scale, y - 2 * scale,
                           x - 8 * scale, y + 18 * scale, color);
    return;
  }

  if (code >= 51 && code <= 57) {
    g_app.tft.fillCircle(x - 9 * scale, y - 5 * scale, 9 * scale, TFT_LIGHTGREY);
    g_app.tft.fillCircle(x + 3 * scale, y - 10 * scale, 12 * scale, TFT_LIGHTGREY);
    g_app.tft.fillCircle(x + 14 * scale, y - 4 * scale, 8 * scale, TFT_LIGHTGREY);
    g_app.tft.fillRect(x - 16 * scale, y - 4 * scale, 34 * scale, 10 * scale, TFT_LIGHTGREY);
    for (int i = -1; i <= 1; ++i) {
      g_app.tft.drawPixel(x + i * 10 * scale, y + 15 * scale, color);
      g_app.tft.drawPixel(x + i * 10 * scale, y + 18 * scale, color);
    }
    return;
  }

  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) {
    g_app.tft.fillCircle(x - 9 * scale, y - 5 * scale, 9 * scale, TFT_LIGHTGREY);
    g_app.tft.fillCircle(x + 3 * scale, y - 10 * scale, 12 * scale, TFT_LIGHTGREY);
    g_app.tft.fillCircle(x + 14 * scale, y - 4 * scale, 8 * scale, TFT_LIGHTGREY);
    g_app.tft.fillRect(x - 16 * scale, y - 4 * scale, 34 * scale, 10 * scale, TFT_LIGHTGREY);
    for (int i = -1; i <= 1; ++i) {
      g_app.tft.drawLine(x + i * 10 * scale, y + 12 * scale,
                         x + i * 10 * scale - 4 * scale, y + 22 * scale, color);
    }
    return;
  }

  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    g_app.tft.fillCircle(x - 9 * scale, y - 5 * scale, 9 * scale, TFT_LIGHTGREY);
    g_app.tft.fillCircle(x + 3 * scale, y - 10 * scale, 12 * scale, TFT_LIGHTGREY);
    g_app.tft.fillCircle(x + 14 * scale, y - 4 * scale, 8 * scale, TFT_LIGHTGREY);
    g_app.tft.fillRect(x - 16 * scale, y - 4 * scale, 34 * scale, 10 * scale, TFT_LIGHTGREY);
    for (int i = -1; i <= 1; ++i) {
      g_app.tft.fillCircle(x + i * 10 * scale, y + 17 * scale, 2 * scale, color);
    }
    return;
  }

  if (code == 1 || code == 2) {
    g_app.tft.fillCircle(x - 12 * scale, y - 12 * scale, 8 * scale, TFT_YELLOW);
  }
  g_app.tft.fillCircle(x - 9 * scale, y - 3 * scale, 9 * scale, color);
  g_app.tft.fillCircle(x + 3 * scale, y - 8 * scale, 12 * scale, color);
  g_app.tft.fillCircle(x + 14 * scale, y - 2 * scale, 8 * scale, color);
  g_app.tft.fillRect(x - 16 * scale, y - 2 * scale, 34 * scale, 10 * scale, color);
}

static String shortDate(const char *date) {
  if (!date || strlen(date) < 10) {
    return "--";
  }
  return String(date + 5);
}

void drawWeatherDayScreen(int dayIndex) {
  g_app.tft.fillScreen(TFT_BLACK);
  String label = dayIndex == 0 ? "Today" : "Tomorrow";
  drawHeader("Muurame weather - " + label);

  if (dayIndex >= g_app.weatherCount) {
    FastLED.clear(true);
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setTextSize(2);
    g_app.tft.setCursor(4, 70);
    g_app.tft.print("No weather data.");
    drawFooter("Press B to refresh.", "Left/Right for screens.");
    return;
  }

  WeatherDay &day = g_app.weatherDays[dayIndex];
  drawWeatherIcon(54, 74, day.weatherCode, 2);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setTextSize(2);
  g_app.tft.setCursor(112, 38);
  g_app.tft.print(shortDate(day.date));
  g_app.tft.setCursor(112, 64);
  g_app.tft.print(weatherDescription(day.weatherCode));

  g_app.tft.setTextSize(1);
  g_app.tft.setCursor(112, 96);
  g_app.tft.printf("Temp %.0f..%.0f C", day.tempMin, day.tempMax);
  g_app.tft.setCursor(112, 110);
  g_app.tft.printf("Rain %.1f mm  %d%%", day.precipitation, day.precipitationProbability);
  g_app.tft.setCursor(112, 124);
  g_app.tft.printf("Wind %.0f km/h", day.windSpeed);
  drawFooter("Open-Meteo forecast", "Left/Right for price/weather.");
}

void drawWeatherWeekScreen() {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader("Muurame weather - 7 days");
  FastLED.clear(true);

  if (g_app.weatherCount <= 0) {
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setTextSize(2);
    g_app.tft.setCursor(4, 70);
    g_app.tft.print("No weather data.");
    drawFooter("Press B to refresh.", "Left/Right for screens.");
    return;
  }

  int rowH = 17;
  int y = 28;
  g_app.tft.setTextSize(1);
  for (int i = 0; i < g_app.weatherCount; ++i) {
    WeatherDay &day = g_app.weatherDays[i];
    uint16_t color = weatherColor(day.weatherCode);
    g_app.tft.fillCircle(8, y + 6, 4, color);
    g_app.tft.setTextColor(TFT_WHITE);
    g_app.tft.setCursor(18, y + 1);
    g_app.tft.print(shortDate(day.date));
    g_app.tft.setCursor(72, y + 1);
    g_app.tft.printf("%4.0f..%4.0f C", day.tempMin, day.tempMax);
    g_app.tft.setCursor(160, y + 1);
    g_app.tft.printf("%3d%%", day.precipitationProbability);
    g_app.tft.setCursor(206, y + 1);
    g_app.tft.print(weatherDescription(day.weatherCode));
    y += rowH;
  }
  drawFooter("Dot=color, %=rain risk", "B refreshes data.");
}

void drawNewsScreenList(const String &header, NewsItem *items, int count) {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader(header);
  FastLED.clear(true);

  if (count <= 0) {
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setTextSize(2);
    g_app.tft.setCursor(4, 70);
    g_app.tft.print("No news available.");
    drawFooter("Press B to refresh.", "Left/Right for screens.");
    return;
  }

  g_app.tft.setTextSize(1);
  g_app.tft.setTextWrap(true);
  int y = 28;

  for (int i = 0; i < count; i++) {
    g_app.tft.setCursor(4, y);
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.print(items[i].pubDate + ": ");
    g_app.tft.setTextColor(TFT_WHITE);
    g_app.tft.println(items[i].title);

    y = g_app.tft.getCursorY() + 4;
    if (i < count - 1 && y < 145) {
      g_app.tft.drawFastHLine(2, y - 2, 316, 0x4208);
    }
    if (y > 145) break;
  }

  drawFooter("Use Left/Right to browse", "B to refresh news.");
}

void drawKotimaaNewsScreen() {
  drawNewsScreenList("Yle News: Kotimaa - Latest", g_app.newsItemsKotimaa, g_app.newsCountKotimaa);
}

void drawKeskiSuomiNewsScreen() {
  drawNewsScreenList("Yle News: Keski-Suomi - Latest", g_app.newsItemsKeskiSuomi,
                     g_app.newsCountKeskiSuomi);
}

void drawStocksScreen() {
  g_app.tft.fillScreen(TFT_BLACK);

  String subtitle = "Helsinki Stock Exchange";
  if (g_app.lastStockMarketTimestamp != 0) {
    time_t marketTime = g_app.lastStockMarketTimestamp;
    tm timeInfo;
    localtime_r(&marketTime, &timeInfo);
    char dateBuf[20];
    strftime(dateBuf, sizeof(dateBuf), " - %a, %b %d", &timeInfo);
    subtitle += dateBuf;
  }

  for (int i = 0; i < g_app.stockCount; ++i) {
    if (g_app.stocks[i].symbol == "^OMXHPI" && g_app.stocks[i].changePercent != 0) {
      char omxBuf[16];
      snprintf(omxBuf, sizeof(omxBuf), " | OMXHPI %+.1f%%", g_app.stocks[i].changePercent);
      subtitle += omxBuf;
      break;
    }
  }

  drawHeader(subtitle);
  FastLED.clear(true);

  int y = 28;
  g_app.tft.setTextSize(1);

  if (g_app.stockCount <= 0) {
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setTextSize(2);
    g_app.tft.setCursor(4, 70);
    g_app.tft.print("No stock data available");
    g_app.tft.setTextSize(1);
    g_app.tft.setCursor(4, 90);
    g_app.tft.print("(0/" + String(MAX_STOCKS) + " fetched)");
    drawFooter("Press B to refresh.", "Left/Right for screens.");
    return;
  }

  for (int i = 0; i < g_app.stockCount; ++i) {
    StockItem &stock = g_app.stocks[i];

    if (stock.symbol == "^OMXHPI") {
      continue;
    }

    g_app.tft.setTextColor(TFT_WHITE);
    g_app.tft.setCursor(4, y);
    g_app.tft.print(stock.symbol);
    g_app.tft.print(" ");
    g_app.tft.print(stock.name);

    int priceX = 140;
    g_app.tft.setCursor(priceX, y);
    g_app.tft.printf("%.2f EUR", stock.price);

    int changeX = 200;
    uint16_t changeColor = (stock.change >= 0) ? TFT_GREEN : TFT_RED;
    g_app.tft.setTextColor(changeColor);
    g_app.tft.setCursor(changeX, y);
    g_app.tft.printf("%+.2f", stock.change);

    g_app.tft.setCursor(260, y);
    g_app.tft.printf("%+.1f%%", stock.changePercent);

    y += 18;
    if (y > 140) break;
  }

  int displayedCount = g_app.stockCount;
  for (int i = 0; i < g_app.stockCount; ++i) {
    if (g_app.stocks[i].symbol == "^OMXHPI") {
      displayedCount--;
      break;
    }
  }
  String footerMsg = String(displayedCount) + " of " + String(MAX_STOCKS) + " stocks";
  drawFooter("B to refresh.", footerMsg);
}

void drawTrainsScreen() {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader("Jyväskylä station - next 6 trains");
  FastLED.clear(true);

  if (g_app.trainCount <= 0) {
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setTextSize(2);
    g_app.tft.setCursor(4, 70);
    g_app.tft.print("No train data.");
    drawFooter("Press B to refresh.", "Needs Digitraffic API access.");
    return;
  }

  g_app.tft.setTextSize(1);
  g_app.tft.setTextColor(TFT_LIGHTGREY);
  g_app.tft.setCursor(4, 28);
  g_app.tft.print("Time");
  g_app.tft.setCursor(46, 28);
  g_app.tft.print("Dir");
  g_app.tft.setCursor(86, 28);
  g_app.tft.print("Train");
  g_app.tft.setCursor(140, 28);
  g_app.tft.print("End");
  g_app.tft.setCursor(264, 28);
  g_app.tft.print("Trk");

  int y = 44;
  for (int i = 0; i < g_app.trainCount && i < MAX_TRAINS; ++i) {
    TrainItem &train = g_app.trains[i];

    g_app.tft.setTextColor(TFT_WHITE);
    g_app.tft.setCursor(4, y);
    g_app.tft.print(train.localTime);

    g_app.tft.setTextColor(train.isArrival ? TFT_GREEN : TFT_RED);
    g_app.tft.setCursor(46, y);
    g_app.tft.print(train.isArrival ? "ARR" : "DEP");

    g_app.tft.setTextColor(TFT_WHITE);
    g_app.tft.setCursor(86, y);
    g_app.tft.print(train.trainLabel);
    if (train.cancelled) {
      g_app.tft.setTextColor(TFT_ORANGE);
      g_app.tft.print(" C");
    }

    g_app.tft.setTextColor(TFT_CYAN);
    g_app.tft.setCursor(140, y);
    g_app.tft.print(train.destination);

    g_app.tft.setTextColor(TFT_WHITE);
    g_app.tft.setCursor(264, y);
    g_app.tft.print(train.track);

    if (train.delayMinutes != 0) {
      g_app.tft.setTextColor(train.delayMinutes > 0 ? TFT_ORANGE : TFT_GREEN);
      g_app.tft.setCursor(284, y);
      g_app.tft.printf("%+d", train.delayMinutes);
    }

    y += 18;
  }

  drawFooter("Green=arrival, red=departure", "End=terminal station code");
}

static String formatUptime(unsigned long ms) {
  unsigned long seconds = ms / 1000UL;
  unsigned long days = seconds / 86400UL;
  seconds %= 86400UL;
  unsigned long hours = seconds / 3600UL;
  seconds %= 3600UL;
  unsigned long minutes = seconds / 60UL;
  char buf[24];
  snprintf(buf, sizeof(buf), "%lud %02lu:%02lu", days, hours, minutes);
  return String(buf);
}

static String todayIsoDate() {
  time_t now = time(nullptr);
  if (now <= 0) {
    return "";
  }
  tm localNow;
  localtime_r(&now, &localNow);
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &localNow);
  return String(buf);
}

static bool isLeapYear(int year) {
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static int daysInMonth(int year, int month) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    return isLeapYear(year) ? 29 : 28;
  }
  return kDays[month - 1];
}

static int isoWeekNumber(time_t epoch) {
  tm localTm;
  localtime_r(&epoch, &localTm);
  char weekBuf[4];
  strftime(weekBuf, sizeof(weekBuf), "%V", &localTm);
  return atoi(weekBuf);
}

static bool hasHolidayOnDate(const char *isoDate) {
  for (int i = 0; i < g_app.holidayCount; ++i) {
    if (strcmp(g_app.holidays[i].date, isoDate) == 0) {
      return true;
    }
  }
  return false;
}

static int nextHolidayIndex(const String &today) {
  for (int i = 0; i < g_app.holidayCount; ++i) {
    if (today.length() == 0 || String(g_app.holidays[i].date) >= today) {
      return i;
    }
  }
  return -1;
}

static String formatEpoch(time_t epoch) {
  if (epoch <= 0) {
    return "never";
  }
  tm localTime;
  localtime_r(&epoch, &localTime);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &localTime);
  return String(buf);
}

void drawCalendarScreen() {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader("Calendar + Finland holidays");
  FastLED.clear(true);

  time_t now = time(nullptr);
  if (now <= 0) {
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setTextSize(2);
    g_app.tft.setCursor(4, 70);
    g_app.tft.print("No local time.");
    drawFooter("NTP time required", "Press B to refresh.");
    return;
  }
  tm todayTm;
  localtime_r(&now, &todayTm);
  const int year = todayTm.tm_year + 1900;
  const int month = todayTm.tm_mon + 1;
  const int todayDay = todayTm.tm_mday;
  const int monthDays = daysInMonth(year, month);

  tm firstTm = {};
  firstTm.tm_year = year - 1900;
  firstTm.tm_mon = month - 1;
  firstTm.tm_mday = 1;
  firstTm.tm_hour = 12;
  time_t firstEpoch = mktime(&firstTm);
  localtime_r(&firstEpoch, &firstTm);
  const int firstWeekdayMon = (firstTm.tm_wday + 6) % 7;  // Monday=0

  const int calX = 2;
  const int calY = 26;
  const int weekW = 18;
  const int dayW = 28;
  const int rowH = 16;
  const int panelX = 220;

  char monthLabel[22];
  strftime(monthLabel, sizeof(monthLabel), "%B %Y", &todayTm);
  g_app.tft.setTextSize(1);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setCursor(calX + weekW, calY);
  g_app.tft.print(monthLabel);

  g_app.tft.setTextColor(TFT_LIGHTGREY);
  g_app.tft.setCursor(calX, calY + 12);
  g_app.tft.print("Wk");
  const char *dayNames[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
  for (int col = 0; col < 7; ++col) {
    g_app.tft.setCursor(calX + weekW + col * dayW + 6, calY + 12);
    g_app.tft.print(dayNames[col]);
  }

  g_app.tft.drawFastVLine(panelX - 4, calY, 122, 0x4208);

  for (int row = 0; row < 6; ++row) {
    int mondayOffset = row * 7 - firstWeekdayMon;
    time_t weekEpoch = firstEpoch + (time_t)mondayOffset * 86400;
    int weekNum = isoWeekNumber(weekEpoch);
    int y = calY + 26 + row * rowH;

    g_app.tft.setTextColor(TFT_LIGHTGREY);
    g_app.tft.setCursor(calX, y);
    g_app.tft.printf("%02d", weekNum);

    for (int col = 0; col < 7; ++col) {
      int day = row * 7 + col - firstWeekdayMon + 1;
      if (day < 1 || day > monthDays) {
        continue;
      }
      char isoDate[11];
      snprintf(isoDate, sizeof(isoDate), "%04d-%02d-%02d", year, month, day);
      bool isToday = day == todayDay;
      bool isHoliday = hasHolidayOnDate(isoDate);

      int x = calX + weekW + col * dayW + 6;
      if (isHoliday) {
        g_app.tft.setTextColor(TFT_CYAN);
      } else {
        g_app.tft.setTextColor(TFT_WHITE);
      }
      g_app.tft.setCursor(x, y);
      g_app.tft.printf("%2d", day);

      if (isToday) {
        g_app.tft.drawRect(x - 2, y - 1, 16, 11, TFT_YELLOW);
      }
    }
  }

  const String today = todayIsoDate();
  int nextIndex = nextHolidayIndex(today);
  g_app.tft.setTextSize(1);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setCursor(panelX, calY);
  g_app.tft.print("Next holidays");

  int listY = calY + 14;
  if (g_app.holidayCount <= 0 || nextIndex < 0) {
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setCursor(panelX, listY);
    g_app.tft.print("No data");
  } else {
    for (int i = nextIndex; i < g_app.holidayCount && i < nextIndex + 4; ++i) {
      g_app.tft.setTextColor(TFT_CYAN);
      g_app.tft.setCursor(panelX, listY);
      g_app.tft.print(g_app.holidays[i].date + 5);
      g_app.tft.setTextColor(TFT_WHITE);
      g_app.tft.setCursor(panelX + 40, listY);
      String name = g_app.holidays[i].localName;
      if (name.length() > 14) {
        name = name.substring(0, 14);
      }
      g_app.tft.print(name);
      listY += 14;
    }
  }

  drawFooter("Wk=ISO week, cyan=holiday", "B refresh holidays");
}

void drawSystemHealthScreen() {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader("System health");
  FastLED.clear(true);

  g_app.tft.setTextSize(1);
  int y = 30;
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setCursor(4, y);
  g_app.tft.printf("WiFi: %s", WiFi.status() == WL_CONNECTED ? "connected" : "disconnected");
  y += 14;

  g_app.tft.setCursor(4, y);
  if (WiFi.status() == WL_CONNECTED) {
    g_app.tft.printf("RSSI: %d dBm", WiFi.RSSI());
  } else {
    g_app.tft.print("RSSI: n/a");
  }
  y += 14;

  g_app.tft.setCursor(4, y);
  g_app.tft.printf("Uptime: %s", formatUptime(millis()).c_str());
  y += 14;

  g_app.tft.setCursor(4, y);
  g_app.tft.printf("Heap free: %u KB", ESP.getFreeHeap() / 1024U);
  y += 14;

  g_app.tft.setCursor(4, y);
  g_app.tft.printf("Heap min: %u KB", ESP.getMinFreeHeap() / 1024U);
  y += 14;

  g_app.tft.setCursor(4, y);
  g_app.tft.printf("Power save: %s", g_app.powerSaveActive ? "ON" : "OFF");
  y += 14;

  g_app.tft.setCursor(4, y);
  g_app.tft.printf("Clock sync: %s", g_app.gotTime ? "ok" : "missing");
  y += 14;

  g_app.tft.setCursor(4, y);
  g_app.tft.printf("Last sync: %s", formatEpoch(g_app.lastDataSyncEpoch).c_str());
  y += 14;

  g_app.tft.setCursor(4, y);
  g_app.tft.printf("Battery: n/a");

  drawFooter("Health diagnostics", "Manual refresh: press B");
}

void drawCurrentScreen() {
  if (g_app.activeScreen == SCREEN_PRICE_TODAY || g_app.activeScreen == SCREEN_PRICE_TOMORROW) {
    drawMainScreen();
  } else if (g_app.activeScreen == SCREEN_WEATHER_TODAY) {
    drawWeatherDayScreen(0);
  } else if (g_app.activeScreen == SCREEN_WEATHER_TOMORROW) {
    drawWeatherDayScreen(1);
  } else if (g_app.activeScreen == SCREEN_WEATHER_WEEK) {
    drawWeatherWeekScreen();
  } else if (g_app.activeScreen == SCREEN_NEWS_KOTIMAA) {
    drawKotimaaNewsScreen();
  } else if (g_app.activeScreen == SCREEN_NEWS_KESKI_SUOMI) {
    drawKeskiSuomiNewsScreen();
  } else if (g_app.activeScreen == SCREEN_STOCKS) {
    drawStocksScreen();
  } else if (g_app.activeScreen == SCREEN_TRAINS) {
    drawTrainsScreen();
  } else if (g_app.activeScreen == SCREEN_CALENDAR) {
    drawCalendarScreen();
  } else if (g_app.activeScreen == SCREEN_SYSTEM_HEALTH) {
    drawSystemHealthScreen();
  } else if (g_app.activeScreen == SCREEN_RAIN_AIR_QUALITY) {
    drawRainAirQualityScreen();
  } else if (g_app.activeScreen == SCREEN_ON_THIS_DAY) {
    drawOnThisDayScreen();
  } else if (g_app.activeScreen == SCREEN_BUS_TRIPS) {
    drawBusTripsScreen();
  }
}

void drawSettingsScreen() {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader("Threshold settings");
  g_app.tft.setTextSize(2);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setCursor(4, 40);
  g_app.tft.print("Cheap <=");
  g_app.tft.print(g_app.settings.thresholdCheap, 2);
  g_app.tft.setCursor(4, 70);
  g_app.tft.print("Moderate <=");
  g_app.tft.print(g_app.settings.thresholdModerate, 2);
  g_app.tft.setCursor(4, 100);
  g_app.tft.print("Brightness: ");
  g_app.tft.print(g_app.settings.ledBrightness);

  int y = 40 + g_app.settingsIndex * 30;
  g_app.tft.drawRect(2, y - 2, 316, 26, TFT_YELLOW);
  drawFooter("Up/Down to choose value.", "Left/Right to change.");
}

void drawConfigMode() {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader("Config AP Mode");
  g_app.tft.setTextSize(2);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setCursor(4, 40);
  g_app.tft.print("SSID: NordpoolBadge_Config");
  g_app.tft.setCursor(4, 70);
  g_app.tft.print("No WiFi credentials");
  g_app.tft.setCursor(4, 100);
  g_app.tft.print("Use Serial or save settings");
  drawFooter("Set SSID/password via code.", "Restart after updating.");
}

void drawRefreshError() {
  g_app.tft.fillRect(0, 90, 320, 30, TFT_BLACK);
  g_app.tft.setTextColor(TFT_YELLOW);
  g_app.tft.setTextSize(2);
  g_app.tft.setCursor(4, 92);
  g_app.tft.print("Unable to refresh data.");
}

void drawRainAirQualityScreen() {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader("Rain & Air Quality");
  g_app.tft.setTextSize(2);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setCursor(4, 40);
  g_app.tft.print("Rain 1h:");
  if (g_app.rainCount > 0) {
    g_app.tft.setTextColor(TFT_CYAN);
    g_app.tft.setCursor(4, 70);
    g_app.tft.printf("%.1fmm", g_app.rainNext60m[0].precipitationMm);
  } else {
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setCursor(4, 70);
    g_app.tft.print("--");
  }
  g_app.tft.setTextSize(2);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setCursor(4, 92);
  g_app.tft.print("AQI:");
  int aqi = g_app.airQuality.aqiUS;
  AirQualityLevel level = classifyAqi(aqi);
  g_app.tft.setTextColor(level.color);
  g_app.tft.setTextSize(3);
  g_app.tft.setCursor(4, 112);
  g_app.tft.printf("%d", aqi);
  g_app.tft.setTextSize(2);
  g_app.tft.setCursor(90, 114);
  g_app.tft.printf("%s %s", level.label, level.emoji);
  g_app.tft.setTextSize(1);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setCursor(4, 138);
  g_app.tft.printf("PM2.5: %.1f  PM10: %.1f  NO2: %d  O3: %d",
                   g_app.airQuality.pm25, g_app.airQuality.pm10,
                   g_app.airQuality.no2, g_app.airQuality.o3);
  drawFooter("B refresh", "");
}

void drawOnThisDayScreen() {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader("On this day");
  FastLED.clear(true);

  if (!g_app.hasOnThisDay || g_app.onThisDay.text[0] == '\0') {
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setTextSize(2);
    g_app.tft.setCursor(4, 70);
    g_app.tft.print("No trivia available.");
    drawFooter("Press B to refresh.", "Source: Wikimedia/Wikipedia");
    return;
  }

  g_app.tft.setTextSize(2);
  g_app.tft.setTextColor(TFT_CYAN);
  g_app.tft.setCursor(4, 32);
  g_app.tft.printf("%d", g_app.onThisDay.year);

  g_app.tft.setTextSize(1);
  g_app.tft.setTextColor(TFT_WHITE);
  g_app.tft.setTextWrap(true);
  g_app.tft.setCursor(4, 56);
  g_app.tft.println(g_app.onThisDay.text);

  drawFooter("Daily historical event", "Source: Wikimedia/Wikipedia");
}

void drawBusTripsScreen() {
  g_app.tft.fillScreen(TFT_BLACK);
  drawHeader("Bus trips (Waltti)");
  FastLED.clear(true);

  String key = g_app.settings.digitransitApiKey;
  key.trim();
  if (key.length() == 0 || key.equalsIgnoreCase("x")) {
    g_app.tft.setTextColor(TFT_YELLOW);
    g_app.tft.setTextSize(2);
    g_app.tft.setCursor(4, 70);
    g_app.tft.print("No Apikey");
    drawFooter("Set digitransitApiKey", "in settings_store.cpp");
    return;
  }

  g_app.tft.setTextSize(1);
  for (int i = 0; i < 2; ++i) {
    const BusTripItem &trip = g_app.busTrips[i];
    int y = 30 + i * 60;
    uint16_t titleColor = i == 0 ? TFT_CYAN : TFT_GREEN;
    g_app.tft.setTextColor(titleColor);
    g_app.tft.setCursor(4, y);
    g_app.tft.print(trip.title[0] ? trip.title : (i == 0 ? "Muurame -> JKL" : "JKL -> Muurame"));

    if (!trip.available) {
      g_app.tft.setTextColor(TFT_YELLOW);
      g_app.tft.setCursor(4, y + 14);
      g_app.tft.print("No route right now");
      continue;
    }

    g_app.tft.setTextColor(TFT_WHITE);
    g_app.tft.setCursor(4, y + 14);
    g_app.tft.printf("Line %s  %s -> %s", trip.line, trip.departLocal, trip.arriveLocal);
    g_app.tft.setCursor(4, y + 28);
    g_app.tft.printf("From: %s", trip.fromStop);
    g_app.tft.setCursor(4, y + 42);
    g_app.tft.printf("To:   %s  (%d min)", trip.toStop, trip.durationMin);
  }

  drawFooter("B refreshes route", "Uses Digitransit API key");
}
