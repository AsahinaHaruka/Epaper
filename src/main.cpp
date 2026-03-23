#include "esp_sleep.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "SensorManager.h"
#include "_preference.h"
#include "battery.h"
#include "config.h"
#include "display_driver.h" // Display globals
#include "screen_ink.h"
#include "todo.h"
#include "version.h"
#include "weather.h"
#include "countdown.h"
#include "wiring.h"

// Configuration timeout constants
#define CONFIG_BUTTON_WINDOW_MS 3000
#define WIFI_CONNECT_TIMEOUT_SEC 10
#define CONFIG_PORTAL_TIMEOUT_SEC 180
#define NTP_SYNC_MAX_RETRIES 20
#define NTP_SYNC_DELAY_MS 500

// Sleep duration constants
#define MINUTE_IN_SECONDS 60
#define HOUR_IN_SECONDS 3600
#define LOW_BATTERY_SLEEP_SEC 3600
#define RETRY_SLEEP_SEC 60
#define ERROR_RETRY_SLEEP_SEC 600

// RTC memory (persistent across deep sleep)
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR time_t lastFullSync = 0;
RTC_DATA_ATTR int cachedBatteryPct = -1; // 缓存电量百分比，仅每小时刷新

// Cached sensor data (read before WiFi to avoid chip heat affecting readings)
SensorData cachedSensorData = {0, 0, false};

// Global objects
WiFiManager wm;
WiFiManagerParameter para_qweather_host("qweather_host", "和风天气Host", "",
                                        64);
WiFiManagerParameter para_qweather_key("qweather_key", "和风天气API Key", "",
                                       32);
WiFiManagerParameter para_qweather_lat("qweather_lat", "纬度(如:39.92)", "",
                                       16);
WiFiManagerParameter para_qweather_lon("qweather_lon", "经度(如:116.41)", "",
                                       16);
WiFiManagerParameter para_ms_client_id("ms_client_id", "MS Todo Client ID", "",
                                       48);
WiFiManagerParameter para_ms_tenant_id("ms_tenant_id", "MS Todo Tenant ID",
                                       "consumers", 48);
WiFiManagerParameter para_cd_url("cd_url", "倒数日URL(不填则用本地)", "", 64);
WiFiManagerParameter para_cd_local("cd_local",
    "本地倒计日(分号分隔: MM-DD,名称,农历0/1)", "", 200);
SensorManager sensor;

// Flag to track if config was saved
bool shouldSaveConfig = false;

// ============================================================================
// FILESYSTEM HELPERS
// ============================================================================

/**
 * Initialize LittleFS with format-on-fail option
 * @return true if mounted successfully, false otherwise
 */
bool initLittleFS() {
  // formatOnFail=true will format the filesystem if corrupted
  if (!LittleFS.begin(true)) {
    Serial.println("Failed to mount LittleFS even after format attempt");
    return false;
  }
  Serial.println("LittleFS mounted successfully");
  return true;
}

/**
 * Load HTML template from LittleFS
 * @param filename Path to file in LittleFS (e.g., "/portal_header.html")
 * @return File contents as String, or empty string on error
 */
String loadTemplate(const char *filename) {
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.printf("Failed to open file: %s\n", filename);
    return String("");
  }

  String content = file.readString();
  file.close();
  return content;
}

// ============================================================================
// WIFI CONFIGURATION FUNCTIONS
// ============================================================================

/**
 * Callback function when WiFiManager saves configuration
 */
void saveConfigCallback() {
  Serial.println("Configuration saved callback triggered");
  shouldSaveConfig = true;
}

/**
 * Load saved configuration from Preferences and populate WiFiManager parameters
 */
