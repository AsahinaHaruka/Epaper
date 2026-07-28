// Screen display module for 7.5" E-Paper (800x480)
// Left side: Calendar with lunar dates (480px)
// Right side: Clock and sensors (320px)

#include "screen_ink.h"
#include "SensorManager.h"
#include "_preference.h"
#include "config.h"
#include "countdown.h"
#include "display_driver.h"
#include "font.h"
#include "holiday.h"
#include "icons.h"
#include "nongli.h"
#include "todo.h"
#include "weather.h"
#include <LittleFS.h>
#include <time.h>

// Fonts
#define FONT_TEXT u8g2_font_wqy16_t_gb2312
#define FONT_SUB u8g2_font_wqy12_t_gb2312
#define FONT_TODO u8g2_font_wqy14_t_gb2312

// Week day strings
const char *week_str[] = {"日", "一", "二", "三", "四", "五", "六"};

// Lunar calendar strings
const String nl10_str[] = {"初", "十", "廿", "卅"};
const String nl_str[] = {"十", "一", "二", "三", "四", "五",
                         "六", "七", "八", "九", "十"};
const String nl_mon_str[] = {"",   "正", "二", "三", "四", "五", "六",
                             "七", "八", "九", "十", "冬", "腊"};

// Holiday definitions
const int jrLength = 13;
const int jrDate[] = {101, 214, 308, 312,  501,  504, 601,
                      701, 801, 910, 1001, 1224, 1225};
const char *jrText[] = {"元旦",   "情人节", "妇女节", "植树节", "劳动节",
                        "青年节", "儿童节", "建党节", "建军节", "教师节",
                        "国庆节", "平安夜", "圣诞节"};

// Global state
int _screen_status = -1;
int _calendar_status = -1;
struct tm tmInfo = {0};
TaskHandle_t SCREEN_HANDLER;
int lunarDates[31];
int jqAccDate[24];
Holiday currentHoliday; // 当月节假日数据

// Dynamic layout values (computed per-render based on actual month row count)
int16_t g_calDayH =
    CAL_DAY_H; // Will be recalculated to fill space above weather panel

// External sensor manager
extern SensorManager sensor;

// Load weather icon bitmap from LittleFS
// Returns true if loaded successfully, buf must be large enough for size*size/8
// bytes
bool loadWeatherIcon(uint16_t iconCode, uint8_t size, uint8_t *buf) {
  char path[32];
  snprintf(path, sizeof(path), "/icons/%d_%d.bin", iconCode, size);
  File f = LittleFS.open(path, "r");
  if (!f) {
    // Fallback to 999 (unknown)
    snprintf(path, sizeof(path), "/icons/999_%d.bin", size);
    f = LittleFS.open(path, "r");
    if (!f)
      return false;
  }
  int bytes = (size / 8 + ((size % 8) ? 1 : 0)) * size; // bytes per row * rows
  f.read(buf, bytes);
  f.close();
  return true;
}

// Draw weather bitmap icon at position, with color
void drawWeatherBitmap(int16_t x, int16_t y, uint16_t iconCode, uint8_t size,
                       uint16_t color) {
  int bytesPerRow = (size + 7) / 8;
  int bufSize = bytesPerRow * size;
  uint8_t *buf = (uint8_t *)malloc(bufSize);
  if (!buf)
    return;
  if (loadWeatherIcon(iconCode, size, buf)) {
    display.drawXBitmap(x, y, buf, size, size, color);
  }
  free(buf);
}

// Draw calendar header (week day row)
void draw_cal_header() {
  u8g2Fonts.setFont(FONT_TEXT);
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setForegroundColor(GxEPD_WHITE);

  int16_t startX = (CAL_WIDTH - 7 * CAL_DAY_W) / 2;
  int16_t y = CAL_TOP_H;

  for (int i = 0; i < 7; i++) {
    uint16_t color = (i == 0 || i == 6) ? GxEPD_RED : GxEPD_BLACK;
    int16_t x = startX + i * CAL_DAY_W;

    // Fill background
    display.fillRect(x, y, CAL_DAY_W, CAL_HEADER_H, color);

    // Draw text
    int16_t textW = u8g2Fonts.getUTF8Width(week_str[i]);
    u8g2Fonts.setCursor(x + (CAL_DAY_W - textW) / 2, y + CAL_HEADER_H - 4);
    u8g2Fonts.print(week_str[i]);
  }

  // Fill edges
  display.fillRect(0, y, startX, CAL_HEADER_H, GxEPD_RED);
  display.fillRect(startX + 7 * CAL_DAY_W, y,
                   CAL_WIDTH - startX - 7 * CAL_DAY_W, CAL_HEADER_H, GxEPD_RED);
}

// Draw calendar year/month/day info at top
// Left: Date, Week Number, Day of Week, Lunar Date
// Right: Indoor/Outdoor Temperature and Humidity (aligned to calendar right
// edge)
void draw_cal_year_info() {
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

  // === Left Side: Date Info (2-line layout) ===
  // Line 1: YYYY年M月D日  [距xxx还有x天]
  u8g2Fonts.setFont(u8g2_font_fub25_tn);
  u8g2Fonts.setCursor(10, 30);
  u8g2Fonts.print(String(tmInfo.tm_year + 1900).c_str());
  u8g2Fonts.setFont(FONT_TEXT);
  u8g2Fonts.print("年");
  u8g2Fonts.setFont(u8g2_font_fub25_tn);
  u8g2Fonts.print(String(tmInfo.tm_mon + 1).c_str());
  u8g2Fonts.setFont(FONT_TEXT);
  u8g2Fonts.print("月");
  u8g2Fonts.setFont(u8g2_font_fub25_tn);
  u8g2Fonts.print(String(tmInfo.tm_mday).c_str());
  u8g2Fonts.setFont(FONT_TEXT);
  u8g2Fonts.print("日");

  // Countdown display (if configured)
  {
    CountdownData *cdData = countdown_data();
    if (cdData && cdData->count > 0) {
      // Find the nearest active countdown
      CountdownItem *nearest = nullptr;
      for (int i = 0; i < cdData->count; i++) {
        if (cdData->items[i].daysRemaining >= 0) {
          if (!nearest ||
              cdData->items[i].daysRemaining < nearest->daysRemaining) {
            nearest = &cdData->items[i];
          }
        }
      }

      if (nearest) {
        u8g2Fonts.setFont(FONT_TEXT);
        u8g2Fonts.setForegroundColor(GxEPD_RED);
        if (nearest->daysRemaining == 0) {
          u8g2Fonts.printf("  %s就是今天！", nearest->label);
        } else {
          u8g2Fonts.printf("  距%s还有%d天", nearest->label,
                           nearest->daysRemaining);
        }
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      }
    }
  }

  // Line 2: 乙巳年 蛇 腊月二十 第XX周  星期X
  char week_num[8];
  strftime(week_num, sizeof(week_num), "%V", &tmInfo);

  int lunarDate = lunarDates[tmInfo.tm_mday - 1];
  int lunarMon = abs(lunarDate) / 100;
  int adjustYear = (lunarMon > tmInfo.tm_mon + 1) ? 1 : 0;
  int tg = nl_tg(tmInfo.tm_year + 1900 - adjustYear);
  int dz = nl_dz(tmInfo.tm_year + 1900 - adjustYear);

  int lunarDay = abs(lunarDate) % 100;
  String lunarStr;
  if (lunarDay == 10) {
    lunarStr = "初十";
  } else if (lunarDay == 20) {
    lunarStr = "二十";
  } else if (lunarDay == 30) {
    lunarStr = "三十";
  } else {
    lunarStr = nl10_str[lunarDay / 10] + nl_str[lunarDay % 10];
  }

  u8g2Fonts.setFont(FONT_TEXT);
  u8g2Fonts.setCursor(10, CAL_TOP_H - 5);
  u8g2Fonts.printf("%s%s年 %s %s月%s  第%s周  星期%s", nl_tg_text[tg],
                   nl_dz_text[dz], nl_sx_text[dz], nl_mon_str[lunarMon].c_str(),
                   lunarStr.c_str(), week_num, week_str[tmInfo.tm_wday]);

  // === Right Side: Indoor/Outdoor Temp & Humidity ===
  // Right edge aligned with calendar grid right edge
  int16_t calStartX = (CAL_WIDTH - 7 * CAL_DAY_W) / 2;
  int16_t calRightEdge = calStartX + 7 * CAL_DAY_W; // Calendar grid right edge
  int16_t rightX = calRightEdge; // Align to calendar right edge
  int16_t tableY = 8;

  extern SensorData cachedSensorData;
  SensorData indoor = cachedSensorData;
  Weather *weather = weather_data_now();

  u8g2Fonts.setFont(FONT_TEXT); // 使用较大字体显示温湿度

  // === Icon-based display: house/tree icons + temperature/humidity icons ===
  // Column layout: [icon] │ [temp_icon] value  [humid_icon] value
  int16_t col1X = rightX - 155; // house/tree icon starts here
  int16_t colSepX = col1X + 15; // separator "│"
  int16_t col2X = colSepX + 15; // temperature icon
  int16_t col3X = col2X + 20;   // Temperature value starts here
  int16_t col4X = rightX - 50;  // humidity icon
  int16_t col5X = col4X + 20;   // Humidity value starts here

  // Row 1: Indoor (house icon) — icon vertically centered with text
  int16_t row1Y = tableY + 18;
  display.drawXBitmap(col1X, row1Y - ICON_HOUSE_H + 2, icon_house, ICON_HOUSE_W,
                      ICON_HOUSE_H, GxEPD_BLACK);
  u8g2Fonts.setCursor(colSepX, row1Y);
  u8g2Fonts.print("│");
  display.drawXBitmap(col2X, row1Y - ICON_TEMPERATURE_H + 2, icon_temperature,
                      ICON_TEMPERATURE_W, ICON_TEMPERATURE_H, GxEPD_BLACK);
  u8g2Fonts.setCursor(col3X, row1Y);
  if (indoor.valid) {
    u8g2Fonts.setForegroundColor(indoor.temperature >= 35.0 ? GxEPD_RED
                                                            : GxEPD_BLACK);
    u8g2Fonts.printf("%.1f°C", indoor.temperature);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  } else {
    u8g2Fonts.print("--°C");
  }
  display.drawXBitmap(col4X, row1Y - ICON_HUMIDITY_H + 2, icon_humidity,
                      ICON_HUMIDITY_W, ICON_HUMIDITY_H, GxEPD_BLACK);
  u8g2Fonts.setCursor(col5X, row1Y);
  if (indoor.valid) {
    u8g2Fonts.printf("%.0f% %", indoor.humidity);
  } else {
    u8g2Fonts.print("-- %");
  }

  // Row 2: Outdoor (tree icon)
  int16_t row2Y = tableY + 38;
  display.drawXBitmap(col1X, row2Y - ICON_TREE_H + 2, icon_tree, ICON_TREE_W,
                      ICON_TREE_H, GxEPD_BLACK);
  u8g2Fonts.setCursor(colSepX, row2Y);
  u8g2Fonts.print("│");
  display.drawXBitmap(col2X, row2Y - ICON_TEMPERATURE_H + 2, icon_temperature,
                      ICON_TEMPERATURE_W, ICON_TEMPERATURE_H, GxEPD_BLACK);
  u8g2Fonts.setCursor(col3X, row2Y);
  if (weather_status() == 1) {
    u8g2Fonts.setForegroundColor(weather->temp >= 35 ? GxEPD_RED : GxEPD_BLACK);
    u8g2Fonts.printf("%d°C", weather->temp);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  } else {
    u8g2Fonts.print("--°C");
  }
  display.drawXBitmap(col4X, row2Y - ICON_HUMIDITY_H + 2, icon_humidity,
                      ICON_HUMIDITY_W, ICON_HUMIDITY_H, GxEPD_BLACK);
  u8g2Fonts.setCursor(col5X, row2Y);
  if (weather_status() == 1) {
    u8g2Fonts.printf("%d% %", weather->humidity);
  } else {
    u8g2Fonts.print("-- %");
  }
}

