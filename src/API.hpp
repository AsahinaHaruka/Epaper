#ifndef __API_HPP__
#define __API_HPP__

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <esp_http_client.h>

#include <ArduinoUZlib.h> // 解压gzip

struct Weather {
  String time;
  int8_t temp;
  int8_t feelsLike;
  int8_t humidity;
  int16_t wind360;
  String windDir;
  int8_t windScale;
  uint8_t windSpeed;
  float precip;
  uint16_t icon;
  String text;
  String updateTime;
};

struct DailyWeather {
  String date;
  String sunrise;
  String sunset;
  String moonPhase;
  uint16_t moonPhaseIcon;
  int8_t tempMax;
  int8_t tempMin;
  int8_t humidity;
  float precip;
  uint16_t iconDay;
  String textDay;
  uint16_t iconNight;
  String textNight;
  int16_t wind360Day;
  String windDirDay;
  int8_t windScaleDay;
  uint8_t windSpeedDay;
  int16_t wind360Night;
  String windDirNight;
  int8_t windScaleNight;
  uint8_t windSpeedNight;
};

struct HourlyForecast {
  Weather *weather;
  uint8_t length;
  uint8_t interval;
};

struct DailyForecast {
  DailyWeather *weather;
  uint8_t length;
  String updateTime;
};

struct AirQuality {
  int16_t aqi;             // AQI 数值
  String category;         // 等级描述，如 "Good", "Moderate"
  String primaryPollutant; // 主要污染物名称
};

struct MinutelyPrecip {
  String summary; // 分钟降水描述，如 "95分钟后雨就停了"
};

struct GeoLocation {
  String name; // 城市名称
};

template <uint8_t MAX_RETRY = 3> class API {
  using callback = std::function<bool(JsonDocument &)>;
  using precall = std::function<void()>;

private:
  HTTPClient http;
  WiFiClientSecure wifiClient;

  // Bearer token 认证的 REST API 请求（用于空气质量 API）
  bool getRestfulAPIWithBearer(String url, const char *token, callback cb) {
    return getRestfulAPI(url, cb, [this, token]() {
      http.addHeader("Authorization", String("Bearer ") + token);
    });
  }

  bool getRestfulAPI(String url, callback cb, precall pre = precall()) {
    // Serial.printf("Request Url: %s\n", url.c_str());
    JsonDocument doc;

    for (uint8_t i = 0; i < MAX_RETRY; i++) {
      bool shouldRetry = false;
      if (http.begin(wifiClient, url)) {
        if (pre)
          pre();
        // Serial.printf("Before GET\n");
        int httpCode = http.GET();
        // Serial.printf("GET %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_NOT_MODIFIED) {
          bool isGzip = false;
          int headers = http.headers();
          for (int j = 0; j < headers; j++) {
            String headerName = http.headerName(j);
            String headerValue = http.header(j);
            // Serial.println(headerName + ": " + headerValue);
            if (headerName.equalsIgnoreCase("Content-Encoding") &&
                headerValue.equalsIgnoreCase("gzip")) {
              isGzip = true;
              break;
            }
          }
          String s = http.getString();
          DeserializationError error;
          if (isGzip) {
            // gzip解压缩
            uint8_t *outBuf = NULL;
            uint32_t outLen = 0;
            ArduinoUZlib::decompress((uint8_t *)s.c_str(), (uint32_t)s.length(),
                                     outBuf, outLen);
            error = deserializeJson(doc, (char *)outBuf, outLen);
            free(outBuf);
          } else {
            error = deserializeJson(doc, s);
          }

          if (!error) {
            wifiClient.flush();
            http.end();
            return cb(doc);
          } else {
            Serial.print(F("Parse JSON failed, error: "));
            Serial.println(error.c_str());
            shouldRetry = error == DeserializationError::IncompleteInput;
          }
        } else {
          Serial.print(F("Get failed, error: "));
          if (httpCode < 0) {
            Serial.println(http.errorToString(httpCode));
            shouldRetry = httpCode == HTTPC_ERROR_CONNECTION_REFUSED ||
                          httpCode == HTTPC_ERROR_CONNECTION_LOST ||
                          httpCode == HTTPC_ERROR_READ_TIMEOUT;
          } else {
            Serial.println(httpCode);
          }
        }
        wifiClient.flush();
        http.end();
      } else {
        Serial.println(F("Unable to connect"));
      }
      if (!shouldRetry)
        break;
      Serial.println(F("Retry after 10 second"));
      delay(5000);
    }
    return false;
  }