void loadSavedConfig() {
  Preferences pref;
  pref.begin(PREF_NAMESPACE, true); // Read-only mode

  String qweather_host = pref.getString(PREF_QWEATHER_HOST, "");
  String qweather_key = pref.getString(PREF_QWEATHER_KEY, "");
  String qweather_lat = pref.getString(PREF_QWEATHER_LAT, "");
  String qweather_lon = pref.getString(PREF_QWEATHER_LON, "");
  String ms_client_id = pref.getString(PREF_MS_CLIENT_ID, "");
  String ms_tenant_id = pref.getString(PREF_MS_TENANT_ID, "consumers");
  String cd_url = pref.getString(PREF_CD_URL, "");
  String cd_local = pref.getString(PREF_CD_LOCAL, "");

  pref.end();

  // Update WiFiManager parameter default values
  if (qweather_host.length() > 0) {
    new (&para_qweather_host) WiFiManagerParameter(
        "qweather_host", "和风天气Host", qweather_host.c_str(), 64);
  }
  if (qweather_key.length() > 0) {
    new (&para_qweather_key) WiFiManagerParameter(
        "qweather_key", "和风天气API Key", qweather_key.c_str(), 32);
  }
  if (qweather_lat.length() > 0) {
    new (&para_qweather_lat) WiFiManagerParameter(
        "qweather_lat", "纬度(如:39.92)", qweather_lat.c_str(), 16);
  }
  if (qweather_lon.length() > 0) {
    new (&para_qweather_lon) WiFiManagerParameter(
        "qweather_lon", "经度(如:116.41)", qweather_lon.c_str(), 16);
  }
  if (ms_client_id.length() > 0) {
    new (&para_ms_client_id) WiFiManagerParameter(
        "ms_client_id", "MS Todo Client ID", ms_client_id.c_str(), 48);
  }
  if (ms_tenant_id.length() > 0) {
    new (&para_ms_tenant_id) WiFiManagerParameter(
        "ms_tenant_id", "MS Todo Tenant ID", ms_tenant_id.c_str(), 48);
  }
  if (cd_url.length() > 0) {
    new (&para_cd_url) WiFiManagerParameter("cd_url", "倒数日URL(不填则用本地)", cd_url.c_str(), 64);
  }
  if (cd_local.length() > 0) {
    new (&para_cd_local) WiFiManagerParameter("cd_local",
        "本地倒计日(分号分隔: MM-DD,名称,农历0/1)", cd_local.c_str(), 200);
  }

  Serial.println("Loaded saved configuration:");
  Serial.printf("  QWeather Host: %s\n", qweather_host.c_str());
  Serial.printf("  QWeather Key: %s\n",
                qweather_key.length() > 0 ? "***" : "(empty)");
  Serial.printf("  QWeather Lat: %s, Lon: %s\n", qweather_lat.c_str(),
                qweather_lon.c_str());
}

/**
 * Save WiFiManager parameters to Preferences
 */
void saveWiFiConfig() {
  Serial.println("Saving configuration to Preferences...");
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);

  String qweather_host = para_qweather_host.getValue();
  String qweather_key = para_qweather_key.getValue();
  String qweather_lat = para_qweather_lat.getValue();
  String qweather_lon = para_qweather_lon.getValue();
  String ms_client_id = para_ms_client_id.getValue();
  String ms_tenant_id = para_ms_tenant_id.getValue();
  String cd_url = para_cd_url.getValue();
  String cd_local = para_cd_local.getValue();

  if (qweather_host.length() > 0) {
    pref.putString(PREF_QWEATHER_HOST, qweather_host);
    Serial.printf("Saved QWeather Host: %s\n", qweather_host.c_str());
  }
  if (qweather_key.length() > 0) {
    pref.putString(PREF_QWEATHER_KEY, qweather_key);
    Serial.printf("Saved QWeather Key: %s\n", qweather_key.c_str());
  }
  if (qweather_lat.length() > 0) {
    pref.putString(PREF_QWEATHER_LAT, qweather_lat);
    Serial.printf("Saved QWeather Lat: %s\n", qweather_lat.c_str());
  }
  if (qweather_lon.length() > 0) {
    pref.putString(PREF_QWEATHER_LON, qweather_lon);
    Serial.printf("Saved QWeather Lon: %s\n", qweather_lon.c_str());
  }
  if (ms_client_id.length() > 0) {
    pref.putString(PREF_MS_CLIENT_ID, ms_client_id);
    Serial.printf("Saved MS Client ID: %s\n", ms_client_id.c_str());
  }
  if (ms_tenant_id.length() > 0) {
    pref.putString(PREF_MS_TENANT_ID, ms_tenant_id);
    Serial.printf("Saved MS Tenant ID: %s\n", ms_tenant_id.c_str());
  }

  pref.putString(PREF_CD_URL, cd_url);
  pref.putString(PREF_CD_LOCAL, cd_local);

  pref.end();
  Serial.println("Configuration saved successfully!");
}