// Draw calendar days grid
void draw_cal_days() {
  int monthNum = tmInfo.tm_mon + 1;
  int year = tmInfo.tm_year + 1900;

  // Calculate days in month
  int totalDays = 31;
  if (monthNum == 4 || monthNum == 6 || monthNum == 9 || monthNum == 11) {
    totalDays = 30;
  } else if (monthNum == 2) {
    bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    totalDays = isLeap ? 29 : 28;
  }

  // Calculate first day of month (0=Sunday)
  int wday1 = (tmInfo.tm_wday - (tmInfo.tm_mday - 1) % 7 + 7) % 7;

  int16_t startX = (CAL_WIDTH - 7 * CAL_DAY_W) / 2;
  int16_t startY = CAL_TOP_H + CAL_HEADER_H;

  int jqIndex = 0;
  int jrIndex = 0;

  for (int day = 1; day <= totalDays; day++) {
    int pos = wday1 + day - 1;
    int col = pos % 7;
    int row = pos / 7;

    int16_t x = startX + col * CAL_DAY_W;
    int16_t y = startY + row * g_calDayH;

    // Check if this day is a holiday or makeup workday from API
    int holidayType = 0; // 0: normal, 1: holiday(休), -1: makeup workday(班)
    for (int i = 0; i < currentHoliday.length; i++) {
      if (abs(currentHoliday.holidays[i]) == day) {
        holidayType = currentHoliday.holidays[i] > 0 ? 1 : -1;
        break;
      }
    }

    // Color: red for weekends or holidays, but 班 (makeup workday) always black
    uint16_t color;
    if (holidayType == -1) {
      color = GxEPD_BLACK; // 班: always black
    } else {
      color =
          (col == 0 || col == 6 || holidayType == 1) ? GxEPD_RED : GxEPD_BLACK;
    }
    u8g2Fonts.setForegroundColor(color);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

    // Draw day number (centered in cell, with lunar text grouped below)
    // Position day number and lunar text as a group in the center of the cell
    // Day number font height ~14px, lunar font height ~12px, gap ~4px → group
    // ~30px
    int16_t groupH = 30; // approximate total height of day+lunar group
    int16_t groupTopY =
        y + (g_calDayH - groupH) / 2; // vertically center the group
    u8g2Fonts.setFont(u8g2_font_fub14_tn);
    String dayStr = String(day);
    int16_t numW = u8g2Fonts.getUTF8Width(dayStr.c_str());
    int16_t numX = x + (CAL_DAY_W - numW) / 2;
    int16_t numY = groupTopY + 14; // baseline of day number
    u8g2Fonts.setCursor(numX, numY);
    u8g2Fonts.print(dayStr.c_str());

    // Draw holiday/workday marker (休/班) in top-right corner
    if (holidayType != 0) {
      u8g2Fonts.setFont(FONT_SUB);
      const char *marker = holidayType > 0 ? "休" : "班";
      u8g2Fonts.setForegroundColor(holidayType > 0 ? GxEPD_RED : GxEPD_BLACK);
      u8g2Fonts.setCursor(x + CAL_DAY_W - 16, y + 16);
      u8g2Fonts.print(marker);
    }

    // Highlight today
    if (day == tmInfo.tm_mday) {
      display.drawRoundRect(x + 2, y + 2, CAL_DAY_W - 4, g_calDayH - 4, 4,
                            GxEPD_RED);
      display.drawRoundRect(x + 3, y + 3, CAL_DAY_W - 6, g_calDayH - 6, 3,
                            GxEPD_RED);
    }

    // Draw lunar date / solar term / holiday
    String lunarStr = "";
    int lunarDate = lunarDates[day - 1];
    int isLeapMon = lunarDate < 0 ? 1 : 0;
    lunarDate = abs(lunarDate);
    int lunarMon = lunarDate / 100;
    int lunarDay = lunarDate % 100;

    // Check solar term
    bool isJq = false;
    int accDays = tmInfo.tm_yday + 1 - tmInfo.tm_mday + day;
    for (; jqIndex < 24; jqIndex++) {
      if (accDays < jqAccDate[jqIndex])
        break;
      if (accDays == jqAccDate[jqIndex]) {
        lunarStr = String(nl_jq_text[jqIndex]);
        isJq = true;
        break;
      }
    }

    // Check holiday
    bool isJr = false;
    int dateNum = monthNum * 100 + day;
    for (; jrIndex < jrLength; jrIndex++) {
      if (dateNum < jrDate[jrIndex])
        break;
      if (dateNum == jrDate[jrIndex]) {
        lunarStr = String(jrText[jrIndex]);
        isJr = true;
        break;
      }
    }

    // Lunar date if not solar term or holiday
    if (!isJq && !isJr) {
      if (lunarDay == 1) {
        lunarStr = (isLeapMon ? "闰" : "") + nl_mon_str[lunarMon] + "月";
      } else if (lunarDay == 10) {
        lunarStr = "初十";
      } else if (lunarDay == 20) {
        lunarStr = "二十";
      } else if (lunarDay == 30) {
        lunarStr = "三十";
      } else {
        lunarStr = nl10_str[lunarDay / 10] + nl_str[lunarDay % 10];
      }

      // Check lunar holidays
      if (lunarMon == 1 && lunarDay == 1)
        lunarStr = "春节";
      else if (lunarMon == 1 && lunarDay == 15)
        lunarStr = "元宵节";
      else if (lunarMon == 5 && lunarDay == 5)
        lunarStr = "端午节";
      else if (lunarMon == 7 && lunarDay == 7)
        lunarStr = "七夕";
      else if (lunarMon == 8 && lunarDay == 15)
        lunarStr = "中秋节";
      else if (lunarMon == 9 && lunarDay == 9)
        lunarStr = "重阳节";
    }

    // Draw lunar text
    u8g2Fonts.setFont(FONT_SUB);
    // 班 always black, otherwise red for weekends/holidays
    if (holidayType == -1) {
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    } else {
      u8g2Fonts.setForegroundColor(
          (col == 0 || col == 6 || holidayType == 1) ? GxEPD_RED : GxEPD_BLACK);
    }
    int16_t lunarW = u8g2Fonts.getUTF8Width(lunarStr.c_str());
    // Position lunar text right below day number (grouped together)
    int16_t groupH2 = 30;
    int16_t groupTopY2 = y + (g_calDayH - groupH2) / 2;
    int16_t lunarY = groupTopY2 + 28; // 14px below day number baseline
    u8g2Fonts.setCursor(x + (CAL_DAY_W - lunarW) / 2, lunarY);
    u8g2Fonts.print(lunarStr.c_str());
  }
}

