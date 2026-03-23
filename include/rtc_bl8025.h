#ifndef __RTC_BL8025_H__
#define __RTC_BL8025_H__

#include <time.h>

// BL8025T RTC (兼容 RX8025) I2C 地址
#define BL8025_I2C_ADDR 0x32

/**
 * 初始化 RTC（I2C 已由 SHT30 初始化，共用总线）
 * 返回 true 表示 RTC 通信正常
 */
bool rtc_init();

/**
 * 从 RTC 读取当前时间并设置到系统时钟
 * 返回 true 表示读取成功
 */
bool rtc_read(struct tm *timeinfo);

/**
 * 将系统时间写入 RTC
 * 通常在 NTP 同步成功后调用
 */
bool rtc_write(const struct tm *timeinfo);

/**
 * 从 RTC 读取时间并设置为系统时间
 * 用于深度睡眠唤醒后恢复时间
 */
bool rtc_sync_to_system();

#endif
