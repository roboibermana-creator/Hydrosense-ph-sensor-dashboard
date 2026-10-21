#include "TSS_Sensor.h"

#include <math.h>
#include <string.h>

// The manual's examples confirm low word first on the Modbus register map:
// TSS example response words 0x0000, 0x40E0 -> IEEE-754 bits 0x40E00000 -> 7.0.
static float convertRegistersToFloat(uint16_t word0, uint16_t word1) {
  const uint32_t bits = (static_cast<uint32_t>(word1) << 16) |
                        static_cast<uint32_t>(word0);
  float value = 0.0f;
  static_assert(sizeof(value) == sizeof(bits), "float must be 32-bit");
  memcpy(&value, &bits, sizeof(value));
  return value;
}

// Convert a float to the register order used by the sensor manual.
static void convertFloatToRegisters(float value,
                                    uint16_t &word0,
                                    uint16_t &word1) {
  uint32_t bits = 0;
  memcpy(&bits, &value, sizeof(value));
  word0 = static_cast<uint16_t>(bits & 0xFFFFU);
  word1 = static_cast<uint16_t>((bits >> 16) & 0xFFFFU);
}

static bool validPhysicalTSS(float value) {
  return isfinite(value) && value >= 0.0f && value <= 50000.0f;
}

TSS_Sensor::TSS_Sensor()
    : modbusSerial(Serial2), calSlope(1.0f), calOffset(0.0f) {}

void TSS_Sensor::preTransmission() {
  digitalWrite(RS485_RE_DE_PIN, HIGH); // TX mode; assumes RE and DE are tied.
}

void TSS_Sensor::postTransmission() {
  digitalWrite(RS485_RE_DE_PIN, LOW);  // RX mode; assumes RE and DE are tied.
}

void TSS_Sensor::begin() {
  pinMode(RS485_RE_DE_PIN, OUTPUT);
  digitalWrite(RS485_RE_DE_PIN, LOW);

  // Serial2.begin(...) must be called ONCE by the main application, e.g.:
  // Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  // Do not initialize the UART again here because the pH sensor shares it.
  node.begin(TSS_SLAVE_ID, modbusSerial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

#ifdef TSS_CAL_SLOPE
  calSlope = TSS_CAL_SLOPE;
#endif
#ifdef TSS_CAL_OFFSET
  calOffset = TSS_CAL_OFFSET;
#endif

  Serial.println();
  Serial.println("========== INIT TSS SENSOR ==========");
  Serial.printf("TSS Slave ID: %u\n", static_cast<unsigned>(TSS_SLAVE_ID));
  Serial.printf("Software calibration: slope=%.6f, offset=%.6f\n",
                calSlope, calOffset);
  Serial.println("TSS driver ready. UART configuration is managed by main.");
  Serial.println("====================================");
}

float TSS_Sensor::readTSS() {
  // Three attempts. The caller should still schedule polling according to the
  // sensor response time (register 12); these retries are for communication
  // recovery, not a measurement-stability filter.
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    const uint8_t result = node.readHoldingRegisters(REG_TSS_VALUE, 2);

    if (result == node.ku8MBSuccess) {
      const uint16_t word0 = node.getResponseBuffer(0);
      const uint16_t word1 = node.getResponseBuffer(1);
      const float tssValue = convertRegistersToFloat(word0, word1);

      if (validPhysicalTSS(tssValue)) {
        return tssValue;
      }

      Serial.printf(
          "TSS INVALID: word0=0x%04X, word1=0x%04X, value=%.7g; "
          "range expected 0..50000 mg/L\n",
          word0, word1, static_cast<double>(tssValue));
    } else {
      Serial.printf("TSS Modbus read failed: attempt %u/3, error=0x%02X\n",
                    static_cast<unsigned>(attempt + 1), result);
    }

    if (attempt < 2) {
      delay(250);
    }
  }

  Serial.println("ERROR: TSS read failed or decoded value was invalid.");
  return -1.0f;
}

