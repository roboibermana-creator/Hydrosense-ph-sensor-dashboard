// Pembungkus aktuator & sensor yang dipakai state machine (Modes.h).
#include "App.h"
#include <math.h>

bool kunciModbus(uint32_t timeoutMs) {
  return modbusMutex &&
         xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void lepasModbus() {
  if (modbusMutex)
    xSemaphoreGive(modbusMutex);
}

// ---------------- Aktuator ----------------

void pompaOn() { pumpON(); }

void pompaOff() { pumpOFF(); }

bool valveBuka() { return valveController.openValve1(); }

bool valveTutup() { return valveController.closeValve1(); }
void valveUpdate() { valveController.update(); }
bool valveTerbuka() { return valveController.getValve1Status(); }

void tampilkanError(const char *pesan) {
  Serial.print("[ERROR] ");
  Serial.println(pesan);
  digitalWrite(PIN_LED_ERROR, HIGH);
}

bool permintaanBerhenti() {
  // TODO: baca flag "stop" dari kolom terpisah di perintah control (device_commands)
  return false;
}
// ---------------- Sensor Level (Radar 4-20mA) ----------------

float bacaJarakAir() {
  float levelPercent = 0.0;
  float distanceCm = 0.0;
  float currentMa = 0.0;
  readTankSensor(levelPercent, distanceCm, currentMa);
  return distanceCm;
}

bool bacaFloatSwitch() { return digitalRead(PIN_FLOAT_SWITCH) == HIGH; }
// ---------------- TSS (Modbus) ----------------
// Rata-ratakan RAW dulu baru dikalibrasi sekali di akhir (bukan rata-rata
// hasil kalibrasi per-sample) -- setara secara matematis untuk transformasi
// linear, tapi sekalian dapat rata-rata raw (rawOut) untuk dipakai sebagai
// sample kalibrasi TSS di dashboard tanpa baca sensor 2x.

float bacaRataRataTSS(int nSample, float *rawOut) {
  float sumRaw = 0.0;
  int count = 0;
  for (int i = 0; i < nSample; i++) {
    float val = tssSensor.readTSS();
    if (val >= 0) {
      sumRaw += val;
      count++;
    }
    delay(100);
  }
  float avgRaw = (count > 0) ? (sumRaw / count) : 0.0;
  if (rawOut)
    *rawOut = avgRaw;
  float slope, offset;
  tssSensor.getCalibration(slope, offset);
  float calibrated = (slope * avgRaw) + offset;
  if (!isfinite(calibrated) || calibrated < 0.0f) {
    calibrated = 0.0f;
  }
  tssRawTerakhir = avgRaw;
  tssTerakhir = calibrated;
  float suhu = NAN;
  suhuTssTerakhir = tssSensor.readSuhu(suhu) ? suhu : NAN;
  float ntu = NAN;
  ntuTerakhir = tssSensor.readNtu(ntu) ? ntu : NAN;
  // Mode uji: seven segment menampilkan status float switch (lihat loop()),
  // bukan nilai TSS.
  return calibrated;
}

float trimmedMean(float *data, int n, float frac) {
  if (n <= 0)
    return 0.0f;
  // Urutkan naik (insertion sort; n kecil).
  for (int i = 1; i < n; i++) {
    float v = data[i];
    int j = i - 1;
    while (j >= 0 && data[j] > v) {
      data[j + 1] = data[j];
      j--;
    }
    data[j + 1] = v;
  }
  int potong = (int)(n * frac); // 20% dari 20 sampel = 4 di tiap sisi
  if (n - 2 * potong < 1)
    potong = 0;
  float jumlah = 0.0f;
  for (int i = potong; i < n - potong; i++)
    jumlah += data[i];
  return jumlah / (n - 2 * potong);
}

float trimmedMeanTSS(float *data, int n, float frac, float *rawOut) {
  float avgRaw = trimmedMean(data, n, frac);
  if (rawOut)
    *rawOut = avgRaw;
  float slope, offset;
  tssSensor.getCalibration(slope, offset);
  float calibrated = (slope * avgRaw) + offset;
  if (!isfinite(calibrated) || calibrated < 0.0f)
    calibrated = 0.0f;
  tssRawTerakhir = avgRaw;
  tssTerakhir = calibrated;
  return calibrated;
}

// ===================================================================
//        MODBUS RTU (baca sensor pH via Sensor.cpp)

// ===================================================================

HasilPH bacaProsesPH() {
  HasilPH hasil;
  PhReading phRead = readSensor();
  hasil.valid = phRead.valid;
  hasil.ph = phRead.ph;
  hasil.suhu = phRead.tempC;
  hasil.mv = isnan(phRead.mv) ? NAN : (float)phRead.mv;
  hasil.sd = phRead.sd;
  hasil.drift = phRead.drift;
  hasil.kualitas = String(phRead.quality);
  hasil.compliant = phRead.compliant;
  hasil.rawPh = phRead.rawPh;
  hasil.rawTemp = phRead.rawTemp;
  hasil.modbusErrorCount = phRead.modbusErrors;
  return hasil;
}