/**
 * Setup WiFi connection and configuration portal
 * @param forceConfig If true, force config portal to open
 * @return true if WiFi connected successfully, false otherwise
 */
bool setupWiFi(bool forceConfig) {
  Serial.println(forceConfig ? "=== FORCED CONFIG MODE ==="
                             : "=== WiFi Setup ===");

  // Load saved configuration
  loadSavedConfig();

  // Configure WiFiManager
  wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_SEC);
  wm.setSaveConfigCallback(saveConfigCallback);

  // Add custom parameters
  wm.addParameter(&para_qweather_host);
  wm.addParameter(&para_qweather_key);
  wm.addParameter(&para_qweather_lat);
  wm.addParameter(&para_qweather_lon);
  wm.addParameter(&para_ms_client_id);
  wm.addParameter(&para_ms_tenant_id);
  wm.addParameter(&para_cd_url);
  wm.addParameter(&para_cd_local);

  // Load custom HTML from filesystem
  String portalHeader = loadTemplate("/portal_header.html");
  if (portalHeader.length() > 0) {
    wm.setCustomHeadElement(portalHeader.c_str());
  }

  // Set up web server callback to add custom endpoints
  wm.setWebServerCallback([]() {
    // Save endpoint - saves config without restarting
    wm.server->on("/save", []() {
      saveWiFiConfig();
      String response = loadTemplate("/save_success.html");
      wm.server->send(200, "text/html", response);
    });

    // Restart endpoint - saves config and restarts device
    wm.server->on("/restart", []() {
      saveWiFiConfig();
      String response = loadTemplate("/restart_page.html");
      wm.server->send(200, "text/html", response);
      delay(1000);
      ESP.restart();
    });
  });

  // Add custom HTML for save and restart buttons (loaded from filesystem)
  String buttonsHtml = loadTemplate("/custom_buttons.html");
  WiFiManagerParameter custom_buttons(buttonsHtml.c_str());
  wm.addParameter(&custom_buttons);

  // Set config portal timeout
  wm.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT_SEC);

  // Connect or start config portal
  bool wifiConnected;
  if (forceConfig) {
    Serial.println("Starting config portal...");
    wifiConnected = wm.startConfigPortal("EPaper-Config");
  } else {
    wifiConnected = wm.autoConnect("EPaper-Config");
  }

  if (!wifiConnected) {
    Serial.println("WiFi connection failed");
    return false;
  }

  Serial.println("WiFi connected");

  // Save configuration if callback was triggered
  if (shouldSaveConfig) {
    saveWiFiConfig();
    shouldSaveConfig = false;
  }

  return true;
}

// ============================================================================
// TIME SYNCHRONIZATION
// ============================================================================

/**
 * Start asynchronous SNTP synchronization
 */
void startNTPTimeSync() {
  Serial.println("Starting NTP time sync in background...");

  // Set timezone to Beijing time (UTC+8) before NTP sync
  setenv("TZ", "CST-8", 1);
  tzset();

  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
}

/**
 * Wait for system time to synchronize with NTP servers
 * @return true if sync successful, false otherwise
 */