float TSS_Sensor::readCalibratedTSS() {
  const float raw = readTSS();
  if (raw < 0.0f) {
    return raw; // Preserve error code; never calibrate a failed read.
  }

  if (!isfinite(calSlope) || !isfinite(calOffset)) {
    Serial.println("ERROR: software calibration coefficients are not finite.");
    return -1.0f;
  }

  const float calibrated = calSlope * raw + calOffset;
  if (!isfinite(calibrated) || calibrated > 50000.0f) {
    Serial.printf("ERROR: calibrated TSS out of range: %.7g mg/L\n",
                  static_cast<double>(calibrated));
    return -1.0f;
  }

  return (calibrated < 0.0f) ? 0.0f : calibrated;
}

void TSS_Sensor::calibrateSpan(float rawLow, float actualLow,
                               float rawHigh, float actualHigh) {
  if (!isfinite(rawLow) || !isfinite(actualLow) ||
      !isfinite(rawHigh) || !isfinite(actualHigh) ||
      fabsf(rawHigh - rawLow) < 1.0e-6f) {
    Serial.println("ERROR: invalid span calibration points.");
    return;
  }

  const float slope = (actualHigh - actualLow) / (rawHigh - rawLow);
  const float offset = actualLow - slope * rawLow;
  if (!isfinite(slope) || !isfinite(offset)) {
    Serial.println("ERROR: calculated span coefficients are not finite.");
    return;
  }

  calSlope = slope;
  calOffset = offset;
  Serial.printf("Software span calibration: Y = %.6f * X + %.6f\n",
                calSlope, calOffset);
}

void TSS_Sensor::calibrateLinearRegression(const float *rawValues,
                                           const float *actualValues,
                                           uint8_t n) {
  if (rawValues == nullptr || actualValues == nullptr || n < 2) {
    Serial.println("ERROR: regression calibration requires at least 2 valid points.");
    return;
  }

  float sumX = 0.0f;
  float sumY = 0.0f;
  float sumXY = 0.0f;
  float sumX2 = 0.0f;

  for (uint8_t i = 0; i < n; ++i) {
    if (!isfinite(rawValues[i]) || !isfinite(actualValues[i])) {
      Serial.printf("ERROR: invalid regression point at index %u.\n",
                    static_cast<unsigned>(i));
      return;
    }
    sumX += rawValues[i];
    sumY += actualValues[i];
    sumXY += rawValues[i] * actualValues[i];
    sumX2 += rawValues[i] * rawValues[i];
  }

  const float count = static_cast<float>(n);
  const float denom = count * sumX2 - sumX * sumX;
  if (!isfinite(denom) || fabsf(denom) < 1.0e-6f) {
    Serial.println("ERROR: raw regression points do not vary enough.");
    return;
  }

  const float slope = (count * sumXY - sumX * sumY) / denom;
  const float offset = (sumY - slope * sumX) / count;
  if (!isfinite(slope) || !isfinite(offset)) {
    Serial.println("ERROR: regression coefficients are not finite.");
    return;
  }

  calSlope = slope;
  calOffset = offset;
  Serial.printf("Software regression: Y = %.6f + %.6f * X\n",
                calOffset, calSlope);
}

void TSS_Sensor::setCalibration(float slope, float offset) {
  if (!isfinite(slope) || !isfinite(offset)) {
    Serial.println("ERROR: invalid software calibration coefficients.");
    return;
  }
  calSlope = slope;
  calOffset = offset;
}

void TSS_Sensor::getCalibration(float &slope, float &offset) {
  slope = calSlope;
  offset = calOffset;
}

bool TSS_Sensor::bacaSingleRegister(uint16_t reg, int16_t &hasil) {
  const uint8_t result = node.readHoldingRegisters(reg, 1);
  if (result != node.ku8MBSuccess) {
    Serial.printf("Read register 0x%04X failed, Modbus error=0x%02X\n",
                  reg, result);
    return false;
  }

  hasil = static_cast<int16_t>(node.getResponseBuffer(0));
  return true;
}

