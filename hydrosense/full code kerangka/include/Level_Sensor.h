#pragma once

#include <Arduino.h>

// Inisialisasi ADC untuk sensor level
void sensorBegin();

// Baca sensor: mengembalikan level (%), jarak (cm), dan arus (mA) via referensi
void readTankSensor(float &levelPercent, float &distanceCm, float &currentMa);

// Tentukan status berdasarkan level: LOW_LOW / LOW / NORMAL / HIGH / HIGH_HIGH
String getStatus(float levelPercent);