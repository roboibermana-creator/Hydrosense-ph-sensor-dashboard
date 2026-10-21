#ifndef TSS_SENSOR_H
#define TSS_SENSOR_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <ModbusMaster.h>
#include "Config.h"

/*
 * Driver for BOQU ZDYG-2087-01-compatible / RD-SSBWT-01 TSS sensor.
 *
 * IMPORTANT:
 * - Serial2 must be initialized exactly once by the main application.
 * - If pH and TSS share the same RS485 bus, all Modbus transactions on that
 *   bus must be serialized. Do not call the pH and TSS drivers concurrently
 *   from different tasks unless they use the SAME shared bus mutex.
 * - Register 13 is the measurement parameter (1=turbidity, 2=suspended solids),
 *   not a generic operating-status register.
 */
class TSS_Sensor {
private:
  ModbusMaster node;
  HardwareSerial &modbusSerial;

  float calSlope;
  float calOffset;

  static void preTransmission();
  static void postTransmission();

  bool readCalibrationMode(int16_t &mode);
  bool isMultiPointCalibrationActive(int16_t *modeOut = nullptr);

  bool tulisFloatRegister(uint16_t startReg, float value);
  bool bacaSingleRegister(uint16_t reg, int16_t &hasil);

public:
  TSS_Sensor();

  void begin();
  float readTSS();
  float readCalibratedTSS();

  // Software-side linear correction.
  void calibrateSpan(float rawLow, float actualLow,
                     float rawHigh, float actualHigh);
  void calibrateLinearRegression(const float *rawValues,
                                 const float *actualValues, uint8_t n);
  void setCalibration(float slope, float offset);
  void getCalibration(float &slope, float &offset);

  void setScrapingTime(uint16_t minutes);
  void startScraping();
  void readStatus();

  // Kept for source compatibility, but deliberately disabled: these routines
  // target slave ID 1, which Config.h assigns to the pH sensor in this project.
  // Do not issue ID-change or wiper commands to ID 1 on the shared bus.
  void changeSlaveIdToOneToTwo();
  void testWiperOnSlaveOne();

  // Register 13 is a measurement parameter, not a generic status.
  bool bacaParameterPengukuran(int16_t &parameter);
  bool bacaStatusSensor(int16_t &status); // Legacy alias; reads parameter 13.
  bool bacaKelembabanProbe(int16_t &kelembaban);

  bool tulisFactor(float factor);
  bool keluarModeKalibrasi();
  bool setModeKalibrasi(uint16_t mode);
  bool setTitikKalibrasi(uint16_t titik);
  bool tulisTargetKalibrasi(float nilaiPartech);
  bool tulisAktualKalibrasi(float nilaiSensor);
};

#endif // TSS_SENSOR_H