bool TSS_Sensor::readCalibrationMode(int16_t &mode) {
  return bacaSingleRegister(REG_CAL_MODE, mode);
}

bool TSS_Sensor::isMultiPointCalibrationActive(int16_t *modeOut) {
  int16_t mode = -1;
  if (!readCalibrationMode(mode)) {
    return true; // Fail closed: do not issue overlapping writes if mode is unknown.
  }
  if (modeOut != nullptr) {
    *modeOut = mode;
  }
  return (mode == TSS_CAL_MODE_TWO_POINT || mode == 3);
}

bool TSS_Sensor::bacaParameterPengukuran(int16_t &parameter) {
  return bacaSingleRegister(REG_MEASUREMENT_PARAMETER, parameter);
}

bool TSS_Sensor::bacaStatusSensor(int16_t &status) {
  // Backward-compatible alias. Register 13 is NOT a general status register.
  return bacaParameterPengukuran(status);
}

bool TSS_Sensor::bacaKelembabanProbe(int16_t &kelembaban) {
  return bacaSingleRegister(REG_KELEMBABAN_PROBE, kelembaban);
}

void TSS_Sensor::setScrapingTime(uint16_t minutes) {
  if (minutes == 0) {
    Serial.println("ERROR: brushing interval must be a non-zero minute value.");
    return;
  }

  int16_t mode = -1;
  if (isMultiPointCalibrationActive(&mode)) {
    Serial.printf(
        "BLOCKED: cannot write brushing interval while calibration mode is "
        "active/unknown (register 27=%d).\n", mode);
    return;
  }

  const uint8_t result = node.writeSingleRegister(REG_AUTO_SCRAPING_INTERVAL,
                                                   minutes);
  if (result == node.ku8MBSuccess) {
    Serial.printf("Brushing interval set to %u minute(s).\n",
                  static_cast<unsigned>(minutes));
  } else {
    Serial.printf("Failed to set brushing interval; Modbus error=0x%02X\n",
                  result);
  }
}

void TSS_Sensor::startScraping() {
  int16_t mode = -1;
  if (isMultiPointCalibrationActive(&mode)) {
    Serial.printf(
        "BLOCKED: register 20 is used for calibration target in mode 2/3, "
        "or calibration mode could not be read (register 27=%d).\n", mode);
    return;
  }

  const uint8_t result = node.writeSingleRegister(REG_MANUAL_SCRAPING,
                                                   TSS_MANUAL_SCRAPING_VALUE);
  if (result == node.ku8MBSuccess) {
    Serial.println("Manual brushing command acknowledged by Modbus.");
    Serial.println("ACK does not prove the wiper motor physically moved.");
  } else {
    Serial.printf("Manual brushing command failed; Modbus error=0x%02X\n",
                  result);
  }
}

