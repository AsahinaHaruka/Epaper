#include "weather.h"

#include <_preference.h>

TaskHandle_t WEATHER_HANDLER;

String _qweather_host;
String _qweather_key;
String _qweather_lat;
String _qweather_lon;

int8_t _weather_status = -1;
int8_t _weather_type = -1;
Weather _weather_now = {};
DailyWeather dailyWeather[3] = {}; // 3天天气预报
DailyForecast _daily_forecast = {.weather = dailyWeather, .length = 3};
AirQuality _air_quality = {};
MinutelyPrecip _minutely_precip = {};
GeoLocation _geo_location = {};

int8_t weather_type() { return _weather_type; }

int8_t weather_status() { return _weather_status; }
Weather *weather_data_now() { return &_weather_now; }
DailyForecast *weather_data_daily() { return &_daily_forecast; }
AirQuality *weather_data_aqi() { return &_air_quality; }
MinutelyPrecip *weather_data_minutely() { return &_minutely_precip; }
String weather_city_name() { return _geo_location.name; }

void task_weather(void *param) {
  Serial.println("[Task] get weather begin...");

  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  _weather_type =
      pref.getString(PREF_QWEATHER_TYPE).compareTo("1") == 0 ? 1 : 0;
  pref.end();

  Serial.printf("Weather Type: %d\n", _weather_type);

  // 拼接 location 参数: 经度,纬度 (和风天气格式)
  String locParam = _qweather_lon + "," + _qweather_lat;

  API<> api;

  // 获取实时天气（用于今日当前温度、体感温度）
  bool nowSuccess = api.getWeatherNow(_weather_now, _qweather_host.c_str(),
                                      _qweather_key.c_str(), locParam.c_str());
  if (nowSuccess) {
    Serial.println("[Weather] Real-time data fetched successfully");
  } else {
    Serial.println("[Weather] Failed to fetch real-time data");
  }

  // 获取3天天气预报（用于温度范围、风力、降水量）
  bool dailySuccess =
      api.getForecastDaily(_daily_forecast, _qweather_host.c_str(),
                           _qweather_key.c_str(), locParam.c_str());
  if (dailySuccess) {
    Serial.println("[Weather] Daily forecast fetched successfully");
  } else {
    Serial.println("[Weather] Failed to fetch daily forecast");
  }

  // 获取空气质量（纬度在前，经度在后）
  bool aqiSuccess = api.getAirQuality(
      _air_quality, _qweather_host.c_str(), _qweather_key.c_str(),
      _qweather_lat.c_str(), _qweather_lon.c_str());
  if (aqiSuccess) {
    Serial.printf("[Weather] Air quality: AQI=%d, %s\n", _air_quality.aqi,
                  _air_quality.category.c_str());
  } else {
    Serial.println("[Weather] Failed to fetch air quality");
  }

  // 获取分钟级降水
  bool minutelySuccess =
      api.getMinutelyPrecip(_minutely_precip, _qweather_host.c_str(),
                            _qweather_key.c_str(), locParam.c_str());
  if (minutelySuccess) {
    Serial.printf("[Weather] Minutely precip: %s\n",
                  _minutely_precip.summary.c_str());
  } else {
    Serial.println("[Weather] Failed to fetch minutely precip");
  }

  // 获取城市名称（通过经纬度反查）
  bool geoSuccess = api.getCityLookup(_geo_location, _qweather_host.c_str(),
                                      _qweather_key.c_str(), locParam.c_str());
  if (geoSuccess) {
    Serial.printf("[Weather] City: %s\n", _geo_location.name.c_str());
  } else {
    Serial.println("[Weather] Failed to fetch city name");
  }

  // 只要有一个成功就认为成功
  if (nowSuccess || dailySuccess) {
    _weather_status = 1;
  } else {
    _weather_status = 2;
  }

  Serial.println("[Task] get weather end...");
  WEATHER_HANDLER = NULL;
  vTaskDelete(NULL);
}

void weather_exec(int status) {
  _weather_status = status;
  if (status > 0) {
    return;
  }

  if (!WiFi.isConnected()) {
    _weather_status = 2;
    return;
  }

  // Preference 获取配置信息。
  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  _qweather_host = pref.getString(PREF_QWEATHER_HOST, "api.qweather.com");
  _qweather_key = pref.getString(PREF_QWEATHER_KEY, "");
  _qweather_lat = pref.getString(PREF_QWEATHER_LAT, "");
  _qweather_lon = pref.getString(PREF_QWEATHER_LON, "");
  pref.end();

  if (_qweather_key.length() == 0 || _qweather_lat.length() == 0 ||
      _qweather_lon.length() == 0) {
    Serial.println("Qweather key/location invalid.");
    _weather_status = 3;
    return;
  }

  if (WEATHER_HANDLER != NULL) {
    vTaskDelete(WEATHER_HANDLER);
    WEATHER_HANDLER = NULL;
  }
  xTaskCreate(task_weather, "WeatherData", 1024 * 10, NULL, 2,
              &WEATHER_HANDLER);
}

void weather_stop() {
  if (WEATHER_HANDLER != NULL) {
    vTaskDelete(WEATHER_HANDLER);
    WEATHER_HANDLER = NULL;
  }
  _weather_status = 2;
}