bool waitNTPTimeSync() {
  // Wait for time sync
  int retry = 0;
  struct tm timeinfo;
  time_t now;

  time(&now);
  localtime_r(&now, &timeinfo);

  while (timeinfo.tm_year + 1900 < 2025 && retry < NTP_SYNC_MAX_RETRIES) {
    delay(NTP_SYNC_DELAY_MS);
    time(&now);
    localtime_r(&now, &timeinfo);
    retry++;
    Serial.printf("NTP sync attempt %d/%d...\n", retry, NTP_SYNC_MAX_RETRIES);
  }

  Serial.printf("NTP Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  if (timeinfo.tm_year + 1900 < 2025) {
    Serial.println("NTP sync failed - time still invalid");
    return false;
  }

  Serial.println("NTP sync successful!");
  return true;
}

// ============================================================================
// WEATHER AND DISPLAY UPDATE
// ============================================================================

/**
 * Fetch weather data and update display
 * @return true if successful, false otherwise
 */
bool updateWeatherAndDisplay() {
  // === Parallel phase: NTP SNTP + weather + calendar/holiday ===
  // Start weather fetch (runs as FreeRTOS task internally)
  Serial.println("Fetching weather data (parallel with NTP)...");
  weather_exec();

  // Wait for NTP sync to complete before fetching calendar/holiday
  // (because calendar depends on the accurate current date/time)
  if (!waitNTPTimeSync()) {
    return false; // Return false if NTP times out
  }

  // Now NTP is complete and weather is fetching concurrently.
  // Prepare calendar + holiday data concurrently with weather.
  Serial.println(
      "Fetching calendar and holiday data (parallel with weather)...");
  si_calendar();

  // Wait for weather to complete
  while (weather_status() <= 0) {
    delay(100);
  }

  // Check if weather configuration is valid
  if (weather_status() == 3) {
    Serial.println("Weather configuration missing");
    return false;
  }

  Serial.println("Weather data fetched successfully");

  // === Sequential phase: todo & countdown (needs most heap for SSL) ===
  // Run after weather task exits to maximize available heap
  Serial.println("Fetching todo items...");
  todo_exec();
  if (todo_status() == 1) {
    Serial.printf("Todo: %d items fetched\n", todo_data()->count);
  } else {
    Serial.println("Todo: skipped or not configured");
  }

  Serial.println("Updating countdown items...");
  countdown_exec();

  // Disconnect WiFi after all network requests are done
  WiFi.mode(WIFI_OFF);

  // Full screen update (display only, calendar data already prepared)
  Serial.println("Updating display...");
  si_screen_display_only();
  while (si_screen_status() <= 0) {
    delay(100);
  }

  // Check if screen update failed
  if (si_screen_status() == 2) {
    Serial.println("Screen update failed");
    return false;
  }

  Serial.println("Display updated successfully");
  return true;
}

// ============================================================================
// CONFIG MODE DETECTION
// ============================================================================

/**
 * Check if user wants to enter config mode
 * Waits for CONFIG_BUTTON_WINDOW_MS and checks for KEY3 press
 * @return true if config mode requested, false otherwise
 */
bool checkConfigMode() {
  Serial.println("===========================================");
  Serial.println("Press KEY3 to enter config mode");
  Serial.println("(3 second window)");
  Serial.println("===========================================");

  pinMode(KEY3, INPUT);

  unsigned long startTime = millis();
  while (millis() - startTime < CONFIG_BUTTON_WINDOW_MS) {
    if (digitalRead(KEY3) == LOW) {
      delay(50); // Debounce
      if (digitalRead(KEY3) == LOW) {
        Serial.println("*** CONFIG MODE TRIGGERED! ***");

        // Wait for button release
        while (digitalRead(KEY3) == LOW) {
          delay(10);
        }

        // Display config mode message on screen
        si_warning("配置模式\n\n连接WiFi热点:\nEPaper-Config\n\n进行配置");

        return true;
      }
    }
    delay(100);
  }

  Serial.println(
      "No config button press detected, continuing normal operation...");
  return false;
}

// ============================================================================
// POWER MANAGEMENT
// ============================================================================

/**
 * Enter deep sleep mode
 * @param seconds Duration to sleep in seconds
 */