void TSS_Sensor::readStatus() {
  Serial.println();
  Serial.println("========== TSS SENSOR DIAGNOSTICS ==========");

  // Read measurement value using the same validation path as normal polling.
  const float rawTSS = readTSS();
  if (rawTSS >= 0.0f) {
    const float calibrated = calSlope * rawTSS + calOffset;
    Serial.printf("TSS raw         : %.2f mg/L\n", rawTSS);
    if (isfinite(calibrated) && calibrated >= 0.0f && calibrated <= 50000.0f) {
      Serial.printf("TSS software cal: %.2f mg/L\n", calibrated);
    } else {
      Serial.println("TSS software cal: INVALID (check slope/offset)");
    }
  } else {
    Serial.println("TSS raw         : READ ERROR / INVALID DATA");
  }

  int16_t value = -1;
  if (bacaSingleRegister(REG_BRUSHING_TIME, value)) {
    Serial.printf("Brushing time (reg 11): %d\n", value);
  }

  if (bacaSingleRegister(REG_RESPONSE_TIME, value)) {
    Serial.printf("Response time (reg 12): %d second(s)\n", value);
    if (value < 3 || value > 60) {
      Serial.println("WARNING: response-time value is outside documented 3..60 s.");
    }
  }

  if (bacaParameterPengukuran(value)) {
    Serial.printf("Measurement parameter (reg 13): %d\n", value);
    if (value == TSS_PARAMETER_SUSPENDED_SOLIDS) {
      Serial.println("Measurement mode: suspended solids.");
    } else if (value == TSS_PARAMETER_TURBIDITY) {
      Serial.println("WARNING: sensor is configured for turbidity, not suspended solids.");
    } else {
      Serial.println("WARNING: unknown measurement parameter.");
    }
  }

  if (bacaKelembabanProbe(value)) {
    Serial.printf("Sensor humidity (reg 14): %d\n", value);
    if (value >= 10) {
      Serial.println("WARNING: manual recommends sensor humidity below 10; inspect probe/seals.");
    }
  }

  if (readCalibrationMode(value)) {
    Serial.printf("Calibration mode (reg 27): %d", value);
    if (value == TSS_CAL_MODE_FACTOR) {
      Serial.println(" (factor calibration)");
    } else if (value == TSS_CAL_MODE_TWO_POINT) {
      Serial.println(" (two-point calibration)");
    } else if (value == 3) {
      Serial.println(" (four-point calibration)");
    } else {
      Serial.println(" (value not described as a calibration mode in the manual)");
    }
  }

  const uint8_t factorResult = node.readHoldingRegisters(REG_CAL_FACTOR, 2);
  if (factorResult == node.ku8MBSuccess) {
    const uint16_t word0 = node.getResponseBuffer(0);
    const uint16_t word1 = node.getResponseBuffer(1);
    const float factor = convertRegistersToFloat(word0, word1);
    Serial.printf("Hardware factor (reg 6-7): %.6f\n", factor);
    if (!isfinite(factor) || factor < TSS_FACTOR_MIN || factor > TSS_FACTOR_MAX) {
      Serial.println("WARNING: factor outside documented range 0.11..9.99.");
    }
  } else {
    Serial.printf("Hardware factor read failed, Modbus error=0x%02X\n", factorResult);
  }

  float slope = 1.0f;
  float offset = 0.0f;
  getCalibration(slope, offset);
  Serial.printf("Software slope/offset: %.6f / %.6f\n", slope, offset);
  Serial.println("===========================================");
}

bool TSS_Sensor::tulisFloatRegister(uint16_t startReg, float value) {
  if (!isfinite(value)) {
    Serial.println("ERROR: refusing to write a non-finite float to Modbus.");
    return false;
  }

  uint16_t word0 = 0;
  uint16_t word1 = 0;
  convertFloatToRegisters(value, word0, word1);
  node.setTransmitBuffer(0, word0);
  node.setTransmitBuffer(1, word1);

  const uint8_t result = node.writeMultipleRegisters(startReg, 2);
  if (result != node.ku8MBSuccess) {
    Serial.printf("Float write at register 0x%04X failed, Modbus error=0x%02X\n",
                  startReg, result);
    return false;
  }
  return true;
}

bool TSS_Sensor::tulisFactor(float factor) {
  if (!isfinite(factor) ||
      factor < TSS_FACTOR_MIN || factor > TSS_FACTOR_MAX) {
    Serial.printf("ERROR: factor must be finite and between %.2f and %.2f.\n",
                  TSS_FACTOR_MIN, TSS_FACTOR_MAX);
    return false;
  }
  return tulisFloatRegister(REG_CAL_FACTOR, factor);
}

