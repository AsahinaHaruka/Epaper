#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Adafruit_SHT31.h>
#include <Wire.h>

struct SensorData {
  float temperature;
  float humidity;
  bool valid;
};

class SensorManager {
public:
  void begin(int sda, int scl);
  SensorData read();

private:
  Adafruit_SHT31 sht31;
};

#endif
