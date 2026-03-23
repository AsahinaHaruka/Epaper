#include "todo.h"
#include "API.hpp"
#include "_preference.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h> // Ensure time functions are available

// ============================================================================
// 状态管理
// ============================================================================
static int8_t _todo_status = -1;
static TodoData _todo_data = {};

// Device code flow 信息
static char _user_code[16] = {0};
static char _verify_url[64] = {0};

// OAuth2 配置
static String _ms_client_id;
static String _ms_tenant_id;

// ============================================================================
// OAuth2 Token 管理
// ============================================================================

/**
 * 使用 refresh_token 获取新的 access_token
 * @return access_token，失败返回空字符串
 */
static String refreshAccessToken(const String &refreshToken) {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  String url = "https://login.microsoftonline.com/" + _ms_tenant_id +
               "/oauth2/v2.0/token";

  if (!http.begin(client, url)) {
    Serial.println(F("[Todo] Failed to connect to token endpoint"));
    return "";
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body =
      "client_id=" + _ms_client_id +
      "&grant_type=refresh_token"
      "&refresh_token=" +
      refreshToken +
      "&scope=https%3A%2F%2Fgraph.microsoft.com%2FTasks.Read%20offline_access";

  int httpCode = http.POST(body);
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Todo] Token refresh failed: %d\n", httpCode);
    Serial.println(http.getString());
    http.end();
    return "";
  }

  String response = http.getString();
  http.end();

  JsonDocument doc;
  deserializeJson(doc, response);

  if (!doc["access_token"]) {
    Serial.println(F("[Todo] No access_token in refresh response"));
    return "";
  }

  String accessToken = doc["access_token"].as<const char *>();
  const char *newRefresh = doc["refresh_token"];

  // 保存新的 refresh_token
  if (newRefresh && strlen(newRefresh) > 0) {
    Preferences pref;
    pref.begin(PREF_NAMESPACE, false);
    pref.putString(PREF_MS_REFRESH_TK, newRefresh);
    pref.end();
    Serial.println(F("[Todo] New refresh_token saved"));
  }

  Serial.println(F("[Todo] Access token refreshed successfully"));
  return accessToken;
}

/**
 * 发起 Device Code Flow 认证
 * 成功后保存 tokens，返回 access_token
 */
