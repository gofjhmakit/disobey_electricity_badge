#include "news_client.h"
#include "app_state.h"
#include "config.h"
#include "debug_log.h"

#include <HTTPClient.h>

bool fetchNewsFeed(const char *url, NewsItem *items, int &count, unsigned long &lastFetchMs) {
  debugLog("fetchNewsFeed starting for:", url);
  lastFetchMs = millis();
  logNetworkRequest("NEWS", url);
  HTTPClient client;
  client.begin(url);
  client.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  client.setTimeout(15000);

  int rc = client.GET();
  if (rc != 200) {
    client.end();
    logNetworkResponse("NEWS", rc, 0);
    Serial.printf("fetchNewsFeed HTTP failed: %d\n", rc);
    return false;
  }

  String payload = client.getString();
  client.end();
  logNetworkResponse("NEWS", rc, payload.length());

  if (payload.length() == 0) {
    debugLog("fetchNewsFeed: Received empty payload");
    return false;
  }

  count = 0;
  int pos = 0;
  while (count < MAX_NEWS_ITEMS) {
    int itemStart = payload.indexOf("<item>", pos);
    if (itemStart == -1) break;
    int itemEnd = payload.indexOf("</item>", itemStart);
    if (itemEnd == -1) break;

    String itemStr = payload.substring(itemStart, itemEnd);
    int titleStart = itemStr.indexOf("<title>");
    int titleEnd = itemStr.indexOf("</title>");
    if (titleStart != -1 && titleEnd != -1) {
      String title = itemStr.substring(titleStart + 7, titleEnd);
      title.replace("<![CDATA[", "");
      title.replace("]]>", "");
      title.replace("&amp;", "&");
      title.replace("&quot;", "\"");
      title.replace("&apos;", "'");
      title.replace("&lt;", "<");
      title.replace("&gt;", ">");
      title.trim();
      items[count].title = title;
    }
    int dateStart = itemStr.indexOf("<pubDate>");
    int dateEnd = itemStr.indexOf("</pubDate>");
    if (dateStart != -1 && dateEnd != -1) {
      String date = itemStr.substring(dateStart + 9, dateEnd);
      if (date.length() > 16) {
        items[count].pubDate = date.substring(5, 16);
      } else {
        items[count].pubDate = date;
      }
    }
    count++;
    pos = itemEnd;
  }
  debugLog("fetchNewsFeed items parsed:", count);
  return count > 0;
}

bool fetchKotimaaNews() {
  return fetchNewsFeed("https://yle.fi/rss/t/18-34837/fi",
                       g_app.newsItemsKotimaa, g_app.newsCountKotimaa, g_app.lastNewsFetchMs);
}

bool fetchKeskiSuomiNews() {
  return fetchNewsFeed("https://yle.fi/rss/t/18-148148/fi",
                       g_app.newsItemsKeskiSuomi, g_app.newsCountKeskiSuomi,
                       g_app.lastNewsKeskiSuomiFetchMs);
}