bool TSS_Sensor::setModeKalibrasi(uint16_t mode) {
  if (mode != TSS_CAL_MODE_FACTOR &&
      mode != TSS_CAL_MODE_TWO_POINT && mode != 3) {
    Serial.println("ERROR: calibration mode must be 1 (factor), 2 (two-point), or 3 (four-point).");
    return false;
  }

  const uint8_t result = node.writeSingleRegister(REG_CAL_MODE, mode);
  if (result != node.ku8MBSuccess) {
    Serial.printf("Setting calibration mode failed; Modbus error=0x%02X\n", result);
    return false;
  }
  Serial.printf("Calibration mode command acknowledged: %u\n",
                static_cast<unsigned>(mode));
  return true;
}

bool TSS_Sensor::setTitikKalibrasi(uint16_t titik) {
  int16_t mode = -1;
  if (!readCalibrationMode(mode)) {
    Serial.println("ERROR: cannot read calibration mode; point not written.");
    return false;
  }

  uint16_t maxPoint = 0;
  if (mode == TSS_CAL_MODE_TWO_POINT) {
    maxPoint = 2;
  } else if (mode == 3) {
    maxPoint = 4;
  } else {
    Serial.printf("ERROR: register 27=%d is not a multi-point calibration mode.\n", mode);
    return false;
  }

  if (titik < 1 || titik > maxPoint) {
    Serial.printf("ERROR: calibration point must be 1..%u for mode %d.\n",
                  static_cast<unsigned>(maxPoint), mode);
    return false;
  }

  const uint8_t result = node.writeSingleRegister(REG_CAL_POINT, titik);
  if (result != node.ku8MBSuccess) {
    Serial.printf("Setting calibration point failed; Modbus error=0x%02X\n", result);
    return false;
  }
  return true;
}

bool TSS_Sensor::tulisTargetKalibrasi(float nilaiPartech) {
  if (!validPhysicalTSS(nilaiPartech)) {
    Serial.println("ERROR: calibration target must be finite and in 0..50000 mg/L.");
    return false;
  }

  int16_t mode = -1;
  if (!readCalibrationMode(mode) ||
      (mode != TSS_CAL_MODE_TWO_POINT && mode != 3)) {
    Serial.printf(
        "ERROR: target write blocked; register 27 must be readable and "
        "equal to mode 2 or 3 (current=%d).\n", mode);
    return false;
  }
  return tulisFloatRegister(REG_CAL_TARGET, nilaiPartech);
}

bool TSS_Sensor::tulisAktualKalibrasi(float nilaiSensor) {
  if (!validPhysicalTSS(nilaiSensor)) {
    Serial.println("ERROR: calibration actual must be finite and in 0..50000 mg/L.");
    return false;
  }

  int16_t mode = -1;
  if (!readCalibrationMode(mode) ||
      (mode != TSS_CAL_MODE_TWO_POINT && mode != 3)) {
    Serial.printf(
        "ERROR: actual write blocked; register 27 must be readable and "
        "equal to mode 2 or 3 (current=%d).\n", mode);
    return false;
  }
  return tulisFloatRegister(REG_CAL_ACTUAL, nilaiSensor);
}

bool TSS_Sensor::keluarModeKalibrasi() {
  uint8_t res = node.writeSingleRegister(REG_CAL_EXIT_CONTROL, TSS_CAL_EXIT_VALUE);
  if (res == node.ku8MBSuccess) {
    return true;
  }
  return setModeKalibrasi(TSS_CAL_MODE_FACTOR);
}

void TSS_Sensor::changeSlaveIdToOneToTwo() {
  // Deliberately disabled: slave ID 1 is assigned to the pH sensor in this
  // project. Writing register 17 at ID 1 could change the pH sensor's address.
  Serial.println("BLOCKED: ID-change routine targets slave ID 1, which is the pH sensor in this configuration. Isolate hardware and use a vendor-approved procedure if an address change is truly required.");
}

void TSS_Sensor::testWiperOnSlaveOne() {
  // Deliberately disabled for the same reason as changeSlaveIdToOneToTwo().
  Serial.println("BLOCKED: wiper test targets slave ID 1 (pH sensor), not the TSS sensor at ID 2.");
}