// Draw weather panel below calendar (3-day forecast)
// Layout: Today (3/5, detailed) | Tomorrow (top-right) / Day After
// (bottom-right)
void draw_weather_panel() {
  if (weather_status() != 1)
    return;

  DailyForecast *wFc = weather_data_daily();
  Weather *wNow = weather_data_now();
  AirQuality *wAqi = weather_data_aqi();
  MinutelyPrecip *wMinutely = weather_data_minutely();
  if (wFc->length == 0)
    return;

  // Open LittleFS for icon loading
  LittleFS.begin(true);

  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

  // Draw separator line at top of weather panel
  display.drawFastHLine(0, WEATHER_PANEL_Y - 1, WEATHER_PANEL_W, GxEPD_BLACK);

  // Panel dimensions — today takes 3/5
  int16_t todayW = WEATHER_PANEL_W * 3 / 5;   // 288px
  int16_t futureW = WEATHER_PANEL_W - todayW; // 192px
  int16_t futureH = WEATHER_PANEL_H / 2;      // Each future day takes half

  // Draw vertical divider between today and future days
  display.drawFastVLine(todayW, WEATHER_PANEL_Y, WEATHER_PANEL_H, GxEPD_BLACK);

  // ========== TODAY (Left 3/5, full height) ==========
  if (wFc->length >= 1) {
    DailyWeather &today = wFc->weather[0];
    int16_t x = 0;
    int16_t y = WEATHER_PANEL_Y;
    int16_t lineH = 15;

    // Draw vertical "当前" label — vertically centered in panel
    u8g2Fonts.setFont(FONT_TEXT);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    int16_t labelCenterY = y + WEATHER_PANEL_H / 2;
    u8g2Fonts.setCursor(x + 4, labelCenterY - 8);
    u8g2Fonts.print("当");
    u8g2Fonts.setCursor(x + 4, labelCenterY + 20);
    u8g2Fonts.print("前");

    // Weather icon from LittleFS (48x48, vertically centered in panel)
    int16_t iconSize = 48;
    int16_t iconX = x + 28;
    int16_t iconY =
        y + (WEATHER_PANEL_H - iconSize - 16) / 2; // leave room for text below
    drawWeatherBitmap(iconX, iconY, wNow->icon, iconSize, GxEPD_RED);

    // Weather text below icon (horizontally centered with icon center)
    u8g2Fonts.setFont(FONT_TEXT);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    int16_t textW = u8g2Fonts.getUTF8Width(wNow->text.c_str());
    int16_t iconCenterX = iconX + iconSize / 2;
    u8g2Fonts.setCursor(iconCenterX - textW / 2, iconY + iconSize + 18);
    u8g2Fonts.print(wNow->text.c_str());

    // Detail rows (right side of today section)
    int16_t detailX = x + 80; // Start after icon area (wider for 48px icon)

    u8g2Fonts.setFont(FONT_SUB);

    int16_t labelX = detailX + 48;
    int16_t valueX = detailX + 50;

    // Calculate the number of detail rows dynamically
    int numRows =
        5; // Rows 1-5 (体感, 风力, 降水, 空气, 预报) are always present
    int splitBytes = 0;
    bool hasMinutely = (wMinutely->summary.length() > 0);
    if (hasMinutely) {
      numRows++; // baseline row for minutely summary
      for (int i = 0; i < 17 && wMinutely->summary[splitBytes]; i++) {
        uint8_t c = wMinutely->summary[splitBytes];
        if (c < 0x80)
          splitBytes += 1;
        else if (c < 0xE0)
          splitBytes += 2;
        else if (c < 0xF0)
          splitBytes += 3;
        else
          splitBytes += 4;
      }
      if (splitBytes < wMinutely->summary.length()) {
        numRows++; // second row when wrapped
      }
    }

    int totalTextH = numRows * lineH;
    int16_t detailY = y + (WEATHER_PANEL_H - totalTextH) / 2 +
                      10; // +10 to align baseline roughly center

    auto drawLabel = [&](int16_t y, const char *label) {
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      int16_t w = u8g2Fonts.getUTF8Width(label);
      u8g2Fonts.setCursor(labelX - w, y);
      u8g2Fonts.print(label);
      u8g2Fonts.setCursor(valueX, y);
    };

    // Row 1: 体感
    drawLabel(detailY, "体感: ");
    u8g2Fonts.setForegroundColor(wNow->feelsLike >= 35 ? GxEPD_RED
                                                       : GxEPD_BLACK);
    u8g2Fonts.printf("%d°C", wNow->feelsLike);

    // Row 2: 当前风力 (使用实时数据)
    detailY += lineH;
    drawLabel(detailY, "风力: ");
    u8g2Fonts.setForegroundColor(wNow->windScale >= 6 ? GxEPD_RED
                                                      : GxEPD_BLACK);
    u8g2Fonts.printf("%s %d级(%dkm/h)", wNow->windDir.c_str(), wNow->windScale,
                     wNow->windSpeed);

    // Row 3: 降水
    detailY += lineH;
    drawLabel(detailY, "降水: ");
    u8g2Fonts.setForegroundColor(wNow->precip >= 50.0 ? GxEPD_RED
                                                      : GxEPD_BLACK);
    u8g2Fonts.printf("%.1f mm", wNow->precip);

    // Row 4: 空气
    detailY += lineH;
    drawLabel(detailY, "空气: ");
    if (wAqi->category.length() > 0) {
      u8g2Fonts.setForegroundColor(wAqi->aqi >= 201 ? GxEPD_RED : GxEPD_BLACK);
      u8g2Fonts.printf("%s(%d)", wAqi->category.c_str(), wAqi->aqi);
    } else {
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      u8g2Fonts.print("--");
    }

    // Row 5: 预报
    detailY += lineH;
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    drawLabel(detailY, "预报: ");
    u8g2Fonts.setForegroundColor(today.tempMax >= 35 ? GxEPD_RED : GxEPD_BLACK);
    u8g2Fonts.printf("%s %d～%d°C %s%d级", today.textDay.c_str(), today.tempMin,
                     today.tempMax, today.windDirDay.c_str(),
                     today.windScaleDay);

    // Row 6: 分钟级降水
    if (hasMinutely) {
      detailY += lineH;
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      int16_t labelW = u8g2Fonts.getUTF8Width("空气: ");

      String text1 = wMinutely->summary.substring(0, splitBytes);
      u8g2Fonts.setCursor(labelX - labelW, detailY);
      u8g2Fonts.print(text1.c_str());

      if (splitBytes < wMinutely->summary.length()) {
        String text2 = wMinutely->summary.substring(splitBytes);
        detailY += lineH;
        u8g2Fonts.setCursor(labelX - labelW, detailY);
        u8g2Fonts.print(text2.c_str());
      }
    }
    // The Bottom-right location/update time has been moved to drawClock()
  }

  u8g2Fonts.setForegroundColor(GxEPD_BLACK);

  // Helper lambda to draw a future day (tomorrow or day-after)
  auto drawFutureDay = [&](DailyWeather &day, const char *label1,
                           const char *label2, int16_t x, int16_t y) {
    int16_t lineH = 14;
    int16_t halfH = futureH; // height of each future section

    // Vertical label — vertically centered in section
    u8g2Fonts.setFont(FONT_SUB);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    int16_t lblCY = y + halfH / 2;
    u8g2Fonts.setCursor(x + 4, lblCY - 2);
    u8g2Fonts.print(label1);
    u8g2Fonts.setCursor(x + 4, lblCY + 14);
    u8g2Fonts.print(label2);

    // Weather icon from LittleFS (24x24) — vertically centered
    int16_t iconSize = 24;
    int16_t iconX = x + 22;
    int16_t iconY = y + (halfH - iconSize - 14) / 2; // leave room for text
    drawWeatherBitmap(iconX, iconY, day.iconDay, iconSize, GxEPD_RED);

    // Weather text centered under icon
    u8g2Fonts.setFont(FONT_SUB);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    int16_t textW = u8g2Fonts.getUTF8Width(day.textDay.c_str());
    int16_t iconCenterX = iconX + iconSize / 2;
    u8g2Fonts.setCursor(iconCenterX - textW / 2, iconY + iconSize + 12);
    u8g2Fonts.print(day.textDay.c_str());

    // Detail rows (vertically centered)
    int16_t detailX = x + 56;
    int numRows = 3;
    int totalTextH = numRows * lineH;
    int16_t detailY = y + (halfH - totalTextH) / 2 + 10; // +10 for baseline

    // Row 1: 温度
    u8g2Fonts.setForegroundColor(day.tempMax >= 35 ? GxEPD_RED : GxEPD_BLACK);
    u8g2Fonts.setCursor(detailX, detailY);
    u8g2Fonts.printf("温度：%d°C～%d°C", day.tempMin, day.tempMax);

    // Row 2: 风力
    detailY += lineH;
    u8g2Fonts.setForegroundColor(day.windScaleDay >= 6 ? GxEPD_RED
                                                       : GxEPD_BLACK);
    u8g2Fonts.setCursor(detailX, detailY);
    u8g2Fonts.printf("风力：%s %d级", day.windDirDay.c_str(), day.windScaleDay);

    // Row 3: 降水
    detailY += lineH;
    u8g2Fonts.setForegroundColor(day.precip >= 50.0 ? GxEPD_RED : GxEPD_BLACK);
    u8g2Fonts.setCursor(detailX, detailY);
    u8g2Fonts.printf("降水：%.1f mm", day.precip);
  };

  // ========== TOMORROW (Right top) ==========
  if (wFc->length >= 2) {
    display.drawFastHLine(todayW, WEATHER_PANEL_Y + futureH, futureW,
                          GxEPD_BLACK);
    drawFutureDay(wFc->weather[1], "明", "日", todayW, WEATHER_PANEL_Y);
  }

  // ========== DAY AFTER TOMORROW (Right bottom) ==========
  if (wFc->length >= 3) {
    drawFutureDay(wFc->weather[2], "后", "日", todayW,
                  WEATHER_PANEL_Y + futureH);
  }

  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  LittleFS.end();
}

