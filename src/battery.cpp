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

#include "wiring.h"

// BAT_ADC_PIN and WAKE_IO_PIN are defined in wiring.h

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

  // 多次采样取平均（消除抖动）
  uint32_t rawTotal = 0;
  const int samples = 10;
  for (int i = 0; i < samples; i++) {
    rawTotal += analogRead(BAT_ADC_PIN);
    delay(2);
  }
  uint32_t rawAvg = rawTotal / samples;

  // 也用 analogReadMilliVolts 获取校准后电压
  uint32_t mvTotal = 0;
  for (int i = 0; i < samples; i++) {
    mvTotal += analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }
  uint32_t pinMv = mvTotal / samples;

  // 关闭测量电路以省电
  digitalWrite(WAKE_IO_PIN, LOW);

  // 电池电压 = 引脚电压 × 2（分压比 10k:10k）
  int batteryMv = pinMv * 2;

  Serial.printf("Battery ADC raw: %d, pin: %dmV, bat: %dmV (%.2fV)\n", rawAvg,
                pinMv, batteryMv, batteryMv / 1000.0);

  return batteryMv;
}

/**
 * 获取电池电量百分比（基于电压查找表）
 * 返回值 0-100
 */
int readBatteryPercent() {
  int cellMv = readBatteryVoltage(); // 已经是实际电池电压 mV

  // 基于锂电池放电曲线的查找表（mV → %）
  struct {
    int mv;
    int pct;
  } table[] = {
      {4200, 100}, {4100, 90}, {4000, 80}, {3900, 60}, {3800, 50},
      {3750, 40},  {3700, 30}, {3650, 20}, {3500, 10}, {3300, 0},
  };
  const int n = sizeof(table) / sizeof(table[0]);

  if (cellMv >= table[0].mv)
    return 100;
  if (cellMv <= table[n - 1].mv)
    return 0;

  for (int i = 0; i < n - 1; i++) {
    if (cellMv >= table[i + 1].mv) {
      // 线性插值
      return table[i + 1].pct + (cellMv - table[i + 1].mv) *
                                    (table[i].pct - table[i + 1].pct) /
                                    (table[i].mv - table[i + 1].mv);
    }
  }
  return 0;
}