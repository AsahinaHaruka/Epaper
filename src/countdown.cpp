#include "countdown.h"
#include "_preference.h"
#include "API.hpp"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "nongli.h" // For lunar calculations

static int8_t _cd_status = -1;
static CountdownData _cd_data = {};

extern struct tm tmInfo; // From main.cpp or screen_ink.cpp

void calculate_countdown_days(CountdownItem& item) {
  item.daysRemaining = -1;
  item.targetTime = UINT64_MAX;

  if (strlen(item.dateStr) < 4) return;

  int tgtMon = atoi(item.dateStr);
  const char* dash = strchr(item.dateStr, '-');
  if (!dash) return;
  int tgtDay = atoi(dash + 1);

  if (tgtMon <= 0 || tgtDay <= 0) return;

  struct tm today = tmInfo;
  today.tm_hour = 0;
  today.tm_min = 0;
  today.tm_sec = 0;
  time_t todayTime = mktime(&today);

  if (!item.isLunar) {
    struct tm target = today;
    target.tm_mon = tgtMon - 1;
    target.tm_mday = tgtDay;
    time_t targetTime = mktime(&target);

    if (targetTime < todayTime) {
      target.tm_year += 1;
      targetTime = mktime(&target);
    }
    item.daysRemaining = (int)((targetTime - todayTime) / 86400);
    item.targetTime = (target.tm_year + 1900) * 10000 + (target.tm_mon + 1) * 100 + target.tm_mday;
  } else {
    // Lunar search
    int searchYear = tmInfo.tm_year + 1900;
    int searchMon = tmInfo.tm_mon + 1;
    bool found = false;
    int searchDays[31];

    for (int m = 0; m < 13 && !found; m++) {
      nl_month_days(searchYear, searchMon, searchDays);
      int totalDays = 31;
      if (searchMon == 4 || searchMon == 6 || searchMon == 9 || searchMon == 11) totalDays = 30;
      else if (searchMon == 2) {
        bool isLeap = (searchYear % 4 == 0 && searchYear % 100 != 0) || (searchYear % 400 == 0);
        totalDays = isLeap ? 29 : 28;
      }

      for (int d = 1; d <= totalDays; d++) {
        int ld = abs(searchDays[d - 1]);
        int lm = ld / 100;
        int lday = ld % 100;

        if (lm == tgtMon && lday == tgtDay) {
          struct tm matchDate = {0};
          matchDate.tm_year = searchYear - 1900;
          matchDate.tm_mon = searchMon - 1;
          matchDate.tm_mday = d;
          time_t matchTime = mktime(&matchDate);
          int diff = (int)((matchTime - todayTime) / 86400);
          
          if (diff >= 0) {
            item.daysRemaining = diff;
            item.targetTime = searchYear * 10000 + searchMon * 100 + d;
            found = true;
            break;
          }
        }
      }

      searchMon++;
      if (searchMon > 12) {
        searchMon = 1;
        searchYear++;
      }
    }
  }
}

static void sort_countdowns() {
  if (_cd_data.count <= 1) return;
  for (int i = 0; i < _cd_data.count - 1; i++) {
    for (int j = 0; j < _cd_data.count - i - 1; j++) {
      if (_cd_data.items[j].daysRemaining < 0) continue; // Invalid
      if (_cd_data.items[j + 1].daysRemaining < 0 || _cd_data.items[j].targetTime > _cd_data.items[j + 1].targetTime) {
        CountdownItem temp = _cd_data.items[j];
        _cd_data.items[j] = _cd_data.items[j + 1];
        _cd_data.items[j + 1] = temp;
      }
    }
  }
}

static void parseAndAddLocal(const String& conf) {
  if (conf.length() < 3 || _cd_data.count >= CD_MAX_ITEMS) return;

  // Format: "MM-DD,Label,Lunar"
  int firstCommIdx = conf.indexOf(',');
  if (firstCommIdx <= 0) return;
  
  int secondCommIdx = conf.indexOf(',', firstCommIdx + 1);
  if (secondCommIdx <= 0) return;

  String datePart = conf.substring(0, firstCommIdx);
  String labelPart = conf.substring(firstCommIdx + 1, secondCommIdx);
  String lunarPart = conf.substring(secondCommIdx + 1);

  CountdownItem& item = _cd_data.items[_cd_data.count];
  strncpy(item.dateStr, datePart.c_str(), sizeof(item.dateStr) - 1);
  item.dateStr[sizeof(item.dateStr) - 1] = '\0';

  strncpy(item.label, labelPart.c_str(), CD_LABEL_MAX_LEN - 1);
  item.label[CD_LABEL_MAX_LEN - 1] = '\0';

  item.isLunar = (lunarPart == "1" || lunarPart.equalsIgnoreCase("true"));

  calculate_countdown_days(item);
  
  if (item.daysRemaining >= 0) {
    _cd_data.count++;
  }
}

void countdown_exec() {
  _cd_status = 0;
  _cd_data.count = 0;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, true);
  String url = pref.getString(PREF_CD_URL, "");
  
  if (url.length() > 0 && url.startsWith("http")) {
    Serial.println("[Countdown] Fetching from URL...");
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    
    if (http.begin(client, url)) {
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String resp = http.getString();
        JsonDocument doc;
        if (deserializeJson(doc, resp) == DeserializationError::Ok) {
          JsonArray arr = doc["countdowns"].as<JsonArray>();
          for (JsonObject obj : arr) {
            if (_cd_data.count >= CD_MAX_ITEMS) break;
            
            CountdownItem& item = _cd_data.items[_cd_data.count];
            const char* label = obj["label"];
            const char* date = obj["date"];
            bool isLunar = obj["is_lunar"];
            
            if (!label || !date) continue;

            strncpy(item.label, label, CD_LABEL_MAX_LEN - 1);
            strncpy(item.dateStr, date, sizeof(item.dateStr) - 1);
            item.isLunar = isLunar;
            
            calculate_countdown_days(item);
            if (item.daysRemaining >= 0) {
              _cd_data.count++;
            }
          }
          _cd_status = 1;
        } else {
          Serial.println("[Countdown] JSON parse failed");
          _cd_status = 2;
        }
      } else {
        Serial.printf("[Countdown] HTTP Error: %d\n", httpCode);
        _cd_status = 2;
      }
      http.end();
    } else {
      _cd_status = 2;
    }
  } else {
    // Parse local text: semicolon-separated entries "MM-DD,Label,Lunar;..."
    Serial.println("[Countdown] Parsing local configs...");
    String conf = pref.getString(PREF_CD_LOCAL, "");
    if (conf.length() > 0) {
      int start = 0;
      while (start < (int)conf.length() && _cd_data.count < CD_MAX_ITEMS) {
        int end = conf.indexOf(';', start);
        if (end < 0) end = conf.length();
        String entry = conf.substring(start, end);
        entry.trim();
        if (entry.length() > 0) {
          parseAndAddLocal(entry);
        }
        start = end + 1;
      }
    }
    _cd_status = 1;
  }
  pref.end();

  sort_countdowns();

  Serial.printf("[Countdown] Valid countdown items: %d\n", _cd_data.count);
}

int8_t countdown_status() { return _cd_status; }
CountdownData* countdown_data() { return &_cd_data; }
