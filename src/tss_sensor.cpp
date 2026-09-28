#include "TSS_Sensor.h"

// ============================================================
// KONVERSI RAW MODBUS KE FLOAT
// ============================================================
static float convertRegistersToFloat(uint16_t lowWord, uint16_t highWord) {
  uint32_t combined = ((uint32_t)highWord << 16) | lowWord;
  float value;
  memcpy(&value, &combined, sizeof(value));
  return value;
}

// Kebalikan dari convertRegistersToFloat() -- dipakai untuk menulis float
// (Factor, Target, Actual kalibrasi) via FC16, harus konsisten dengan urutan
// word yang sama supaya pembacaan REG_TSS_VALUE tetap benar (sudah terbukti
// jalan di hardware).
static void convertFloatToRegisters(float value, uint16_t &lowWord,
                                    uint16_t &highWord) {
  uint32_t combined;
  memcpy(&combined, &value, sizeof(value));
  lowWord = (uint16_t)(combined & 0xFFFF);
  highWord = (uint16_t)(combined >> 16);
}

TSS_Sensor::TSS_Sensor() : calSlope(1.0f), calOffset(0.0f) {}

void TSS_Sensor::preTransmission() { digitalWrite(RS485_RE_DE_PIN, HIGH); }
void TSS_Sensor::postTransmission() { digitalWrite(RS485_RE_DE_PIN, LOW); }

