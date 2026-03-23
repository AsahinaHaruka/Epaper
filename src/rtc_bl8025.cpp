#include "rtc_bl8025.h"
#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>

// BL8025T 寄存器地址 (兼容 RX8025)
// RX8025/BL8025T 使用 byte[7:4] 作为寄存器地址
#define REG_SECONDS 0x00
#define REG_MINUTES 0x01
#define REG_HOURS 0x02
#define REG_WEEKDAY 0x03
#define REG_DAY 0x04
#define REG_MONTH 0x05
#define REG_YEAR 0x06
#define REG_CONTROL 0x0E
#define REG_FLAG 0x0F

// 实际发送的寄存器地址需要左移4位
#define REG_ADDR(r) ((r) << 4)

static uint8_t rtc_addr = BL8025_I2C_ADDR;

// BCD 编解码
static uint8_t bcd2dec(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
static uint8_t dec2bcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

bool rtc_init() {
  // I2C 扫描, 寻找 RTC 设备
  Serial.println("[RTC] Scanning I2C bus...");
  bool found = false;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[RTC] I2C device found at 0x%02X\n", addr);
      // BL8025T 常见地址: 0x32
      if (addr == 0x32 || addr == 0x51 || addr == 0x68) {
        rtc_addr = addr;
        found = true;
      }
    }
  }

  if (!found) {
    // 尝试默认地址
    Wire.beginTransmission(BL8025_I2C_ADDR);
    if (Wire.endTransmission() == 0) {
      rtc_addr = BL8025_I2C_ADDR;
      found = true;
    }
  }

  if (!found) {
    Serial.println("[RTC] No RTC device found on I2C bus");
    return false;
  }

  Serial.printf("[RTC] Using RTC at address 0x%02X\n", rtc_addr);
  return true;
}

bool rtc_read(struct tm *timeinfo) {
  // 设置寄存器指针到秒寄存器
  Wire.beginTransmission(rtc_addr);
  Wire.write(REG_ADDR(REG_SECONDS));
  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    Serial.printf("[RTC] Read: set register failed (err=%d)\n", err);
    return false;
  }

  // 读取 7 个寄存器 (秒/分/时/星期/日/月/年)
  uint8_t count = Wire.requestFrom(rtc_addr, (uint8_t)7);
  if (count != 7) {
    Serial.printf("[RTC] Read: expected 7 bytes, got %d\n", count);
    return false;
  }

  uint8_t sec = Wire.read();
  uint8_t min = Wire.read();
  uint8_t hour = Wire.read();
  uint8_t wday = Wire.read();
  uint8_t day = Wire.read();
  uint8_t month = Wire.read();
  uint8_t year = Wire.read();

  Serial.printf("[RTC] Raw: sec=0x%02X min=0x%02X hour=0x%02X wday=0x%02X "
                "day=0x%02X month=0x%02X year=0x%02X\n",
                sec, min, hour, wday, day, month, year);

  timeinfo->tm_sec = bcd2dec(sec & 0x7F);
  timeinfo->tm_min = bcd2dec(min & 0x7F);
  timeinfo->tm_hour = bcd2dec(hour & 0x3F); // 24小时制
  timeinfo->tm_wday = bcd2dec(wday & 0x07);
  timeinfo->tm_mday = bcd2dec(day & 0x3F);
  timeinfo->tm_mon = bcd2dec(month & 0x1F) - 1; // tm_mon: 0-11
  timeinfo->tm_year = bcd2dec(year) + 100;      // tm_year: years since 1900

  Serial.printf("[RTC] Read: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
                timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min,
                timeinfo->tm_sec);
  return true;
}

bool rtc_write(const struct tm *timeinfo) {
  Wire.beginTransmission(rtc_addr);
  Wire.write(REG_ADDR(REG_SECONDS));
  Wire.write(dec2bcd(timeinfo->tm_sec));
  Wire.write(dec2bcd(timeinfo->tm_min));
  Wire.write(dec2bcd(timeinfo->tm_hour));
  Wire.write(dec2bcd(timeinfo->tm_wday));
  Wire.write(dec2bcd(timeinfo->tm_mday));
  Wire.write(dec2bcd(timeinfo->tm_mon + 1));    // tm_mon 0-11 → 1-12
  Wire.write(dec2bcd(timeinfo->tm_year % 100)); // 只存后两位

  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    Serial.printf("[RTC] Write: I2C failed (err=%d)\n", err);
    // 尝试逐个寄存器写入
    Serial.println("[RTC] Trying single-register writes...");
    bool ok = true;
    uint8_t regs[] = {
        dec2bcd(timeinfo->tm_sec),       dec2bcd(timeinfo->tm_min),
        dec2bcd(timeinfo->tm_hour),      dec2bcd(timeinfo->tm_wday),
        dec2bcd(timeinfo->tm_mday),      dec2bcd(timeinfo->tm_mon + 1),
        dec2bcd(timeinfo->tm_year % 100)};
    for (int i = 0; i < 7; i++) {
      Wire.beginTransmission(rtc_addr);
      Wire.write(REG_ADDR(i));
      Wire.write(regs[i]);
      uint8_t e = Wire.endTransmission();
      if (e != 0) {
        Serial.printf("[RTC] Reg %d write failed (err=%d)\n", i, e);
        ok = false;
      }
    }
    if (!ok)
      return false;
  }

  Serial.printf("[RTC] Write: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
                timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min,
                timeinfo->tm_sec);
  return true;
}

bool rtc_sync_to_system() {
  struct tm timeinfo;
  if (!rtc_read(&timeinfo)) {
    return false;
  }

  // 设置系统时间
  time_t t = mktime(&timeinfo);
  struct timeval tv = {.tv_sec = t, .tv_usec = 0};
  settimeofday(&tv, NULL);

  Serial.printf("[RTC] System time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return true;
}
