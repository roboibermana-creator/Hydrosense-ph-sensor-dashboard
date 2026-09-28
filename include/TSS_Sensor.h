#ifndef TSS_SENSOR_H
#define TSS_SENSOR_H

#include "Config.h" // Include config untuk pin, ID, register
#include <HardwareSerial.h>
#include <ModbusMaster.h>

class TSS_Sensor {
private:
  ModbusMaster node;
  HardwareSerial &modbusSerial = Serial2; // Gunakan Serial2 yang sama dengan pH

  float calSlope;
  float calOffset;

  static void preTransmission();
  static void postTransmission();

public:
  TSS_Sensor();

  void begin();
  float readTSS();
  float readCalibratedTSS();

  void calibrateSpan(float rawLow, float actualLow, float rawHigh,
                     float actualHigh);
  void calibrateLinearRegression(const float *rawValues,
                                 const float *actualValues, uint8_t n);

  void setCalibration(float slope, float offset);
  void getCalibration(float &slope, float &offset);

  void setScrapingTime(uint16_t minutes);
  void startScraping();
  void readStatus();

  // Fungsi diagnostik (lihat komentar implementasi di tss_sensor.cpp)
  void changeSlaveIdToOneToTwo();
  void testWiperOnSlaveOne();

  // ==================== KALIBRASI HARDWARE (two-point native) ====================
  // Implementasi langkah-langkah di Panduan Kalibrasi Sensor TSS resmi
  // (RD-SSBWT-01/BOQU) -- semua tulis/baca register langsung ke sensor fisik,
  // menggantikan Modbus Poll + USB-RS485. Tiap fungsi return true kalau ACK
  // Modbus sukses (BUKAN jaminan nilai sudah "benar" secara fisik -- itu
  // tanggung jawab operator/urutan langkah).
  bool bacaStatusSensor(int16_t &status);       // reg 13
  bool bacaKelembabanProbe(int16_t &kelembaban); // reg 14
  bool tulisFactor(float factor);                // reg 6 (float)
  bool keluarModeKalibrasi();                    // reg 59 = 33
  bool setModeKalibrasi(uint16_t mode);          // reg 27
  bool setTitikKalibrasi(uint16_t titik);        // reg 28
  bool tulisTargetKalibrasi(float nilaiPartech); // reg 20 (float) -- HANYA
                                                  // valid saat mode = 2
  bool tulisAktualKalibrasi(float nilaiSensor);  // reg 22 (float) -- HANYA
                                                  // valid saat mode = 2

private:
  bool tulisFloatRegister(uint16_t startReg, float value);
  bool bacaSingleRegister(uint16_t reg, int16_t &hasil);
};

#endif // TSS_SENSOR_H