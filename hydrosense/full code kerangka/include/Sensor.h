#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include <Preferences.h>

// =====================================================================
//  Rantai pengolahan (rumus_kalibrasi_ph.md):
//    register -> skala -> gerbang validitas -> gerbang stabilitas
//             -> median N -> pembulatan -> PhReading (siap kirim)
//  Firmware TIDAK mengoreksi pH. Kalibrasi ditulis ke register sensor.
// =====================================================================

// Hasil satu siklus pembacaan, sudah bersih + metadata mutu.
struct PhReading {
  bool        valid;        // >= 1 sampel lolos gerbang validitas
  double      ph;           // median, 2 desimal
  double      tempC;        // median, 1 desimal
  uint16_t    rawPh;        // sampel valid terakhir (diagnostik)
  int16_t     rawTemp;
  int         sampleCount;  // sampel valid yang masuk median
  int         modbusErrors; // sampel gagal pada siklus ini
  double      sd;           // sd jendela stabilitas (pH)
  double      drift;        // drift jendela (pH/menit)
  const char *quality;      // good | fair | unstable | error
  const char *phStatus;     // acidic | normal | alkaline
  bool        compliant;    // baku mutu pH 6-9 & quality good/fair
};

// Titik kalibrasi (ditulis ke sensor) atau verifikasi (dibaca saja).
struct CalPoint {
  bool          valid;
  double        phValue;    // nilai buffer referensi
  double        reading;    // pembacaan sensor yang stabil saat itu
  double        mv;         // tegangan elektroda dari register (NAN bila tak ada)
  unsigned long when;       // epoch detik
};

// Regresi verifikasi pH_ref = m * pH_sensor + b (bagian 4).
struct Verification {
  int    n;
  double m, b, r2, rmse, maxErr;
  bool   pass;
};

// Variabel Global yang diekspos
extern Preferences   prefs;
extern CalPoint      calPoints[3];    // [0]=4.01 kal-1, [1]=6.86 (tidak dipakai), [2]=9.18 kal-2
extern CalPoint      verifPoints[3];  // [0]=4.01, [1]=6.86, [2]=9.18  (verifikasi)
extern Verification  verif;
extern bool          calibrated;      // kedua titik kalibrasi sudah ditulis ke sensor
extern unsigned long lastCalibrated;  // epoch detik, 0 bila belum
extern double        lastTempC;
extern bool          sensorOnline;

// Deklarasi Fungsi Sensor
void      setupSensor();
PhReading readSensor();                                   // siklus median N sampel
bool      readModbusSensor(double &ph, double &tempC,
                           uint16_t *rawPh = nullptr, int16_t *rawTemp = nullptr,
                           double *mv = nullptr);         // 1 transaksi + gerbang validitas

void calibratePH4();      // tulis titik 1 (4.01) ke sensor
void calibratePH10();     // tulis titik 2 (9.18) ke sensor
void calibratePH7();      // = verifyPH7 (dipertahankan untuk perintah web "7")
void verifyPH4();
void verifyPH7();
void verifyPH10();
void computeVerification();
void writeDeviation();    // tulis offset sistematis ke register 0x0050
void resetCalibration();
void showStatus();
void showHealthReport();
void dumpModbusRegisters();

#endif
