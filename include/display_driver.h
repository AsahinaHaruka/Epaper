#ifndef __DISPLAY_DRIVER_H__
#define __DISPLAY_DRIVER_H__

#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>

// Display driver type
#define GxEPD2_DRIVER_CLASS GxEPD2_750c_Z08
#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#define MAX_HEIGHT(EPD)                                                        \
  (EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8)             \
       ? EPD::HEIGHT                                                           \
       : (MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8))

// Global display and font instances (extern declarations)
extern GxEPD2_3C<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display;
extern U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// Initialize HSPI bus at 10MHz for faster SPI transfer
// Must be called before display.init()
void display_init();

#endif