static String deviceCodeFlow() {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  // Step 1: 请求 device code
  String url = "https://login.microsoftonline.com/" + _ms_tenant_id +
               "/oauth2/v2.0/devicecode";

  if (!http.begin(client, url)) {
    Serial.println(F("[Todo] Failed to connect to devicecode endpoint"));
    return "";
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body =
      "client_id=" + _ms_client_id +
      "&scope=https%3A%2F%2Fgraph.microsoft.com%2FTasks.Read%20offline"
      "_access";

  int httpCode = http.POST(body);
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Todo] Device code request failed: %d\n", httpCode);
    Serial.println(http.getString());
    http.end();
    return "";
  }

  String response = http.getString();
  http.end();

  // 确保第一次 SSL 连接资源被释放
  client.stop();

  JsonDocument doc;
  deserializeJson(doc, response);

  const char *deviceCode = doc["device_code"];
  const char *userCode = doc["user_code"];
  const char *verifyUrl = doc["verification_uri"];
  int interval = doc["interval"] | 5;
  int expiresIn = doc["expires_in"] | 900;

  // 保存 user_code 和 verify_url 供显示
  strncpy(_user_code, userCode ? userCode : "", sizeof(_user_code) - 1);
  strncpy(_verify_url, verifyUrl ? verifyUrl : "", sizeof(_verify_url) - 1);

  Serial.println(F("[Todo] ===== Device Code Flow ====="));
  Serial.printf("[Todo] URL:  %s\n", _verify_url);
  Serial.printf("[Todo] Code: %s\n", _user_code);
  Serial.println(F("[Todo] ============================="));

  // 设置状态为 3 = 需要认证
  _todo_status = 3;

  // 保存 device_code（doc 可能被后续解析覆盖）
  String deviceCodeStr = String(deviceCode);

  // Step 2: 轮询 token endpoint
  String tokenUrl = "https://login.microsoftonline.com/" + _ms_tenant_id +
                    "/oauth2/v2.0/token";

  Serial.printf("[Todo] Free heap before polling: %d\n", ESP.getFreeHeap());

  unsigned long startMs = millis();
  while ((millis() - startMs) < (unsigned long)expiresIn * 1000) {
    delay(interval * 1000);

    // 检查 WiFi 连接状态
    if (!WiFi.isConnected()) {
      Serial.println(F("[Todo] WiFi disconnected, aborting device code flow"));
      return "";
    }

    Serial.printf("[Todo] Polling... heap=%d\n", ESP.getFreeHeap());

    HTTPClient tokenHttp;
    WiFiClientSecure tokenClient;
    tokenClient.setInsecure();

    if (!tokenHttp.begin(tokenClient, tokenUrl)) {
      Serial.println(F("[Todo] Failed to connect for token poll"));
      continue;
    }

    tokenHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String tokenBody =
        "client_id=" + _ms_client_id +
        "&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"
        "&device_code=" +
        deviceCodeStr;

    int code = tokenHttp.POST(tokenBody);
    String tokenResp = tokenHttp.getString();
    tokenHttp.end();
    tokenClient.stop();

    JsonDocument tokenDoc;
    deserializeJson(tokenDoc, tokenResp);

    if (tokenDoc["access_token"]) {
      // 认证成功
      String accessToken = tokenDoc["access_token"].as<const char *>();
      const char *refreshToken = tokenDoc["refresh_token"];

      // 保存 refresh_token
      if (refreshToken && strlen(refreshToken) > 0) {
        Preferences pref;
        pref.begin(PREF_NAMESPACE, false);
        pref.putString(PREF_MS_REFRESH_TK, refreshToken);
        pref.end();
        Serial.println(F("[Todo] refresh_token saved"));
      }

      Serial.println(F("[Todo] Device code auth successful!"));
      return accessToken;
    }

    const char *error = tokenDoc["error"];
    if (error) {
      if (strcmp(error, "authorization_pending") == 0) {
        continue;
      } else if (strcmp(error, "slow_down") == 0) {
        interval += 5;
        continue;
      } else {
        Serial.printf("[Todo] Auth error: %s\n", error);
        return "";
      }
    }
  }

  Serial.println(F("[Todo] Device code flow timed out"));
  return "";
}

/**
 * 获取有效的 access_token
 * 先尝试 refresh_token，失败则发起 device code flow
 */
static String getAccessToken() {
  Preferences pref;
  pref.begin(PREF_NAMESPACE, true);
  String refreshToken = pref.getString(PREF_MS_REFRESH_TK, "");
  pref.end();

  if (refreshToken.length() > 0) {
    Serial.println(F("[Todo] Attempting token refresh..."));
    String token = refreshAccessToken(refreshToken);
    if (token.length() > 0) {
      return token;
    }
    Serial.println(
        F("[Todo] Refresh failed, falling back to device code flow"));
  }

  return deviceCodeFlow();
}

// ============================================================================
// Task 获取
// ============================================================================

/**
 * 从 Microsoft Graph 获取未完成的待办任务
 */
