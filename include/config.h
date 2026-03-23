#ifndef CONFIG_H
#define CONFIG_H

// Display Pins (from schematic and GxEPD2_WS_ESP32_Driver.cpp)
#define EPD_CS 5
#define EPD_DC 19 // Corrected from 19 based on schematic
#define EPD_RST 16
#define EPD_BUSY 17 // Corrected from 17 based on schematic
#define EPD_SCK 18
#define EPD_MISO 19 // Not connected to E-Paper, but required for SPI
#define EPD_MOSI 23

// Sensor Pins (SHT30)
#define SHT_SDA 21
#define SHT_SCL 22

// Screen Dimensions
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

// Split Layout
#define SPLIT_X 480 // Left Width
#define CLOCK_X 488
#define CLOCK_Y 0
#define CLOCK_W 312
#define CLOCK_H 140

#define SENSOR_X 482
#define SENSOR_Y 240
#define SENSOR_W 318
#define SENSOR_H 240

// Weather panel (below calendar, left side)
#define WEATHER_PANEL_X 0
#define WEATHER_PANEL_Y 375
#define WEATHER_PANEL_W 480
#define WEATHER_PANEL_H 105

// Calendar layout adjustments
// Layout: Header(60) + WeekRow(22) + Grid(6×48=288) + Weather(105) = 475px
#define CAL_TOP_HEIGHT 60   // Header with date/time info
#define CAL_GRID_HEIGHT 288 // 6 rows of calendar days (6×48)
#define CAL_WIDTH 480
#define CAL_TOP_H 60
#define CAL_HEADER_H 22
#define CAL_DAY_W 68
#define CAL_DAY_H 48 // Reduced from 68 to fit with weather panel

#endif
