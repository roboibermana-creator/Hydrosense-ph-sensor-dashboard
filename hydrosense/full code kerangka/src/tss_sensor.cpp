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
TSS_Sensor::TSS_Sensor() : calSlope(1.0f), calOffset(0.0f) {}

void TSS_Sensor::preTransmission() { digitalWrite(RS485_RE_DE_PIN, HIGH); }
void TSS_Sensor::postTransmission() { digitalWrite(RS485_RE_DE_PIN, LOW); }

void TSS_Sensor::begin() {
  Serial.println();
  Serial.println("================================");
  Serial.println("       INIT TSS SENSOR");
  Serial.println("       By magang polban      ");
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
  // pakai itu sebagai nilai awal (bukan wajib -- default tetap slope=1, offset=0).
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
    uint8_t result = node.readHoldingRegisters(REG_TURBIDITY, 2); // Baca 2 register
    if (result == node.ku8MBSuccess) {
      uint16_t lowWord = node.getResponseBuffer(0);
      uint16_t highWord = node.getResponseBuffer(1);
      
      float rawValue = convertRegistersToFloat(lowWord, highWord);
      
      // Datasheet sensor butuh scaling /100
      return rawValue / 100.0f;
    }
    delay(500);
  }
  Serial.println("ERROR: Failed to read TSS sensor");
  Serial.println("Cek bagian kabel atau pin bermasalah");
  return -1.0; // Error
}

float TSS_Sensor::readCalibratedTSS() {
  float raw = readTSS();
  if (raw < 0) return raw; // Teruskan kode error, jangan ikut dikalibrasi

  // Y = calSlope * X + calOffset  (bentuk umum dari Y = b0 + b1*X)
  float calibrated = calSlope * raw + calOffset;
  return (calibrated < 0) ? 0.0f : calibrated; // Nilai TSS fisik tidak mungkin negatif
}

void TSS_Sensor::calibrateSpan(float rawLow, float actualLow, float rawHigh, float actualHigh) {
  if (rawHigh == rawLow) {
    Serial.println("✗ Kalibrasi span gagal: rawLow == rawHigh");
    return;
  }
  // Garis lurus melalui 2 titik (titik rendah & titik tinggi) terhadap alat standar
  calSlope  = (actualHigh - actualLow) / (rawHigh - rawLow);
  calOffset = actualLow - (calSlope * rawLow);

  Serial.print("✓ Kalibrasi span selesai. Y = ");
  Serial.print(calSlope, 6);
  Serial.print("X + ");
  Serial.println(calOffset, 6);
}

void TSS_Sensor::calibrateLinearRegression(const float* rawValues, const float* actualValues, uint8_t n) {
  if (n < 2) {
    Serial.println("✗ Kalibrasi regresi gagal: minimal 2 titik data");
    return;
  }

  float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
  for (uint8_t i = 0; i < n; i++) {
    sumX  += rawValues[i];
    sumY  += actualValues[i];
    sumXY += rawValues[i] * actualValues[i];
    sumX2 += rawValues[i] * rawValues[i];
  }

  float denom = (n * sumX2) - (sumX * sumX);
  if (denom == 0) {
    Serial.println("✗ Kalibrasi regresi gagal: data raw tidak bervariasi");
    return;
  }

  // Metode least squares -- setara dengan b1 (slope) dan b0 (intercept) pada Y = b0 + b1*X
  calSlope  = ((n * sumXY) - (sumX * sumY)) / denom;
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
  uint8_t result = node.writeSingleRegister(REG_AUTO_SCRAPING_INTERVAL, minutes);
  if (result == node.ku8MBSuccess) {
    Serial.print("✓ Auto scraping interval set to: ");
    Serial.print(minutes);
    Serial.println(" min");
  } else {
    Serial.println("✗ Failed to set auto scraping interval!");
  }
}

void TSS_Sensor::startScraping() {
  Serial.println();
  Serial.println(">>> Starting Manual TSS Scraping...");
  uint8_t result = node.writeSingleRegister(REG_MANUAL_SCRAPING, 66);
  if (result == node.ku8MBSuccess) {
    Serial.println("Scraping command sent. Waiting approximately 15 s...");
    for (int i = 0; i < 15; i++) { delay(1000); Serial.print("."); }
    Serial.println();
    Serial.println("Scraping done!");
  } else {
    Serial.print("Failed to start scraping! Modbus Error: 0x");
    Serial.println(result, HEX);
  }
}

void TSS_Sensor::readStatus() {
  Serial.println();
  Serial.println("========== TSS SENSOR STATUS ==========");
  uint8_t result;

  // --------------------------------------------------------
  // TSS
  // --------------------------------------------------------
  result = node.readHoldingRegisters(REG_TURBIDITY, 2);
  if (result == node.ku8MBSuccess) {
    uint16_t lowWord = node.getResponseBuffer(0);
    uint16_t highWord = node.getResponseBuffer(1);
    
    float rawTSS = convertRegistersToFloat(lowWord, highWord) / 100.0f;
    float calibratedTSS = (calSlope * rawTSS) + calOffset;
    if (calibratedTSS < 0.0f) calibratedTSS = 0.0f;

    Serial.print("TSS Raw        : ");
    Serial.println(rawTSS, 3);
    Serial.print("TSS Calibrated : ");
    Serial.print(calibratedTSS, 2);
    Serial.println(" mg/L");
  } else {
    Serial.print("TSS : Read Error 0x");
    Serial.println(result, HEX);
  }
  delay(200);

  // --------------------------------------------------------
  // INTERNAL TEMPERATURE
  // --------------------------------------------------------
  result = node.readHoldingRegisters(REG_INTERNAL_TEMP, 2);
  if (result == node.ku8MBSuccess) {
    uint16_t lowWord = node.getResponseBuffer(0);
    uint16_t highWord = node.getResponseBuffer(1);
    
    float internalTemp = convertRegistersToFloat(lowWord, highWord);
    Serial.print("Temp Internal : ");
    Serial.print(internalTemp, 1);
    Serial.println(" °C");
  } else {
    Serial.print("Temp Internal : Read Error 0x");
    Serial.println(result, HEX);
  }
  delay(200);

  // --------------------------------------------------------
  // ELECTRODE TYPE
  // --------------------------------------------------------
  result = node.readHoldingRegisters(REG_ELECTRODE_TYPE, 1);
  if (result == node.ku8MBSuccess) {
    uint16_t type = node.getResponseBuffer(0);
    Serial.print("Electrode Type : ");
    if (type == 1) {
        Serial.println("With Cleaning");
    } else {
        Serial.println("Without Cleaning");
    }
  } else {
    Serial.print("Electrode Type : Read Error 0x");
    Serial.println(result, HEX);
  }
  Serial.println("=======================================");
}