static bool fetchTasks(const String &accessToken) {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  _todo_data.count = 0;

  // Step 1: 获取所有任务列表
  String listsUrl = "https://graph.microsoft.com/v1.0/me/todo/lists";
  if (!http.begin(client, listsUrl)) {
    Serial.println(F("[Todo] Failed to connect to Graph API"));
    return false;
  }

  http.addHeader("Authorization", "Bearer " + accessToken);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Todo] Get lists failed: %d\n", httpCode);
    http.end();
    return false;
  }

  String listsResp = http.getString();
  http.end();
  client.stop(); // Free SSL buffers before next connection

  JsonDocument listsDoc;
  DeserializationError err = deserializeJson(listsDoc, listsResp);
  if (err) {
    Serial.printf("[Todo] Lists JSON error: %s\n", err.c_str());
    return false;
  }

  JsonArray lists = listsDoc["value"].as<JsonArray>();
  Serial.printf("[Todo] Found %d task lists\n", lists.size());

  // Free list response String — no longer needed
  listsResp = String();

  // Step 2: 遍历每个列表获取未完成任务
  for (JsonObject list : lists) {
    const char *listId = list["id"];
    const char *listName = list["displayName"];
    if (!listId)
      continue;

    Serial.printf("[Todo] Fetching tasks from list: %s\n",
                  listName ? listName : "?");

    String tasksUrl =
        "https://graph.microsoft.com/v1.0/me/todo/lists/" + String(listId) +
        "/tasks?$filter=status%20ne%20'completed'&$orderby=dueDateTime/"
        "dateTime%20asc&$top=" +
        String(TODO_MAX_ITEMS - _todo_data.count);

    if (!http.begin(client, tasksUrl))
      continue;

    http.addHeader("Authorization", "Bearer " + accessToken);
    int taskCode = http.GET();

    if (taskCode == HTTP_CODE_OK) {
      String taskResp = http.getString();
      http.end();
      client.stop();

      JsonDocument taskDoc;
      if (deserializeJson(taskDoc, taskResp) == DeserializationError::Ok) {
        JsonArray tasks = taskDoc["value"].as<JsonArray>();
        for (JsonObject task : tasks) {
          if (_todo_data.count >= TODO_MAX_ITEMS)
            break;

          const char *title = task["title"];
          const char *importance = task["importance"];

          TodoItem &item = _todo_data.items[_todo_data.count];
          strncpy(item.title, title ? title : "(无标题)",
                  TODO_TITLE_MAX_LEN - 1);
          item.title[TODO_TITLE_MAX_LEN - 1] = '\0';
          item.important = (importance && strcmp(importance, "high") == 0);

          // 解析截止日期/时间
          item.dueInfo[0] = '\0';
          JsonObject dueObj = task["dueDateTime"];
          if (!dueObj.isNull()) {
            const char *dtStr = dueObj["dateTime"]; // ISO 8601
            if (dtStr && strlen(dtStr) >= 10) {
              // 格式: "2026-02-14T14:30:00.0000000"
              // 提取月/日
              int month = atoi(dtStr + 5);
              int day = atoi(dtStr + 8);

              // Calculate sort key with UTC+8 Adjustment
              int year = atoi(dtStr);
              int hour = 0;
              int minute = 0;

              // Parse hour/minute only if present
              if (strlen(dtStr) >= 16) {
                hour = atoi(dtStr + 11);
                minute = atoi(dtStr + 14);
              }

              // Adjust for Timezone (UTC -> UTC+8)
              // Graph API returns UTC. We want Beijing Time (UTC+8).
              // Pure arithmetic to avoid mktime/gmtime TZ issues
              // (ESP32 has TZ=CST-8 set, mktime would double-apply offset)
              hour += 8;
              if (hour >= 24) {
                hour -= 24;
                day += 1;
                // Check month overflow
                int daysInMonth;
                if (month == 2) {
                  bool leap =
                      (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
                  daysInMonth = leap ? 29 : 28;
                } else if (month == 4 || month == 6 || month == 9 ||
                           month == 11) {
                  daysInMonth = 30;
                } else {
                  daysInMonth = 31;
                }
                if (day > daysInMonth) {
                  day = 1;
                  month += 1;
                  if (month > 12) {
                    month = 1;
                    year += 1;
                  }
                }
              }

              // Update display string with corrected time
              // Check if original string had "zero time"?
              // Actually, check if *corrected* time is 00:00?
              // No, user prefers "Date Only" if it was date only.
              // But now it might be shifted.
              // If it was 00:00 UTC -> 08:00 UTC+8.
              // Does the user want 08:00 displayed?
              // If task was "Due 2/14" (midnight), showing "2/14 08:00" is
              // acceptable but maybe clutter. But showing "2/14" is better.
              // Let's stick to original logic: if original string seemed to
              // have time, show time. We'll show the ADJUSTED time though.

              if (strlen(dtStr) >= 16 &&
                  !(dtStr[11] == '0' && dtStr[12] == '0' && dtStr[14] == '0' &&
                    dtStr[15] == '0')) {
                snprintf(item.dueInfo, TODO_DUE_MAX_LEN, "%d/%d %02d:%02d",
                         month, day, hour, minute);
              } else {
                snprintf(item.dueInfo, TODO_DUE_MAX_LEN, "%d/%d", month, day);
              }

              item.dueSortKey = (uint64_t)year * 100000000 +
                                (uint64_t)month * 1000000 +
                                (uint64_t)day * 10000 + hour * 100 + minute;
            } else {
              item.dueSortKey = UINT64_MAX;
            }
          } else {
            item.dueSortKey = UINT64_MAX;
          }

          _todo_data.count++;
          Serial.printf("[Todo]   %s %s\n", item.important ? "❗" : "  ",
                        item.title);
        }
      }
    } else {
      http.end();
      client.stop();
      Serial.printf("[Todo] Get tasks failed: %d\n", taskCode);
    }

    if (_todo_data.count >= TODO_MAX_ITEMS)
      break;
  }

  Serial.printf("[Todo] Total tasks: %d\n", _todo_data.count);

  // Sort by dueSortKey ascending
  // Bubble sort is fine for max 10 items
  if (_todo_data.count > 1) {
    for (int i = 0; i < _todo_data.count - 1; i++) {
      for (int j = 0; j < _todo_data.count - i - 1; j++) {
        if (_todo_data.items[j].dueSortKey >
            _todo_data.items[j + 1].dueSortKey) {
          TodoItem temp = _todo_data.items[j];
          _todo_data.items[j] = _todo_data.items[j + 1];
          _todo_data.items[j + 1] = temp;
        }
      }
    }
  }

  return true;
}

