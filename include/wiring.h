#ifndef __WIRING_H__
#define __WIRING_H__

#include "config.h"
#include <Arduino.h>

// Remap legacy pins to config (corrected based on schematic)
#define SPI_MOSI EPD_MOSI
#define SPI_MISO EPD_MISO
#define SPI_SCK EPD_SCK
#define SPI_CS EPD_CS
#define SPI_DC EPD_DC     // GPIO 19
#define SPI_RST EPD_RST   // GPIO 16
#define SPI_BUSY EPD_BUSY // GPIO 17

#define I2C_SDA SHT_SDA // GPIO 21
#define I2C_SCL SHT_SCL // GPIO 22

/*
在编写程序时，有几个非常重要的硬件特性需要注意：
输入专用引脚 (Input Only):
GPIO 34 (KEY3) 和 GPIO 35 (KEY1) 是 ESP32 的仅输入 (Input Only) 引脚。
触发逻辑: 按键是 低电平触发 (Active Low)：按下时读到

*/
// Buttons based on schematic
#define KEY1 34 // SW4 - KEY1
#define KEY2 35 // SW4 - KEY2
#define KEY3 39 // SW3 - KEY3 （SENSOR_VN）
// EN 是硬件复位，不需要定义 GPIO
// #define KEY_EN  RST

#define BAT_ADC_PIN 36 // GPIO36 = SENSOR_VP, 连接 ADC_BAT
#define WAKE_IO_PIN 25 // GPIO25, 连接 WAKE_IO 控制测量开关/SHT30电源

#endif
