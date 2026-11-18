#pragma once

#include <Arduino.h>

// Inisialisasi ADC untuk sensor level
void sensorBegin();

// Baca sensor: mengembalikan level (%), jarak (cm), dan arus (mA) via referensi
void readTankSensor(float &levelPercent, float &distanceCm, float &currentMa);

// false bila sensor level terbaca rusak (radar: arus loop < 3,6 mA = kabel putus / sensor mati,
// atau > 21 mA). Dipakai gerbang "air kolam kering" supaya sensor rusak tidak dianggap air penuh.
bool levelSensorValid();

// Tentukan status berdasarkan level: LOW_LOW / LOW / NORMAL / HIGH / HIGH_HIGH
String getStatus(float levelPercent);