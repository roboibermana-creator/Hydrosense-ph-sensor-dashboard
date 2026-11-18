#include "Level_Sensor.h"
#include "Config.h"
#include "Kolam.h"

float thLow = 20.0;
float thHigh = 80.0;

// ============================================================
// SENSOR LEVEL: RADAR 4-20 mA lewat converter DFRobot SEN0262 (ADC GPIO9)
// ============================================================

static volatile bool sensorOk = true;
bool levelSensorValid() { return sensorOk; }

void sensorBegin() {
  analogSetPinAttenuation(ADC_PIN, ADC_11db); // supaya bisa baca sampai ~3.3V
}

// BACA SENSOR ASLI (ADC_PIN <- A_OUT converter)
void readTankSensor(float &levelPercent, float &distanceCm, float &currentMa) {
  uint32_t totalMv = 0;

  for (int i = 0; i < ADC_SAMPLES; i++) {
    totalMv += analogReadMilliVolts(ADC_PIN);
    delay(2);
  }

  float vOut = (totalMv / (float)ADC_SAMPLES) / 1000.0; // mV -> V
  vOut = constrain(vOut, 0.0f, V_ADC_MAKS_V);

  // Arus loop dari tegangan converter: I = V / R_sense (SEN0262: 120 ohm).
  // Dulu dipetakan 0 V = 4 mA dan 3 V = 20 mA, yang salah untuk modul ini.
  float rawMa = vOut / SENSE_RESISTOR_OHM * 1000.0f;
  sensorOk = rawMa >= 3.6f && rawMa <= 21.0f; // di luar ini = loop putus / sensor rusak
  currentMa = constrain(rawMa, I_MIN_MA, I_MAX_MA);

  levelPercent = (currentMa - I_MIN_MA) / (I_MAX_MA - I_MIN_MA) * 100.0;
  if (SENSOR_INVERTED) {
    levelPercent = 100.0 - levelPercent;
  }
  levelPercent = constrain(levelPercent, 0.0, 100.0);

  distanceCm = kolamMaxAirCm() * (1.0 - levelPercent / 100.0);
}


// ============================================================
// GET STATUS
// ============================================================

String getStatus(float levelPercent) {
  if (levelPercent <= TH_LOW_LOW)
    return "LOW_LOW";
  if (levelPercent >= TH_HIGH_HIGH)
    return "HIGH_HIGH";
  if (levelPercent >= thHigh)
    return "HIGH";
  if (levelPercent <= thLow)
    return "LOW";
  return "NORMAL";
}