// ============================================================================
// 公共接口（同步执行，不使用 FreeRTOS 任务）
// ============================================================================

void todo_exec() {
  _todo_status = 0;

  if (!WiFi.isConnected()) {
    _todo_status = 2;
    return;
  }

  Serial.println(F("[Task] todo fetch begin..."));

  // 读取配置
  Preferences pref;
  pref.begin(PREF_NAMESPACE, true);
  _ms_client_id = pref.getString(PREF_MS_CLIENT_ID, "");
  _ms_tenant_id = pref.getString(PREF_MS_TENANT_ID, "consumers");
  pref.end();

  if (_ms_client_id.length() == 0) {
    Serial.println(F("[Todo] MS Client ID not configured"));
    _todo_status = 2;
    return;
  }

  Serial.printf("[Todo] Free heap: %d\n", ESP.getFreeHeap());

  // 获取 access token
  String accessToken = getAccessToken();
  if (accessToken.length() == 0) {
    Serial.println(F("[Todo] Failed to get access token"));
    if (_todo_status != 3)
      _todo_status = 2; // 保留 status 3 (需要认证)
    return;
  }

  // 获取任务
  _todo_status = 0;
  if (fetchTasks(accessToken)) {
    _todo_status = 1;
    Serial.println(F("[Task] todo fetch success"));
  } else {
    _todo_status = 2;
    Serial.println(F("[Task] todo fetch failed"));
  }
}

void todo_stop() {
  // 同步模式无需停止任务
}

int8_t todo_status() { return _todo_status; }
TodoData *todo_data() { return &_todo_data; }
const char *todo_user_code() { return _user_code; }
const char *todo_verify_url() { return _verify_url; }