public:
  API() {
    // http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const char *encoding = "Content-Encoding";
    const char *headerKeys[1] = {};
    headerKeys[0] = encoding;
    http.collectHeaders(headerKeys, 1);

    wifiClient.setInsecure();
  }

  ~API() {}

  // 获取 HTTPClient
  HTTPClient &httpClient() { return http; }

  // 和风天气 - 实时天气: https://dev.qweather.com/docs/api/weather/weather-now/
  bool getWeatherNow(Weather &result, const char *host, const char *key,
                     const char *locid) {
    // return getRestfulAPI("https://www.baidu.com", [&result](JsonDocument&
    // json) {
    return getRestfulAPI("https://" + String(host) + "/v7/weather/now?key=" +
                             String(key) + "&location=" + String(locid),
                         [&result](JsonDocument &json) {
                           if (strcmp(json["code"], "200") != 0) {
                             Serial.print(F("Get weather failed, error: "));
                             Serial.println(json["code"].as<const char *>());
                             return false;
                           }
                           result.updateTime =
                               String(json["updateTime"].as<const char *>());
                           JsonObject now = json["now"];
                           result.time = now["obsTime"].as<const char *>();
                           result.temp = atoi(now["temp"]);
                           result.feelsLike = atoi(now["feelsLike"]);
                           result.humidity = atoi(now["humidity"]);
                           result.precip = atof(now["precip"]);
                           result.wind360 = atoi(now["wind360"]);
                           result.windDir = now["windDir"].as<const char *>();
                           result.windScale = atoi(now["windScale"]);
                           result.windSpeed = atoi(now["windSpeed"]);
                           result.icon = atoi(now["icon"]);
                           result.text = now["text"].as<const char *>();
                           return true;
                         });
  }

  // 和风天气 - 逐小时天气预报:
  // https://dev.qweather.com/docs/api/weather/weather-hourly-forecast/
  bool getForecastHourly(HourlyForecast &result, const char *host,
                         const char *key, const char *locid) {
    return getRestfulAPI(
        "https://" + String(host) + "/v7/weather/24h?key=" + String(key) +
            "&location=" + String(locid),
        [&result](JsonDocument &json) {
          if (strcmp(json["code"], "200") != 0) {
            Serial.print(F("Get hourly forecast failed, error: "));
            Serial.println(json["code"].as<const char *>());
            return false;
          }
          uint8_t i, hours = json["hourly"].size();
          for (i = 0; i < result.length; i++) {
            if (i * result.interval >= hours)
              break;
            Weather &weather = result.weather[i];
            JsonObject hourly = json["hourly"][i * result.interval];
            weather.time = hourly["fxTime"].as<const char *>();
            weather.temp = atoi(hourly["temp"]);
            weather.humidity = atoi(hourly["humidity"]);
            weather.wind360 = atoi(hourly["wind360"]);
            weather.windDir = hourly["windDir"].as<const char *>();
            weather.windScale = atoi(hourly["windScale"]);
            weather.windSpeed = atoi(hourly["windSpeed"]);
            weather.icon = atoi(hourly["icon"]);
            weather.text = hourly["text"].as<const char *>();
          }
          result.length = i;
          return true;
        });
  }

  // 和风天气 - 逐天天气预报:
  // https://dev.qweather.com/docs/api/weather/weather-daily-forecast/
  bool getForecastDaily(DailyForecast &result, const char *host,
                        const char *key, const char *locid) {
    return getRestfulAPI(
        "https://" + String(host) + "/v7/weather/3d?key=" + String(key) +
            "&location=" + String(locid),
        [&result](JsonDocument &json) {
          if (strcmp(json["code"], "200") != 0) {
            Serial.print(F("Get daily forecast failed, error: "));
            Serial.println(json["code"].as<const char *>());
            return false;
          }
          result.updateTime = String(json["updateTime"].as<const char *>());
          uint8_t i;
          for (i = 0; i < result.length; i++) {
            DailyWeather &weather = result.weather[i];
            JsonObject daily = json["daily"][i];
            weather.date = daily["fxDate"].as<const char *>();
            weather.sunrise = daily["sunrise"].as<const char *>();
            weather.sunset = daily["sunset"].as<const char *>();
            weather.moonPhase = daily["moonPhase"].as<const char *>();
            weather.moonPhaseIcon = atoi(daily["moonPhaseIcon"]);
            weather.tempMax = atoi(daily["tempMax"]);
            weather.tempMin = atoi(daily["tempMin"]);
            weather.humidity = atoi(daily["humidity"]);
            weather.precip = atof(daily["precip"]);
            weather.iconDay = atoi(daily["iconDay"]);
            weather.textDay = daily["textDay"].as<const char *>();
            weather.iconNight = atoi(daily["iconNight"]);
            weather.textNight = daily["textNight"].as<const char *>();
            weather.wind360Day = atoi(daily["wind360Day"]);
            weather.windDirDay = daily["windDirDay"].as<const char *>();
            weather.windScaleDay = atoi(daily["windScaleDay"]);
            weather.windSpeedDay = atoi(daily["windSpeedDay"]);
            weather.wind360Night = atoi(daily["wind360Night"]);
            weather.windDirNight = daily["windDirNight"].as<const char *>();
            weather.windScaleNight = atoi(daily["windScaleNight"]);
            weather.windSpeedNight = atoi(daily["windSpeedNight"]);
          }
          result.length = i;
          return true;
        });
  }

  // 空气质量 API: /airquality/v1/current/{lat}/{lon}
  // 注意: lat在前, lon在后
  bool getAirQuality(AirQuality &result, const char *host, const char *key,
                     const char *lat, const char *lon) {
    return getRestfulAPI(
        "https://" + String(host) + "/airquality/v1/current/" + String(lat) +
            "/" + String(lon) + "?key=" + String(key),
        [&result](JsonDocument &json) {
          // 检查是否有 indexes 数组
          if (!json["indexes"].is<JsonArray>() || json["indexes"].size() == 0) {
            Serial.println(F("Air quality: no indexes data"));
            return false;
          }
          // 使用第一个 index（通常是 us-epa）
          JsonObject idx = json["indexes"][0];
          result.aqi = idx["aqi"].as<int16_t>();
          result.category = idx["category"].as<const char *>();
          result.primaryPollutant =
              idx["primaryPollutant"]["name"].as<const char *>();
          return true;
        });
  }

  // 分钟级降水 API: /v7/minutely/5m?location={lon},{lat}
  // 注意: location 参数是 经度,纬度 格式
  bool getMinutelyPrecip(MinutelyPrecip &result, const char *host,
                         const char *key, const char *loc) {
    return getRestfulAPI(
        "https://" + String(host) + "/v7/minutely/5m?key=" + String(key) +
            "&location=" + String(loc),
        [&result](JsonDocument &json) {
          if (strcmp(json["code"], "200") != 0) {
            Serial.print(F("Get minutely precip failed, error: "));
            Serial.println(json["code"].as<const char *>());
            return false;
          }
          result.summary = String(json["summary"].as<const char *>());
          return true;
        });
  }

  // 和风天气 - 城市查询: /geo/v2/city/lookup?location={lon},{lat}&key={key}
  bool getCityLookup(GeoLocation &result, const char *host, const char *key,
                     const char *loc) {
    return getRestfulAPI("https://" + String(host) +
                             "/geo/v2/city/lookup?key=" + String(key) +
                             "&location=" + String(loc),
                         [&result](JsonDocument &json) {
                           if (strcmp(json["code"], "200") != 0) {
                             Serial.print(F("Get city lookup failed, error: "));
                             Serial.println(json["code"].as<const char *>());
                             return false;
                           }
                           if (!json["location"].is<JsonArray>() ||
                               json["location"].size() == 0) {
                             Serial.println(F("City lookup: no location data"));
                             return false;
                           }
                           JsonObject loc0 = json["location"][0];
                           result.name = loc0["name"].as<const char *>();
                           return true;
                         });
  }
};
#endif // __API_HPP__