// Draw todo title "今日待办"
void drawTodoTitle(int16_t y) {
  u8g2Fonts.setFont(FONT_TEXT);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  const char *title = "今日待办";
  int16_t w = u8g2Fonts.getUTF8Width(title);
  u8g2Fonts.setCursor(CLOCK_X + (CLOCK_W - w) / 2, y);
  u8g2Fonts.print(title);

  // Red underline
  display.drawFastHLine(CLOCK_X + 20, y + 4, CLOCK_W - 40, GxEPD_RED);
}

// Draw clock (right side, 320x140 now)
void drawClock(int hour, int minute) {
  display.fillRect(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, GxEPD_WHITE);

  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

  // Battery percentage at top-right corner
  // 电压在主流程 WiFi 连接前采样并写入 RTC 缓存；drawClock
  // 随全屏刷新触发时缓存一定已有效，
  // 不再回退实时采样以避免射频/热噪声造成的读数跳变
  extern int cachedBatteryPct;
  u8g2Fonts.setFont(FONT_TEXT); // 16px font
  char batBuf[8];
  if (cachedBatteryPct >= 0) {
    snprintf(batBuf, sizeof(batBuf), "%d%%", cachedBatteryPct);
  } else {
    snprintf(batBuf, sizeof(batBuf), "--");
  }
  int16_t batW = u8g2Fonts.getUTF8Width(batBuf);
  // Draw power icon to the left of battery text
  int16_t batTextX = CLOCK_X + CLOCK_W - batW - 4; // Right-aligned battery
  int16_t topY = 16; // Battery text's bottom baseline
  display.drawXBitmap(batTextX - ICON_POWER_W - 4, topY - ICON_POWER_H + 2,
                      icon_power, ICON_POWER_W, ICON_POWER_H, GxEPD_BLACK);
  u8g2Fonts.setCursor(batTextX, topY);
  u8g2Fonts.print(batBuf);

  // Parse update time from wNow->updateTime
  String updateTimeStr = "--:--";
  Weather *wNow = weather_data_now();
  if (wNow->updateTime.length() >= 16) {
    updateTimeStr = wNow->updateTime.substring(11, 16); // "HH:MM"
  }

  // Get city name
  String cityName = weather_city_name();
  if (cityName.length() == 0)
    cityName = "--";

  char updateBuf[32];
  snprintf(updateBuf, sizeof(updateBuf), "更新于 %s", updateTimeStr.c_str());

  // Draw location and update info on the top-left of the clock space
  // on the same line as the battery info
  int16_t curX = CLOCK_X + 4;

  // [local_icon]
  display.drawXBitmap(curX, topY - ICON_LOCAL_H + 2, icon_local, ICON_LOCAL_W,
                      ICON_LOCAL_H, GxEPD_BLACK);
  curX += ICON_LOCAL_W + 4;

  // City Name
  u8g2Fonts.setCursor(curX, topY);
  u8g2Fonts.print(cityName.c_str());
  curX += u8g2Fonts.getUTF8Width(cityName.c_str()) +
          14; // move past city name + add gap

  // [update_icon]
  display.drawXBitmap(curX, topY - ICON_UPDATE_H + 2, icon_update,
                      ICON_UPDATE_W, ICON_UPDATE_H, GxEPD_BLACK);
  curX += ICON_UPDATE_W + 4;

  // Update time
  u8g2Fonts.setCursor(curX, topY);
  u8g2Fonts.print(updateBuf);

  // Clock time - moved up
  u8g2Fonts.setFont(u8g2_font_7Segments_26x42_mn);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);

  int16_t w = u8g2Fonts.getUTF8Width(buf);
  // Adjusted Y from 130 to 80
  u8g2Fonts.setCursor(CLOCK_X + (CLOCK_W - w) / 2, CLOCK_Y + 80);
  u8g2Fonts.print(buf);
}

// Helper: Draw a single UTF-8 character at position, return its height
// For vertical text rendering (one character per row, top-to-bottom)
static int16_t drawVerticalChar(int16_t x, int16_t y, const char *text,
                                int charIdx) {
  // Extract the charIdx-th UTF-8 character from text
  const uint8_t *p = (const uint8_t *)text;
  int idx = 0;
  while (*p && idx < charIdx) {
    int len = 1;
    if (*p >= 0xF0)
      len = 4;
    else if (*p >= 0xE0)
      len = 3;
    else if (*p >= 0xC0)
      len = 2;

    for (int i = 0; i < len && *p; i++) {
      p++;
    }
    idx++;
  }
  if (!*p)
    return 0;

  // Determine byte length of this character
  int len = 1;
  if (*p >= 0xF0)
    len = 4;
  else if (*p >= 0xE0)
    len = 3;
  else if (*p >= 0xC0)
    len = 2;

  char buf[5] = {0};
  for (int i = 0; i < len && p[i]; i++) {
    buf[i] = p[i];
  }

  int16_t charW = u8g2Fonts.getUTF8Width(buf);
  u8g2Fonts.setCursor(x - charW / 2, y); // Center horizontally
  u8g2Fonts.print(buf);

  return 16; // Fixed line height for vertical text
}

// Count UTF-8 characters in string
static int utf8Len(const char *text) {
  int count = 0;
  const uint8_t *p = (const uint8_t *)text;
  while (*p) {
    int len = 1;
    if (*p >= 0xF0)
      len = 4;
    else if (*p >= 0xE0)
      len = 3;
    else if (*p >= 0xC0)
      len = 2;

    for (int i = 0; i < len && *p; i++) {
      p++;
    }
    count++;
  }
  return count;
}

// === "今日无待办" weather-aware illustration ============================
// The no-task illustration picks one scene based on the current weather
// condition (QWeather icon code from weather_data_now()) and draws a small
// landscape in the right panel where the todo list would normally sit.
//
// Categories we draw distinctly:
//   kClear      100        sun + mountain         (original look)
//   kClearNight 150        crescent moon + stars + mountain
//   kCloudy     101,102,103,151,152,153
//                          partly-cloudy: sun behind a cloud + mountain
//   kOvercast   104        cloud band + mountain
//   kRain       300,301,305-318,399
//                          cloud + raindrops + mountain
//   kThunder    302,303,304 cloud + lightning bolt + raindrops
//   kSnow       350,351,400-410,456,457,499
//                          cloud + snowflakes + snowcapped peak
//                          (shower snow 350,351 grouped here too)
//   kFog        500,501,502,509,510 fog bands drift over the mountain
//   kSand       503,504,507,508,511-515 wind/sand lines blow across
//   kUnknown    anything else (incl. no weather data) -> bare mountain

enum NoTodoWeatherKind {
  kClear,
  kClearNight,
  kCloudy,
  kOvercast,
  kLightRain,
  kMediumRain,
  kHeavyRain,
  kThunder,
  kSnow,
  kFog,
  kSand,
  kUnknown,
};

static NoTodoWeatherKind noTaskWeather(uint16_t icon) {
  switch (icon) {
  case 100:
    return kClear;
  case 150:
    return kClearNight;
  case 101:
  case 102:
  case 103:
  case 151:
  case 152:
  case 153:
    return kCloudy;
  case 104:
    return kOvercast;
  case 300:
  case 305:
  case 309:
  case 314:
    return kLightRain;
  case 306:
  case 399:
    return kMediumRain;
  case 301:
  case 307:
  case 308:
  case 315:
  case 316:
  case 317:
  case 318:
    return kHeavyRain;
  case 302:
  case 303:
  case 304:
  case 310:
  case 311:
  case 312:
  case 313:
    return kThunder;
  case 350:
  case 351:
  case 400:
  case 401:
  case 402:
  case 403:
  case 404:
  case 405:
  case 406:
  case 407:
  case 408:
  case 409:
  case 410:
  case 456:
  case 457:
  case 499:
    return kSnow;
  case 500:
  case 501:
  case 502:
  case 509:
  case 510:
    return kFog;
  case 503:
  case 504:
  case 507:
  case 508:
  case 511:
  case 512:
  case 513:
  case 514:
  case 515:
    return kSand;
  default:
    return kUnknown;
  }
}

