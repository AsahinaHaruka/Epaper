#include "SensorManager.h"

void SensorManager::begin(int sda, int scl) {
  Wire.begin(sda, scl);
  if (!sht31.begin(0x44)) { // Set to 0x45 for alternate i2c addr
    if (!sht31.begin(0x45)) {
      Serial.println("Couldn't find SHT31");
    }
  }
}

SensorData SensorManager::read() {
  SensorData data;
  data.temperature = sht31.readTemperature();
  data.humidity = sht31.readHumidity();

  if (!isnan(data.temperature) && !isnan(data.humidity)) {
    data.valid = true;
  } else {
    data.valid = false;
    Serial.println("Failed to read from SHT31");
  }
  return data;
}
