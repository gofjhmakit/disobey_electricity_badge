#pragma once

#include "data_models.h"

bool fetchNewsFeed(const char *url, NewsItem *items, int &count, unsigned long &lastFetchMs);
bool fetchKotimaaNews();
bool fetchKeskiSuomiNews();