// Subtitle shown under "今日无待办".
static const char *noTaskSubtitle(uint16_t icon) {
  switch (noTaskWeather(icon)) {
  case kClear:
    return "阳光正好，轻松一下";
  case kClearNight:
    return "月色温柔，安然入梦";
  case kCloudy:
    return "云淡风轻，悠闲一天";
  case kOvercast:
    return "天色阴沉，慢享时光";
  case kLightRain:
    return "窗外细雨，适合休息";
  case kMediumRain:
    return "雨声淅沥，适合休息";
  case kHeavyRain:
    return "雨大风急，安心在家";
  case kThunder:
    return "雷雨交加，安心在家";
  case kSnow:
    return "雪花轻落，万物静谧";
  case kFog:
    return "雾气朦胧，慢度时光";
  case kSand:
    return "风沙漫天，缩进屋中";
  default:
    return "Enjoy your day";
  }
}

// Mountain ridge silhouette, drawn at the bottom of the scene.
// Same geometry as the original single scene: SVG points mapped at scale 0.45,
// with SVG x=270 aligned to cx and SVG y=450 (the base) aligned to horizonY.
static void drawMtnRidge(int16_t cx, int16_t horizonY, float G = 1.0f) {
  const float s = 0.45f * G;
  auto mx = [&](int sx) { return cx + (int16_t)((sx - 270) * s); };
  auto my = [&](int sy) { return horizonY + (int16_t)((sy - 450) * s); };

  for (int d = -1; d <= 1; d++) {
    // Main ridge: 130,450 -> 180,380 -> 210,400 -> 270,300 -> 320,370 ->
    // 350,350 -> 400,450
    display.drawLine(mx(130), my(450) + d, mx(180), my(380) + d, GxEPD_BLACK);
    display.drawLine(mx(180), my(380) + d, mx(210), my(400) + d, GxEPD_BLACK);
    display.drawLine(mx(210), my(400) + d, mx(270), my(300) + d, GxEPD_BLACK);
    display.drawLine(mx(270), my(300) + d, mx(320), my(370) + d, GxEPD_BLACK);
    display.drawLine(mx(320), my(370) + d, mx(350), my(350) + d, GxEPD_BLACK);
    display.drawLine(mx(350), my(350) + d, mx(400), my(450) + d, GxEPD_BLACK);
    // Inner ridge: 210,400 -> 250,370 -> 270,300
    display.drawLine(mx(210), my(400) + d, mx(250), my(370) + d, GxEPD_BLACK);
    display.drawLine(mx(250), my(370) + d, mx(270), my(300) + d, GxEPD_BLACK);
  }
}

static void drawNoTaskSnowflake(int16_t x, int16_t y, float s, uint16_t col) {
  display.drawLine(x - (int16_t)s, y, x + (int16_t)s, y, col);
  display.drawLine(x, y - (int16_t)s, x, y + (int16_t)s, col);
  int16_t s07 = (int16_t)(s * 0.7f + 0.5f);
  display.drawLine(x - s07, y - s07, x + s07, y + s07, col);
  display.drawLine(x - s07, y + s07, x + s07, y - s07, col);
}

// Zen outline cloud drawn with line segments, scalable to match mountain
// aesthetic
static void drawZenCloud(int16_t cx, int16_t cyb, float s = 1.0f) {
  auto dx = [&](float val) { return (int16_t)(val * s + 0.5f); };
  auto dy = [&](float val) { return cyb - (int16_t)(val * s + 0.5f); };

  // Erase interior with white shapes so mountain doesn't show through
  display.fillCircle(cx - dx(14), dy(6), dx(7), GxEPD_WHITE);
  display.fillCircle(cx, dy(10), dx(10), GxEPD_WHITE);
  display.fillCircle(cx + dx(14), dy(6), dx(7), GxEPD_WHITE);
  display.fillRect(cx - dx(15), dy(8), dx(30), dx(9), GxEPD_WHITE);

  // Draw outline
  display.drawLine(cx - dx(18), cyb, cx + dx(18), cyb, GxEPD_BLACK);
  display.drawLine(cx - dx(18), cyb, cx - dx(24), dy(4), GxEPD_BLACK);
  display.drawLine(cx - dx(24), dy(4), cx - dx(22), dy(10), GxEPD_BLACK);
  display.drawLine(cx - dx(22), dy(10), cx - dx(14), dy(12), GxEPD_BLACK);
  display.drawLine(cx - dx(14), dy(12), cx - dx(10), dy(18), GxEPD_BLACK);
  display.drawLine(cx - dx(10), dy(18), cx + dx(6), dy(19), GxEPD_BLACK);
  display.drawLine(cx + dx(6), dy(19), cx + dx(12), dy(14), GxEPD_BLACK);
  display.drawLine(cx + dx(12), dy(14), cx + dx(20), dy(11), GxEPD_BLACK);
  display.drawLine(cx + dx(20), dy(11), cx + dx(24), dy(5), GxEPD_BLACK);
  display.drawLine(cx + dx(24), dy(5), cx + dx(18), cyb, GxEPD_BLACK);
}

static void drawZenRain(int16_t cx, int16_t hor, int intensity,
                        float G = 1.0f) {
  int16_t rain[16][3] = {
      {-45, -50, 10}, {-10, -65, 12}, {10, -45, 15}, {30, -35, 12}};
  int count = 4;
  if (intensity >= 2) {
    rain[4][0] = -30;
    rain[4][1] = -75;
    rain[4][2] = 18;
    rain[5][0] = -5;
    rain[5][1] = -40;
    rain[5][2] = 15;
    rain[6][0] = 35;
    rain[6][1] = -60;
    rain[6][2] = 15;
    rain[7][0] = -50;
    rain[7][1] = -30;
    rain[7][2] = 15;
    count = 8;
  }
  if (intensity >= 3) {
    rain[8][0] = -55;
    rain[8][1] = -70;
    rain[8][2] = 15;
    rain[9][0] = -25;
    rain[9][1] = -45;
    rain[9][2] = 12;
    rain[10][0] = -35;
    rain[10][1] = -20;
    rain[10][2] = 10;
    rain[11][0] = -15;
    rain[11][1] = -15;
    rain[11][2] = 8;
    rain[12][0] = 15;
    rain[12][1] = -70;
    rain[12][2] = 20;
    rain[13][0] = 5;
    rain[13][1] = -25;
    rain[13][2] = 10;
    rain[14][0] = 55;
    rain[14][1] = -65;
    rain[14][2] = 18;
    rain[15][0] = 50;
    rain[15][1] = -40;
    rain[15][2] = 12;
    count = 16;
  }
  for (int i = 0; i < count; i++) {
    int16_t rx = rain[i][0];
    int16_t ry = rain[i][1];
    int16_t rl = rain[i][2];
    int16_t dx = (int16_t)(rl * G * 0.6f + 0.5f);
    display.drawLine(cx + rx * G, hor + ry * G, cx + rx * G - dx,
                     hor + ry * G + rl * G, GxEPD_BLACK);
  }
}

