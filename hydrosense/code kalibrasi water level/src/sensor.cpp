#include "sensor.h"
#include "config.h"

// ============================================================
// INISIALISASI ADC
// ============================================================

void sensorBegin() {
  analogSetPinAttenuation(ADC_PIN, ADC_11db); // supaya bisa baca sampai ~3.3V
}

// ============================================================
// BACA SENSOR ASLI (ADC_PIN <- A_OUT converter)
// ============================================================

void readTankSensor(float &levelPercent, float &distanceCm, float &currentMa) {
  uint32_t totalMv = 0;

  for (int i = 0; i < ADC_SAMPLES; i++) {
    totalMv += analogReadMilliVolts(ADC_PIN);
    delay(2);
  }

  float vOut = (totalMv / (float)ADC_SAMPLES) / 1000.0; // mV -> V
  vOut = constrain(vOut, V_MIN_V, V_MAX_V);

  currentMa = I_MIN_MA + (vOut - V_MIN_V) * (I_MAX_MA - I_MIN_MA) / (V_MAX_V - V_MIN_V);
  currentMa = constrain(currentMa, I_MIN_MA, I_MAX_MA);

  levelPercent = (currentMa - I_MIN_MA) / (I_MAX_MA - I_MIN_MA) * 100.0;
  if (SENSOR_INVERTED) {
    levelPercent = 100.0 - levelPercent;
  }
  levelPercent = constrain(levelPercent, 0.0, 100.0);

  distanceCm = TANK_HEIGHT_CM * (1.0 - levelPercent / 100.0);
}

// ============================================================
// GET STATUS
// ============================================================

String getStatus(float levelPercent) {
  if (levelPercent <= TH_LOW_LOW)   return "LOW_LOW";
  if (levelPercent >= TH_HIGH_HIGH) return "HIGH_HIGH";
  if (levelPercent >= thHigh)       return "HIGH";
  if (levelPercent <= thLow)        return "LOW";
  return "NORMAL";
}