void go_sleep(int seconds) {
  Serial.printf("Going to sleep for %d seconds\n", seconds);

  // Power optimization - reset GPIO pins
  gpio_reset_pin((gpio_num_t)EPD_CS);
  gpio_reset_pin((gpio_num_t)EPD_DC);
  gpio_reset_pin((gpio_num_t)EPD_RST);
  gpio_reset_pin((gpio_num_t)EPD_BUSY);

  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);

  Serial.flush();
  esp_deep_sleep_start();
}

/**
 * Print wakeup reason for debugging
 */
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("Wakeup caused by timer");
    break;
  default:
    Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
  }
}

// ============================================================================
// MAIN SETUP AND LOOP
// ============================================================================

void setup() {
  delay(10);
  Serial.begin(115200);

  ++bootCount;

  // Determine wakeup cause early for fast-path decision
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Determine if hourly full sync is needed
  time_t now;
  time(&now);
  bool needFullSync = (bootCount == 1) ||
                      (now - lastFullSync >= HOUR_IN_SECONDS) ||
                      (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED);

  if (!needFullSync) {
    // ==================================================================
    // FAST PATH: MINUTE UPDATE (Partial screen refresh - clock only)
    // No WiFi, no sensor, no battery check — just update the clock
    // ==================================================================
    Serial.println("=== MINUTE UPDATE ===");

    // 深度睡眠后时区环境变量丢失，需要重新设置
    setenv("TZ", "CST-8", 1);
    tzset();

    updateClockOnly();

    // Calculate sleep duration until next minute
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int secondsToNextMinute = 60 - timeinfo.tm_sec;
    go_sleep(secondsToNextMinute);
    return; // Should never reach here
  }

  // ==================================================================
  // FULL SYNC PATH: Heavy initialization starts here
  // ==================================================================
  Serial.printf("\n\nESP32 E-Paper Calendar v%s\n", FIRMWARE_VERSION);
  print_wakeup_reason();
  Serial.printf("Boot count: %d\n", bootCount);

  // Check battery voltage
  int voltage = readBatteryVoltage();
  Serial.printf("Battery: %d mV\n", voltage);
  if (voltage < 3000 && voltage > 1000) {
    Serial.println("Low battery, entering deep sleep");
    go_sleep(LOW_BATTERY_SLEEP_SEC);
  }

  // Power on SHT30 sensor to ensure I2C bus is not pulled low by floating GND
  pinMode(WAKE_IO_PIN, OUTPUT);
  digitalWrite(WAKE_IO_PIN, HIGH);
  delay(50);

  // Initialize sensor and read IMMEDIATELY (before WiFi heats up the chip)
  sensor.begin(SHT_SDA, SHT_SCL);
  cachedSensorData = sensor.read();

  // Check if user wants to enter config mode (only on initial boot)
  bool forceConfig = false;
  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
    forceConfig = checkConfigMode();
  }

  Serial.println(forceConfig ? "=== CONFIG MODE ==="
                             : "=== HOURLY FULL SYNC ===");

  // Initialize LittleFS for config portal templates
  initLittleFS();

  // Setup WiFi
  if (!setupWiFi(forceConfig)) {
    Serial.println("WiFi setup failed");
    go_sleep(ERROR_RETRY_SLEEP_SEC);
  }

  // Start SNTP background request immediately after WiFi connects
  startNTPTimeSync();

  // 更新电量缓存（每小时仅一次）
  cachedBatteryPct = readBatteryPercent();
  Serial.printf("Battery: %d%%\n", cachedBatteryPct);

  // Fetch weather and update display
  if (!updateWeatherAndDisplay()) {
    WiFi.mode(WIFI_OFF);
    si_warning("和风天气未配置\n\n按KEY3\n进入配置模式");
    go_sleep(RETRY_SLEEP_SEC);
  }

  // Update last sync time
  time(&now);
  lastFullSync = now;

  // Calculate sleep duration until next minute boundary
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  int secondsToNextMinute = 60 - timeinfo.tm_sec;
  go_sleep(secondsToNextMinute);
}

void loop() {
  // Should never reach here due to deep sleep
  delay(1000);
}