// Draw one of several "no task" landscapes depending on the live weather.
// cx,cy is the scene center (matches the original centerX,centerY).
static void drawNoTaskIllustration(int16_t cx, int16_t cy, int16_t horizonY,
                                   uint16_t icon) {
  float G = 1.33f;

  switch (noTaskWeather(icon)) {
  case kClear: {
    drawMtnRidge(cx, horizonY, G);
    // Red Sun
    display.fillCircle(cx - 35 * G, horizonY - 50 * G, 10 * G, GxEPD_RED);
    // Birds flying
    display.drawLine(cx + 30 * G, horizonY - 70 * G, cx + 34 * G,
                     horizonY - 66 * G, GxEPD_BLACK);
    display.drawLine(cx + 34 * G, horizonY - 66 * G, cx + 38 * G,
                     horizonY - 70 * G, GxEPD_BLACK);
    display.drawLine(cx + 42 * G, horizonY - 65 * G, cx + 45 * G,
                     horizonY - 62 * G, GxEPD_BLACK);
    display.drawLine(cx + 45 * G, horizonY - 62 * G, cx + 48 * G,
                     horizonY - 65 * G, GxEPD_BLACK);
    break;
  }
  case kClearNight: {
    drawMtnRidge(cx, horizonY, G);
    // Crescent Moon
    display.fillCircle(cx - 35 * G, horizonY - 50 * G, 9 * G, GxEPD_BLACK);
    display.fillCircle(cx - 32 * G, horizonY - 52 * G, 8 * G, GxEPD_WHITE);
    // Stars
    display.fillRect(cx - 10 * G, horizonY - 75 * G, 2 * G, 2 * G, GxEPD_BLACK);
    display.fillRect(cx + 25 * G, horizonY - 60 * G, 2 * G, 2 * G, GxEPD_BLACK);
    display.fillRect(cx + 45 * G, horizonY - 70 * G, 2 * G, 2 * G, GxEPD_BLACK);
    break;
  }
  case kCloudy: {
    drawMtnRidge(cx, horizonY, G);
    display.fillCircle(cx - 42 * G, horizonY - 53 * G, 9 * G, GxEPD_RED);
    display.fillRect(cx - 57 * G, horizonY - 53 * G, 35 * G, 15 * G,
                     GxEPD_WHITE);
    for (int i = 0; i < 3; i++) {
      display.drawFastHLine(cx - 62 * G + i * 5 * G,
                            horizonY - 53 * G + i * 4 * G, 30 * G, GxEPD_BLACK);
    }
    break;
  }
  case kOvercast: {
    drawMtnRidge(cx, horizonY, G);
    drawZenCloud(cx - 25 * G, horizonY - 35 * G, 1.2f * G);
    drawZenCloud(cx + 25 * G, horizonY - 50 * G, 0.9f * G);
    break;
  }
  case kLightRain: {
    drawMtnRidge(cx, horizonY, G);
    drawZenRain(cx, horizonY, 1, G);
    break;
  }
  case kMediumRain: {
    drawMtnRidge(cx, horizonY, G);
    drawZenRain(cx, horizonY, 2, G);
    break;
  }
  case kHeavyRain: {
    drawMtnRidge(cx, horizonY, G);
    drawZenRain(cx, horizonY, 3, G);
    break;
  }
  case kThunder: {
    drawMtnRidge(cx, horizonY, G);
    // Striking bold lightning bolt
    display.drawLine(cx - 30 * G, horizonY - 80 * G, cx - 10 * G,
                     horizonY - 65 * G, GxEPD_RED);
    display.drawLine(cx - 10 * G, horizonY - 65 * G, cx - 20 * G,
                     horizonY - 65 * G, GxEPD_RED);
    display.drawLine(cx - 20 * G, horizonY - 65 * G, cx + 5 * G,
                     horizonY - 35 * G, GxEPD_RED);
    // Thicken the top parts of the bolt for a tapering effect
    display.drawLine(cx - 29 * G, horizonY - 80 * G, cx - 9 * G,
                     horizonY - 65 * G, GxEPD_RED);
    display.drawLine(cx - 9 * G, horizonY - 65 * G, cx - 19 * G,
                     horizonY - 65 * G, GxEPD_RED);
    display.drawLine(cx - 31 * G, horizonY - 80 * G, cx - 11 * G,
                     horizonY - 65 * G, GxEPD_RED);
    // Rain lines
    int16_t th_rain[10][3] = {{-50, -70, 15}, {-40, -50, 10}, {-55, -30, 15},
                              {-20, -75, 18}, {-10, -45, 12}, {15, -70, 20},
                              {20, -45, 15},  {40, -60, 15},  {30, -35, 12},
                              {50, -40, 12}};
    for (int i = 0; i < 10; i++) {
      int16_t rx = th_rain[i][0];
      int16_t ry = th_rain[i][1];
      int16_t rl = th_rain[i][2];
      int16_t dx = (int16_t)(rl * G * 0.6f + 0.5f);
      display.drawLine(cx + rx * G, horizonY + ry * G, cx + rx * G - dx,
                       horizonY + ry * G + rl * G, GxEPD_BLACK);
    }
    break;
  }
  case kSnow: {
    drawMtnRidge(cx, horizonY, G);
    {
      const float s = 0.45f * G;
      int16_t peakX = cx;
      int16_t peakY = horizonY + (int16_t)((300 - 450) * s);
      // Snow cap on the mountain peak (white overlay above the ridge).
      display.drawLine(peakX - 14 * G, peakY + 9 * G, peakX - 4 * G,
                       peakY + 1 * G, GxEPD_WHITE);
      display.drawLine(peakX - 4 * G, peakY + 1 * G, peakX + 6 * G,
                       peakY + 10 * G, GxEPD_WHITE);
      display.drawFastHLine(peakX - 14 * G, peakY + 9 * G, 20 * G, GxEPD_WHITE);
    }
    drawNoTaskSnowflake(cx - 40 * G, horizonY - 60 * G, 4 * G, GxEPD_BLACK);
    drawNoTaskSnowflake(cx - 20 * G, horizonY - 45 * G, 3 * G, GxEPD_BLACK);
    drawNoTaskSnowflake(cx + 10 * G, horizonY - 50 * G, 4 * G, GxEPD_BLACK);
    drawNoTaskSnowflake(cx + 40 * G, horizonY - 65 * G, 3 * G, GxEPD_BLACK);
    drawNoTaskSnowflake(cx - 10 * G, horizonY - 70 * G, 3 * G, GxEPD_BLACK);
    break;
  }
  case kFog: {
    drawMtnRidge(cx, horizonY, G);
    // Obscure the middle of the mountain to let the peak peek out, exposing the
    // left peak below it
    display.fillRect(cx - 55 * G, horizonY - 58 * G, 110 * G, 23 * G,
                     GxEPD_WHITE); // Erases hor-58 to hor-35
    // Fog/haze layers
    display.drawFastHLine(cx - 45 * G, horizonY - 34 * G, 40 * G, GxEPD_BLACK);
    display.drawFastHLine(cx + 10 * G, horizonY - 37 * G, 35 * G, GxEPD_BLACK);
    display.drawFastHLine(cx - 20 * G, horizonY - 45 * G, 50 * G, GxEPD_BLACK);
    display.drawFastHLine(cx - 10 * G, horizonY - 51 * G, 40 * G, GxEPD_BLACK);
    display.drawFastHLine(cx - 30 * G, horizonY - 57 * G, 30 * G, GxEPD_BLACK);
    display.drawFastHLine(cx + 5 * G, horizonY - 55 * G, 25 * G, GxEPD_BLACK);
    break;
  }
  case kSand: {
    // Smooth Sand Dunes
    int16_t hor_old = cy + 34;
    display.drawLine(cx - 60 * G, hor_old - 25 * G, cx - 20 * G,
                     hor_old - 35 * G, GxEPD_BLACK);
    display.drawLine(cx - 20 * G, hor_old - 35 * G, cx + 60 * G,
                     hor_old - 10 * G, GxEPD_BLACK);
    display.drawLine(cx - 60 * G, hor_old - 10 * G, cx + 10 * G,
                     hor_old - 25 * G, GxEPD_BLACK);
    display.drawLine(cx + 10 * G, hor_old - 25 * G, cx + 60 * G,
                     hor_old - 5 * G, GxEPD_BLACK);
    // Wind lines
    for (int i = 0; i < 5; i++)
      display.drawFastHLine(cx - 50 * G + i * 15 * G,
                            hor_old - 60 * G + i * 8 * G, 20 * G, GxEPD_RED);
    break;
  }
  default: {
    drawMtnRidge(cx, horizonY, G);
    break;
  }
  }
}

