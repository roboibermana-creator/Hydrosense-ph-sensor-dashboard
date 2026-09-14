#ifndef TSS_SENSOR_H
#define TSS_SENSOR_H

#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include "Config.h" // Include config untuk pin, ID, register

class TSS_Sensor {
private:
  ModbusMaster node;
  HardwareSerial& modbusSerial = Serial2; // Gunakan Serial2 yang sama dengan pH

  float calSlope;
  float calOffset;

  static void preTransmission();
  static void postTransmission();

public:
  TSS_Sensor();

  void begin();
  float readTSS();
  float readCalibratedTSS();

  void calibrateSpan(float rawLow, float actualLow, float rawHigh, float actualHigh);
  void calibrateLinearRegression(const float* rawValues, const float* actualValues, uint8_t n);
  
  void setCalibration(float slope, float offset);
  void getCalibration(float &slope, float &offset);

  void setScrapingTime(uint16_t minutes);
  void startScraping();
  void readStatus();
};

#endif // TSS_SENSOR_H