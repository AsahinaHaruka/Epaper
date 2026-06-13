/*
电压 (V)	近似剩余电量	状态说明
4.20	100%	刚刚充满，充电器断开
4.10	~90%	电量非常充足
4.00	~80%	电量充足
3.90	~60%	中等电量
3.80	~50%	中等电量（接近标称电压）
3.75	~40%	电量偏低
3.70	~30%	标称电压点，但电量已不多
3.65	~20%	低电量
3.50	~10%	极低电量，应立即充电
3.30	0%	放电截止电压，继续放电将损坏电池

充电截止电压 4.2
放电截止电压 3.3
 */
#include "battery.h"
#include <Arduino.h>
#include <algorithm>

#include "wiring.h"

// 记录上次电量，RTC_DATA_ATTR 保证在 ESP32 深度休眠（Deep Sleep）期间数据不丢失
RTC_DATA_ATTR int g_last_battery_percent = -1;

/**
 * 获取电池电压（mV）
 * 硬件: WAKE_IO(GPIO25) HIGH → 开启分压电路 → GPIO36读取 → WAKE_IO LOW
 * 分压比: R14=10k, R15=10k → Vbat = Vadc × 2
 */
int readBatteryVoltage() {
  // 设置 ADC 衰减以支持读取高达 ~3.3V 的输入
  analogSetAttenuation(ADC_11db);

  // 开启测量电路
  pinMode(WAKE_IO_PIN, OUTPUT);
  digitalWrite(WAKE_IO_PIN, HIGH);
  delay(50); // 等待电压稳定

  // 1. 更好的采样逻辑：中位值平均滤波（去极值滤波）
  const int NUM_SAMPLES = 20;
  uint32_t samples[NUM_SAMPLES];

  for (int i = 0; i < NUM_SAMPLES; i++) {
    samples[i] = analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }

  // 关闭测量电路以省电
  digitalWrite(WAKE_IO_PIN, LOW);

  // 排序样本
  std::sort(samples, samples + NUM_SAMPLES);

  // 去掉最高和最低的各 20%（4个），对中间的 12 个求平均，有效剔除突发噪声
  const int TRIM = 4;
  uint32_t mvTotal = 0;
  for (int i = TRIM; i < NUM_SAMPLES - TRIM; i++) {
    mvTotal += samples[i];
  }
  uint32_t pinMv = mvTotal / (NUM_SAMPLES - 2 * TRIM);

  // 电池电压 = 引脚电压 × 2（分压比 10k:10k）
  int batteryMv = pinMv * 2;

  Serial.printf("Battery ADC pin: %dmV, bat: %dmV (%.2fV)\n", 
                pinMv, batteryMv, batteryMv / 1000.0);

  return batteryMv;
}

/**
 * 获取电池电量百分比（基于高分辨率查找表与防回弹）
 * 返回值 0-100
 */
int readBatteryPercent() {
  int cellMv = readBatteryVoltage(); // 已经是实际电池电压 mV

  // 更好的算法：采用更精细的锂电池放电曲线（基于高分辨率经验查找表）
  // 这个表格把平台期切分得更细，使得百分比随时间的变化更加线性
  static const struct {
    int mv;
    int pct;
  } table[] = {
      {4150, 100}, {4080, 95}, {4000, 90}, {3930, 85}, {3870, 80},
      {3820, 70},  {3780, 60}, {3750, 50}, {3730, 40}, {3700, 30},
      {3680, 25},  {3650, 20}, {3600, 10}, {3550, 5},  {3300, 0},
  };
  const int n = sizeof(table) / sizeof(table[0]);

  int current_percent = 0;
  if (cellMv >= table[0].mv) {
    current_percent = 100;
  } else if (cellMv <= table[n - 1].mv) {
    current_percent = 0;
  } else {
    for (int i = 0; i < n - 1; i++) {
      if (cellMv >= table[i + 1].mv) {
        // 线性插值
        current_percent = table[i + 1].pct + (cellMv - table[i + 1].mv) *
                                      (table[i].pct - table[i + 1].pct) /
                                      (table[i].mv - table[i + 1].mv);
        break;
      }
    }
  }

  // 2. 加入“防回弹（只降不升）”逻辑
  if (g_last_battery_percent == -1) {
    g_last_battery_percent = current_percent;
  } else {
    // 如果测到的新电量比历史电量大，判定为负载停止后的电压回弹，不予采纳
    if (current_percent > g_last_battery_percent) {
        // 兼容充电的情况：只有当电量跃升大于一定阈值（比如插上充电器），才允许电量上涨
        if (current_percent - g_last_battery_percent >= 5) {
            g_last_battery_percent = current_percent;
        } else {
            // 微小的回弹（如 WiFi 关闭后电压恢复），保持原电量不变
            current_percent = g_last_battery_percent;
        }
    } else {
        // 正常放电下降
        g_last_battery_percent = current_percent;
    }
  }

  return current_percent;
}