// Draw todo list below clock as vertical text timeline
// Layout: Grouped by date, items arranged right-to-left
// Separator logic:
// - Date column (Red) for each group
// - Items (Black)
// - Items within a group separated by Black Dashed Line
// - Groups separated by Red Solid Line
void drawTodoList() {
  TodoData *todos = todo_data();

  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

  // Determine layout area
  // Clock ends at 140. Title "今日待办" at Y=110-130?
  // Let's place Title at Y=120, line at 125.
  // Todo items start at 135.
  drawTodoTitle(115);

  int16_t areaTop = 135;
  int16_t areaBot = SCREEN_HEIGHT - 5;
  int16_t areaLeft = CLOCK_X;
  int16_t areaRight = CLOCK_X + CLOCK_W - 5;
  int16_t availH = areaBot - areaTop;

  // === No tasks: Upper half has illustration + text, lower half has weather
  // temperature trend chart ===
  if (!todos || todos->count == 0) {
    int16_t centerX = CLOCK_X + CLOCK_W / 2;
    int16_t centerY = 205; // Shifted and scaled to fit the smaller top region

    // Pick the live weather condition (use 999 if there is no valid weather).
    Weather *wNow = weather_data_now();
    uint16_t nowIcon = (weather_status() == 1 && wNow) ? wNow->icon : 999;

    int16_t horizonY = centerY + 42; // moved down a bit to use more space

    // Upper-half no-task scene varies by current weather.
    drawNoTaskIllustration(centerX, centerY, horizonY, nowIcon);

    if (noTaskWeather(nowIcon) == kSand) {
      int16_t hor0 = centerY + 34;

      // Text line 1: "今日无待办"
      u8g2Fonts.setFont(FONT_TODO);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      const char *line1 = "今日无待办";
      int16_t tw1 = u8g2Fonts.getUTF8Width(line1);
      u8g2Fonts.setCursor(centerX - tw1 / 2, hor0 + 3);
      u8g2Fonts.print(line1);

      // Text line 2: weather-aware subtitle.
      int16_t y2 = hor0 + 19;
      u8g2Fonts.setFont(FONT_SUB);
      const char *line2 = noTaskSubtitle(nowIcon);
      int16_t tw2 = u8g2Fonts.getUTF8Width(line2);
      u8g2Fonts.setCursor(centerX - tw2 / 2, y2);
      u8g2Fonts.print(line2);
    } else {
      // Text line 1: "今日无待办" (Larger font, drawn above the subtitle)
      u8g2Fonts.setFont(FONT_TODO); // Increase font size to 16px
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      const char *line1 = "今日无待办";
      int16_t tw1 = u8g2Fonts.getUTF8Width(line1);
      u8g2Fonts.setCursor(centerX - tw1 / 2, horizonY - 13);
      u8g2Fonts.print(line1);

      // Text line 2: weather-aware subtitle. (Baseline at the bottom of the
      // mountain)
      u8g2Fonts.setFont(FONT_SUB);
      const char *line2 = noTaskSubtitle(nowIcon);
      int16_t tw2 = u8g2Fonts.getUTF8Width(line2);
      u8g2Fonts.setCursor(centerX - tw2 / 2, horizonY + 3);
      u8g2Fonts.print(line2);
    }

    // Horizontal Split Line (moved up to 262 to shrink the no-tasks area and
    // maximize space)
    display.drawFastHLine(CLOCK_X + 20, 262, CLOCK_W - 40, GxEPD_BLACK);

    // --- Lower Half: Weather and Temperature trend ---
    DailyForecast *wFc = weather_data_daily();
    if (weather_status() == 1 && wFc && wFc->length >= 3) {
      // 3 columns: today, tomorrow, day after
      int16_t colX[3] = {(int16_t)(CLOCK_X + 52), (int16_t)(CLOCK_X + 156),
                         (int16_t)(CLOCK_X + 260)};
      const char *labels[3] = {"今天", "明天", "后天"};

      // 1. Draw labels, icons, weather text
      LittleFS.begin(true);
      u8g2Fonts.setFont(
          FONT_TEXT); // Enlarged from FONT_SUB to FONT_TEXT (16px)
      for (int i = 0; i < 3; i++) {
        // Label (今天/明天/后天)
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        int16_t lw = u8g2Fonts.getUTF8Width(labels[i]);
        u8g2Fonts.setCursor(colX[i] - lw / 2, 278);
        u8g2Fonts.print(labels[i]);

        // Icon
        drawWeatherBitmap(colX[i] - 12, 288, wFc->weather[i].iconDay, 24,
                          GxEPD_RED);

        // Weather text
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        int16_t tw = u8g2Fonts.getUTF8Width(wFc->weather[i].textDay.c_str());
        u8g2Fonts.setCursor(colX[i] - tw / 2,
                            328); // Adjusted baseline to Y=328 for larger font
        u8g2Fonts.print(wFc->weather[i].textDay.c_str());
      }
      LittleFS.end();

      // 2. Draw temperature line chart
      // Find min and max temp over 3 days
      int8_t maxT = -127;
      int8_t minT = 127;
      for (int i = 0; i < 3; i++) {
        if (wFc->weather[i].tempMax > maxT)
          maxT = wFc->weather[i].tempMax;
        if (wFc->weather[i].tempMin < minT)
          minT = wFc->weather[i].tempMin;
      }

      int16_t chartTopY = 365;
      int16_t chartBotY = 425;
      int16_t chartH = chartBotY - chartTopY;

      auto getY = [&](int8_t t) -> int16_t {
        if (maxT == minT)
          return chartTopY + chartH / 2;
        return chartBotY - (t - minT) * chartH / (maxT - minT);
      };

      int16_t yMax[3], yMin[3];
      for (int i = 0; i < 3; i++) {
        yMax[i] = getY(wFc->weather[i].tempMax);
        yMin[i] = getY(wFc->weather[i].tempMin);
      }

      // Draw high temp lines (Red)
      display.drawLine(colX[0], yMax[0], colX[1], yMax[1], GxEPD_RED);
      display.drawLine(colX[0], yMax[0] + 1, colX[1], yMax[1] + 1, GxEPD_RED);
      display.drawLine(colX[1], yMax[1], colX[2], yMax[2], GxEPD_RED);
      display.drawLine(colX[1], yMax[1] + 1, colX[2], yMax[2] + 1, GxEPD_RED);

      // Draw low temp lines (Black)
      display.drawLine(colX[0], yMin[0], colX[1], yMin[1], GxEPD_BLACK);
      display.drawLine(colX[0], yMin[0] + 1, colX[1], yMin[1] + 1, GxEPD_BLACK);
      display.drawLine(colX[1], yMin[1], colX[2], yMin[2], GxEPD_BLACK);
      display.drawLine(colX[1], yMin[1] + 1, colX[2], yMin[2] + 1, GxEPD_BLACK);

      // Draw points and values
      u8g2Fonts.setFont(u8g2_font_fub11_tr); // Enlarged to 13px bold font
      for (int i = 0; i < 3; i++) {
        // High temp points
        display.fillCircle(colX[i], yMax[i], 3, GxEPD_RED);
        u8g2Fonts.setForegroundColor(GxEPD_RED);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d°", wFc->weather[i].tempMax);
        int16_t tw = u8g2Fonts.getUTF8Width(buf);
        u8g2Fonts.setCursor(
            colX[i] - tw / 2,
            yMax[i] - 10); // Offset adjusted to -10 to avoid line overlap
        u8g2Fonts.print(buf);

        // Low temp points
        display.fillCircle(colX[i], yMin[i], 3, GxEPD_BLACK);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        snprintf(buf, sizeof(buf), "%d°", wFc->weather[i].tempMin);
        tw = u8g2Fonts.getUTF8Width(buf);
        u8g2Fonts.setCursor(
            colX[i] - tw / 2,
            yMin[i] + 22); // Offset adjusted to +22 to avoid line overlap
        u8g2Fonts.print(buf);
      }
    } else {
      // Weather data unavailable fallback
      u8g2Fonts.setFont(FONT_TEXT);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      const char *noWeatherStr = "天气数据不可用";
      int16_t nw = u8g2Fonts.getUTF8Width(noWeatherStr);
      u8g2Fonts.setCursor(centerX - nw / 2, 350);
      u8g2Fonts.print(noWeatherStr);
    }

    return;
  }

  // === Draw vertical grouped timeline ===
  int16_t colW = 22;     // Width per column
  int16_t dateColW = 20; // Width for date column
  int16_t charH = 16;    // Height per character
  int16_t sepW = 10;     // Separator width

  // Start from right side
  int16_t curX = areaRight;
  uint64_t currentGroupKey = 0; // 0 is invalid key

  // Iterate through sorted items
  for (uint8_t i = 0; i < todos->count; i++) {
    TodoItem &item = todos->items[i];

    // Determine group key (YYYYMMDD part of dueSortKey)
    uint64_t itemGroupKey = (item.dueSortKey == UINT64_MAX)
                                ? UINT64_MAX
                                : (item.dueSortKey / 10000);

    bool isNewGroup = (i == 0) || (itemGroupKey != currentGroupKey);
    bool isFirstItem = (i == 0);

    // If new group, we need a separator (if not first) and a date column
    if (isNewGroup) {
      if (!isFirstItem) {
        // Red solid line separator between groups
        // Check space
        if (curX - sepW < CLOCK_X)
          break;
        int16_t lineX = curX - sepW / 2;
        display.drawFastVLine(lineX, areaTop, availH, GxEPD_RED);
        display.drawFastVLine(lineX + 1, areaTop, availH, GxEPD_RED);
        curX -= sepW;
      }

      // Draw Date Column
      currentGroupKey = itemGroupKey;

      // Prepare date string
      char dateStr[32] = "无\n截\n止\n日\n期"; // Default for no date
      if (itemGroupKey != UINT64_MAX) {
        // Extract Month/Day from item.dueInfo or recalculate?
        // item.dueSortKey format: YYYYMMDDHHmm
        // GroupKey: YYYYMMDD
        int m = (itemGroupKey % 10000) / 100;
        int d = itemGroupKey % 100;
        snprintf(dateStr, sizeof(dateStr), "%d\n月\n%d\n日", m, d);
      }

      // Check space for date col
      if (curX - dateColW < CLOCK_X)
        break;

      // Draw Date Column (Red)
      int16_t dateCX = curX - dateColW / 2;
      u8g2Fonts.setFont(FONT_TEXT);
      u8g2Fonts.setForegroundColor(GxEPD_RED);

      // Parse newline manually for vertical stack
      int16_t dateY = areaTop + charH;
      char *p = dateStr;
      while (*p) {
        if (*p == '\n') {
          p++;
          continue;
        }
        // Find next newline or end
        char chunk[8] = {0};
        int cLen = 0;
        while (*p && *p != '\n' && cLen < 7) {
          chunk[cLen++] = *p++;
        }
        // Draw chunk centered
        int16_t cw = u8g2Fonts.getUTF8Width(chunk);
        u8g2Fonts.setCursor(dateCX - cw / 2, dateY);
        u8g2Fonts.print(chunk);
        dateY += charH;
      }

      curX -= dateColW;
    }

    // Separator before item (Black dashed)
    // Only if NOT new group (user requested no separator after date/before last
    // item)
    // Separator before item (Black dashed)
    // Always draw separator between Date and Item, and between Items
    if (curX - sepW < areaLeft)
      break;

    // Black dashed line separator
    int16_t lineX = curX - sepW / 2;
    for (int16_t y = areaTop; y < areaBot; y += 4) {
      display.drawPixel(lineX, y, GxEPD_BLACK);
      display.drawPixel(lineX, y + 1, GxEPD_BLACK);
    }
    curX -= sepW;

    // Draw Item
    if (curX - colW < areaLeft)
      break;

    bool hasTime =
        (itemGroupKey != UINT64_MAX && (item.dueSortKey % 10000) != 2400);
    int16_t timeReserveH = hasTime ? 16 : 0;

    int16_t charsPerCol = (availH - timeReserveH) / charH;
    int titleLen = utf8Len(item.title);
    int numCols = (titleLen + charsPerCol - 1) / charsPerCol;
    if (numCols <= 0)
      numCols = 1;
    if (numCols > 3)
      numCols = 3;

    // Check if we hit the left edge
    int fitCols = (curX - areaLeft) / colW;
    if (fitCols <= 0)
      break;
    if (numCols > fitCols)
      numCols = fitCols;

    int16_t itemStartX = curX;
    int charIdx = 0;

    u8g2Fonts.setForegroundColor(item.important ? GxEPD_RED : GxEPD_BLACK);

    for (int col = 0; col < numCols; col++) {
      int16_t itemCX = curX - colW / 2;
      int16_t titleY = areaTop + charH;

      int charsInThisCol = charsPerCol;
      bool isLastCol = (col == numCols - 1);
      if (isLastCol && titleLen > numCols * charsPerCol) {
        charsInThisCol = charsPerCol - 1; // reserve 1 for ellipsis
      }

      u8g2Fonts.setFont(FONT_TODO);
      for (int c = 0; c < charsInThisCol && charIdx < titleLen; c++) {
        drawVerticalChar(itemCX, titleY + c * charH, item.title, charIdx);
        charIdx++;
      }

      if (isLastCol && charIdx < titleLen) {
        u8g2Fonts.setCursor(itemCX - 3, titleY + charsInThisCol * charH);
        u8g2Fonts.print(":"); // vertical ellipsis equivalent
      }

      curX -= colW;
    }

    if (hasTime) {
      int hh = (item.dueSortKey % 10000) / 100;
      int mm = (item.dueSortKey % 100);
      char timeStr[8];
      snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hh, mm);

      u8g2Fonts.setFont(FONT_SUB);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);

      int16_t tw = u8g2Fonts.getUTF8Width(timeStr);
      int16_t center_x = itemStartX - (numCols * colW) / 2;

      u8g2Fonts.setCursor(center_x - tw / 2, areaBot - 2);
      u8g2Fonts.print(timeStr);
    }
  }
}

