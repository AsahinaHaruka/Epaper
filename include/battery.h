#ifndef __BATTERY_H__
#define __BATTERY_H__

int readBatteryVoltage();
int readBatteryPercent();

int voltageToPercent(int cellMv);

#endif