void TSS_Sensor::begin() {
  Serial.println();
  Serial.println("================================");
  Serial.println("       INIT TSS SENSOR          ");
  Serial.println("       By magang polban         ");
  Serial.println("         iqbal ganteng          ");
  Serial.println("================================");

  pinMode(RS485_RE_DE_PIN, OUTPUT);
  // Default = receive mode
  digitalWrite(RS485_RE_DE_PIN, LOW);

  // Serial2 sudah diinisialisasi oleh sensor lain
  node.begin(TSS_SLAVE_ID, modbusSerial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.print("TSS Slave ID : ");
  Serial.println(TSS_SLAVE_ID);

  // Jika koefisien hasil kalibrasi sebelumnya sudah didefinisikan di Config.h,
  // pakai itu sebagai nilai awal (bukan wajib -- default tetap slope=1,
  // offset=0).
#ifdef TSS_CAL_SLOPE
  calSlope = TSS_CAL_SLOPE;
#endif
#ifdef TSS_CAL_OFFSET
  calOffset = TSS_CAL_OFFSET;
#endif

  Serial.print("Regression Slope     : ");
  Serial.println(calSlope, 6);
  Serial.print("Regression Intercept : ");
  Serial.println(calOffset, 6);

  Serial.println("TSS Sensor Ready");
  Serial.println("================================");
}

float TSS_Sensor::readTSS() {
  for (int i = 0; i < 3; i++) {
    // RD-SSBWT-01 / BOQU family: register 2, function 03, quantity 2 -> float
    // TSS
    uint8_t result = node.readHoldingRegisters(REG_TSS_VALUE, 2);
    if (result == node.ku8MBSuccess) {
      uint16_t tssLow = node.getResponseBuffer(0);
      uint16_t tssHigh = node.getResponseBuffer(1);

      float tssValue = convertRegistersToFloat(tssLow, tssHigh);
      return tssValue;
    }
    delay(500);
  }
  Serial.println("ERROR: Failed to read TSS sensor");
  Serial.println("Cek bagian kabel atau pin bermasalah");
  return -1.0; // Error
}

float TSS_Sensor::readCalibratedTSS() {
  float raw = readTSS();
  if (raw < 0)
    return raw; // Teruskan kode error, jangan ikut dikalibrasi

  // Y = calSlope * X + calOffset  (bentuk umum dari Y = b0 + b1*X)
  float calibrated = calSlope * raw + calOffset;
  return (calibrated < 0) ? 0.0f
                          : calibrated; // Nilai TSS fisik tidak mungkin negatif
}

void TSS_Sensor::calibrateSpan(float rawLow, float actualLow, float rawHigh,
                               float actualHigh) {
  if (rawHigh == rawLow) {
    Serial.println("✗ Kalibrasi span gagal: rawLow == rawHigh");
    return;
  }
  // Garis lurus melalui 2 titik (titik rendah & titik tinggi) terhadap alat
  // standar
  calSlope = (actualHigh - actualLow) / (rawHigh - rawLow);
  calOffset = actualLow - (calSlope * rawLow);

  Serial.print("✓ Kalibrasi span selesai. Y = ");
  Serial.print(calSlope, 6);
  Serial.print("X + ");
  Serial.println(calOffset, 6);
}

void TSS_Sensor::calibrateLinearRegression(const float *rawValues,
                                           const float *actualValues,
                                           uint8_t n) {
  if (n < 2) {
    Serial.println("✗ Kalibrasi regresi gagal: minimal 2 titik data");
    return;
  }

  float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
  for (uint8_t i = 0; i < n; i++) {
    sumX += rawValues[i];
    sumY += actualValues[i];
    sumXY += rawValues[i] * actualValues[i];
    sumX2 += rawValues[i] * rawValues[i];
  }

  float denom = (n * sumX2) - (sumX * sumX);
  if (denom == 0) {
    Serial.println("✗ Kalibrasi regresi gagal: data raw tidak bervariasi");
    return;
  }

  // Metode least squares -- setara dengan b1 (slope) dan b0 (intercept) pada Y
  // = b0 + b1*X
  calSlope = ((n * sumXY) - (sumX * sumY)) / denom;
  calOffset = (sumY - (calSlope * sumX)) / n;

  Serial.print("✓ Regresi linear selesai. Y = ");
  Serial.print(calOffset, 6);
  Serial.print(" + ");
  Serial.print(calSlope, 6);
  Serial.println("X");
}

void TSS_Sensor::setCalibration(float slope, float offset) {
  calSlope = slope;
  calOffset = offset;
}

void TSS_Sensor::getCalibration(float &slope, float &offset) {
  slope = calSlope;
  offset = calOffset;
}

void TSS_Sensor::setScrapingTime(uint16_t minutes) {
  // RD-SSBWT-01 / BOQU family: register 21 (0x15) = auto brushing interval
  // (minutes)
  uint8_t result =
      node.writeSingleRegister(REG_AUTO_SCRAPING_INTERVAL, minutes);
  if (result == node.ku8MBSuccess) {
    Serial.print("✓ Brush interval set to: ");
    Serial.print(minutes);
    Serial.println(" min");
  } else {
    Serial.println("✗ Failed to set brush interval!");
  }
}

void TSS_Sensor::startScraping() {
  Serial.println();
  Serial.println(">>> Starting Manual TSS Scraping (RD-SSBWT-01)...");
  // register 20 (0x14), function 06, value 66 = manual brush/wiper ON
  uint8_t result =
      node.writeSingleRegister(REG_MANUAL_SCRAPING, TSS_MANUAL_SCRAPING_VALUE);
  if (result == node.ku8MBSuccess) {
    Serial.println("Scraping command sent (ACK diterima sensor). Waiting "
                   "approximately 15 s...");
    for (int i = 0; i < 15; i++) {
      delay(1000);
      Serial.print(".");
    }
    Serial.println();
    Serial.println("Scraping done!");
    Serial.println("CATATAN: ACK Modbus sukses TIDAK berarti motor wiper "
                   "benar-benar bergerak --");
    Serial.println(
        "dengarkan/perhatikan fisik probe untuk konfirmasi gerakan aktual.");
  } else {
    Serial.print("Failed to start scraping! Modbus Error: 0x");
    Serial.println(result, HEX);
  }
}

void TSS_Sensor::readStatus() {
  Serial.println();
  Serial.println("========== TSS SENSOR STATUS (RD-SSBWT-01) ==========");
  uint8_t result;

  // --------------------------------------------------------
  // TSS (register 2, function 03, quantity 2 -> float)
  // --------------------------------------------------------
  result = node.readHoldingRegisters(REG_TSS_VALUE, 2);
  if (result == node.ku8MBSuccess) {
    uint16_t tssLow = node.getResponseBuffer(0);
    uint16_t tssHigh = node.getResponseBuffer(1);
    float rawTSS = convertRegistersToFloat(tssLow, tssHigh);

    float calibratedTSS = (calSlope * rawTSS) + calOffset;
    if (calibratedTSS < 0.0f)
      calibratedTSS = 0.0f;

    Serial.print("TSS Raw        : ");
    Serial.println(rawTSS, 2);
    Serial.print("TSS Calibrated : ");
    Serial.print(calibratedTSS, 2);
    Serial.println(" mg/L");
  } else {
    Serial.print("TSS : Read Error 0x");
    Serial.println(result, HEX);
  }
  delay(200);

  // --------------------------------------------------------
  // Brushing time terakhir (register 11, informational)
  // --------------------------------------------------------
  result = node.readHoldingRegisters(REG_BRUSHING_TIME, 1);
  if (result == node.ku8MBSuccess) {
    uint16_t brushTime = node.getResponseBuffer(0);
    Serial.print("Brushing Time  : ");
    Serial.println(brushTime);
  } else {
    Serial.print("Brushing Time  : Read Error 0x");
    Serial.println(result, HEX);
  }
  delay(200);

  Serial.println("=======================================");
}
// =====================================================================
// FUNGSI DIAGNOSTIK
// =====================================================================
void TSS_Sensor::changeSlaveIdToOneToTwo() {
  Serial.println();
  Serial.println(">>> MENGUBAH SLAVE ID TSS DARI 1 KE 2 <<<");
  Serial.println("PERINGATAN: PASTIKAN SENSOR pH SUDAH DICABUT/DIPUTUS SEMENTARA DARI KABEL RS485!");
  Serial.println("Jika tidak, sensor pH akan ikut berubah ID-nya menjadi 2 dan rusak komunikasinya!");
  Serial.println("Kirim perintah dalam 5 detik...");
  delay(5000);
  
  // Buat instance ModbusMaster sementara untuk slave ID 1
  ModbusMaster tempNode;
  tempNode.begin(1, modbusSerial);
  tempNode.preTransmission(preTransmission);
  tempNode.postTransmission(postTransmission);
  
  // Register 17 (0x11) adalah Slave Address berdasarkan manual
  uint8_t result = tempNode.writeSingleRegister(17, 2);
  
  if (result == tempNode.ku8MBSuccess) {
    Serial.println("BERHASIL! Slave ID Sensor telah diubah menjadi 2.");
    Serial.println("Silakan matikan power (restart) ESP32 dan sensor agar efeknya terasa.");
  } else {
    Serial.print("GAGAL mengubah Slave ID. Modbus Error: 0x");
    Serial.println(result, HEX);
    Serial.println("Kemungkinan: Sensor sudah memakai ID 2, kabel bermasalah, atau sensor tidak aktif.");
  }
}

void TSS_Sensor::testWiperOnSlaveOne() {
  Serial.println();
  Serial.println(">>> TESTING WIPER DENGAN SLAVE ID 1 <<<");
  Serial.println("Mencoba memaksa wiper bergerak menggunakan Slave ID 1...");
  
  // Buat instance ModbusMaster sementara untuk slave ID 1
  ModbusMaster tempNode;
  tempNode.begin(1, modbusSerial);
  tempNode.preTransmission(preTransmission);
  tempNode.postTransmission(postTransmission);
  
  uint8_t result = tempNode.writeSingleRegister(REG_MANUAL_SCRAPING, 66);
  
  if (result == tempNode.ku8MBSuccess) {
    Serial.println("COMMAND WIPER KE ID 1 SUKSES TERKIRIM!");
    Serial.println("Jika wiper bergerak sekarang, berarti sensor Anda MASIH MENGGUNAKAN SLAVE ID 1.");
    Serial.println("Anda WAJIB mengubahnya ke ID 2 menggunakan perintah 'X'.");
  } else {
    Serial.print("Gagal mengirim wiper ke ID 1. Modbus Error: 0x");
    Serial.println(result, HEX);
  }
}

// =====================================================================
// KALIBRASI HARDWARE (two-point native, lihat Panduan Kalibrasi Sensor TSS)
// =====================================================================
bool TSS_Sensor::bacaSingleRegister(uint16_t reg, int16_t &hasil) {
  uint8_t result = node.readHoldingRegisters(reg, 1);
  if (result != node.ku8MBSuccess) {
    Serial.printf("✗ Baca register 0x%04X gagal, Modbus error 0x%02X\n", reg,
                  result);
    return false;
  }
  hasil = (int16_t)node.getResponseBuffer(0);
  return true;
}

bool TSS_Sensor::tulisFloatRegister(uint16_t startReg, float value) {
  uint16_t lowWord, highWord;
  convertFloatToRegisters(value, lowWord, highWord);
  node.setTransmitBuffer(0, lowWord);
  node.setTransmitBuffer(1, highWord);
  uint8_t result = node.writeMultipleRegisters(startReg, 2);
  if (result != node.ku8MBSuccess) {
    Serial.printf("✗ Tulis float ke register 0x%04X gagal, Modbus error 0x%02X\n",
                  startReg, result);
    return false;
  }
  return true;
}

bool TSS_Sensor::bacaStatusSensor(int16_t &status) {
  return bacaSingleRegister(REG_STATUS_SENSOR, status);
}

bool TSS_Sensor::bacaKelembabanProbe(int16_t &kelembaban) {
  return bacaSingleRegister(REG_KELEMBABAN_PROBE, kelembaban);
}

bool TSS_Sensor::tulisFactor(float factor) {
  return tulisFloatRegister(REG_CAL_FACTOR, factor);
}

bool TSS_Sensor::keluarModeKalibrasi() {
  uint8_t result =
      node.writeSingleRegister(REG_CAL_EXIT_CONTROL, TSS_CAL_EXIT_VALUE);
  if (result != node.ku8MBSuccess) {
    Serial.printf("✗ Keluar mode kalibrasi gagal, Modbus error 0x%02X\n",
                  result);
    return false;
  }
  return true;
}

bool TSS_Sensor::setModeKalibrasi(uint16_t mode) {
  uint8_t result = node.writeSingleRegister(REG_CAL_MODE, mode);
  if (result != node.ku8MBSuccess) {
    Serial.printf("✗ Set mode kalibrasi=%u gagal, Modbus error 0x%02X\n", mode,
                  result);
    return false;
  }
  return true;
}

bool TSS_Sensor::setTitikKalibrasi(uint16_t titik) {
  uint8_t result = node.writeSingleRegister(REG_CAL_POINT, titik);
  if (result != node.ku8MBSuccess) {
    Serial.printf("✗ Set titik kalibrasi=%u gagal, Modbus error 0x%02X\n",
                  titik, result);
    return false;
  }
  return true;
}

// PERINGATAN: register ini (0x0014) SAMA dengan REG_MANUAL_SCRAPING --
// panggil ini HANYA setelah setModeKalibrasi(2) aktif, JANGAN pernah
// dicampur dengan startScraping() dalam satu sesi kalibrasi.
bool TSS_Sensor::tulisTargetKalibrasi(float nilaiPartech) {
  return tulisFloatRegister(REG_CAL_TARGET, nilaiPartech);
}

bool TSS_Sensor::tulisAktualKalibrasi(float nilaiSensor) {
  return tulisFloatRegister(REG_CAL_ACTUAL, nilaiSensor);
}