// Main screen task
void task_screen(void *param) {
  Serial.println(F("[Task] screen update begin..."));

  display_init();
  display.init(115200);
  display.setRotation(0);
  u8g2Fonts.begin(display);

  // Use pre-cached sensor data (read before WiFi to avoid heat)
  extern SensorData cachedSensorData;
  SensorData indoor = cachedSensorData;
  Weather *weather = weather_data_now();
  DailyForecast *forecast = weather_data_daily();

  // Calculate dynamic calendar day height based on actual month row count
  // Calendar rows expand to fill space between header and fixed weather panel
  int monthNum = tmInfo.tm_mon + 1;
  int year = tmInfo.tm_year + 1900;
  int totalDays = 31;
  if (monthNum == 4 || monthNum == 6 || monthNum == 9 || monthNum == 11) {
    totalDays = 30;
  } else if (monthNum == 2) {
    bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    totalDays = isLeap ? 29 : 28;
  }
  int wday1 = (tmInfo.tm_wday - (tmInfo.tm_mday - 1) % 7 + 7) % 7;
  int totalRows = (wday1 + totalDays - 1) / 7 + 1;
  int16_t availH = WEATHER_PANEL_Y - CAL_TOP_H - CAL_HEADER_H;
  g_calDayH = availH / totalRows; // 5 rows → 58px, 6 rows → 48px

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Left side: Calendar (480px)
    draw_cal_year_info();
    draw_cal_header();
    draw_cal_days();
    draw_weather_panel();

    // Vertical Divider between Calendar/Weather and Clock/Todo
    display.drawFastVLine(SPLIT_X, 0, SCREEN_HEIGHT, GxEPD_BLACK);

    // Right side: Clock (320px)
    drawClock(tmInfo.tm_hour, tmInfo.tm_min);

    // Todo list below clock
    drawTodoList();
  } while (display.nextPage());

  display.powerOff();
  // display.hibernate(); // Don't hibernate, keep controller RAM for partial
  // update

  Serial.println(F("[Task] screen update end..."));
  _screen_status = 1;
  SCREEN_HANDLER = NULL;
  vTaskDelete(NULL);
}

void si_calendar() {
  _calendar_status = 0;
  time_t now = 0;
  time(&now);
  localtime_r(&now, &tmInfo);

  if (tmInfo.tm_year + 1900 < 2025) {
    Serial.println(F("ERR: Invalid system time"));
    si_warning("系统时间无效\nNTP同步失败\n请检查网络连接");
    _calendar_status = 2;
    return;
  }

  // Calculate lunar dates for this month
  nl_month_days(tmInfo.tm_year + 1900, tmInfo.tm_mon + 1, lunarDates);
  nl_year_jq(tmInfo.tm_year + 1900, jqAccDate);

  // Fetch holiday data from API
  if (!getHolidays(currentHoliday, tmInfo.tm_year + 1900, tmInfo.tm_mon + 1)) {
    Serial.println(F("Warning: Failed to fetch holiday data"));
    currentHoliday.length = 0; // Reset to empty if failed
  }

  _calendar_status = 1;
}

int si_calendar_status() { return _calendar_status; }

void si_screen() {
  _screen_status = 0;
  si_calendar();

  if (si_calendar_status() != 1) {
    _screen_status = 2;
    return;
  }

  if (SCREEN_HANDLER != NULL)
    vTaskDelete(SCREEN_HANDLER);
  xTaskCreate(task_screen, "Screen", 10240, NULL, 2, &SCREEN_HANDLER);
}

void si_screen_display_only() {
  _screen_status = 0;

  // si_calendar() must have been called already (with WiFi active)
  if (si_calendar_status() != 1) {
    _screen_status = 2;
    return;
  }

  if (SCREEN_HANDLER != NULL)
    vTaskDelete(SCREEN_HANDLER);
  xTaskCreate(task_screen, "Screen", 10240, NULL, 2, &SCREEN_HANDLER);
}

int si_screen_status() { return _screen_status; }

// Partial refresh for clock only
void updateClockOnly() {
  time_t now;
  time(&now);
  localtime_r(&now, &tmInfo);

  Serial.println("[Clock] Starting partial refresh...");

  // Use init(0, false) to preserve partial mode
  display_init();
  display.init(0, false);
  display.setRotation(0);
  u8g2Fonts.begin(display);

  // Calculate strict bounding box for the clock text to minimize update
  // time/area Font: u8g2_font_7Segments_26x42_mn Y baseline is CLOCK_Y + 130
  // Approx height ~50px. Let's refresh a stripe that safely covers the clock
  // digits. Move Y from 130 to 80. Top of digits roughly 80-60=20.
  int16_t partial_y = CLOCK_Y + 20;
  int16_t partial_h = 70; // 20 to 90 covers the 80 baseline text

  Serial.printf("[Clock] setPartialWindow(%d, %d, %d, %d)\n", CLOCK_X,
                partial_y, CLOCK_W, partial_h);
  display.setPartialWindow(CLOCK_X, partial_y, CLOCK_W, partial_h);
  display.firstPage();
  do {
    // Fill the background white to erase old digits
    display.fillRect(CLOCK_X, partial_y, CLOCK_W, partial_h, GxEPD_WHITE);

    u8g2Fonts.setFont(u8g2_font_7Segments_26x42_mn);
    u8g2Fonts.setFontMode(1); // Transparent mode
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min);
    int16_t w = u8g2Fonts.getUTF8Width(buf);
    u8g2Fonts.setCursor(CLOCK_X + (CLOCK_W - w) / 2, CLOCK_Y + 80);
    u8g2Fonts.print(buf);
    Serial.printf("[Clock] Drawing time: %s\n", buf);
  } while (display.nextPageBW()); // Fast B/W partial refresh for clock

  Serial.println("[Clock] Partial refresh complete, powering off");
  display.powerOff();
}

void si_warning(const char *str) {
  display_init();
  display.init(115200);
  display.setRotation(0);
  u8g2Fonts.begin(display);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(FONT_TEXT);
    u8g2Fonts.setForegroundColor(GxEPD_RED);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

    // Center the text
    int16_t w = u8g2Fonts.getUTF8Width(str);
    u8g2Fonts.setCursor((SCREEN_WIDTH - w) / 2, SCREEN_HEIGHT / 2);
    u8g2Fonts.print(str);
  } while (display.nextPage());

  display.powerOff();
  // display.hibernate(); // Don't hibernate
}
