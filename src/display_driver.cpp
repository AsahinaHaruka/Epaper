// E-Paper Display Driver for GDE075Z08
#include <Arduino.h>
#include <GxEPD2_3C.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

// Display driver type
#define GxEPD2_DRIVER_CLASS GxEPD2_750c_Z08
#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#define MAX_HEIGHT(EPD)                                                        \
  (EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8)             \
       ? EPD::HEIGHT                                                           \
       : (MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8))

// Global display and font instances
GxEPD2_3C<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)>
    display(GxEPD2_DRIVER_CLASS(/*CS=*/5, /*DC=*/19, /*RST=*/16, /*BUSY=*/17));

U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// HSPI bus for faster SPI transfer (10MHz vs default 4MHz)
SPIClass hspi(HSPI);

void display_init() {
  hspi.begin(18, 19, 23, 5); // SCK, MISO, MOSI, CS
  display.epd2.selectSPI(hspi, SPISettings(10000000, MSBFIRST, SPI_MODE0));
}
