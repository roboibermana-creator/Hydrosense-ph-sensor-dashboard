#pragma once

#include <Arduino.h>
#include "secrets.h"

// ============================================================
// SENSOR (RADAR 4-20mA -> CONVERTER SEN0262 A_OUT -> ADC)
// ============================================================

#define ADC_PIN        4    // (Diubah ke 4) A_OUT converter -> GPIO4. (ESP32-S3 tidak bisa pakai 34 untuk ADC)
#define ADC_SAMPLES    32   // BUKAN PIN! Ini artinya "ambil data 32 kali lalu dirata-rata"

// Rentang arus & tegangan converter (SEN0262: linear 4-20mA -> 0-3V)
extern const float I_MIN_MA;
extern const float I_MAX_MA;
extern const float V_MIN_V;
extern const float V_MAX_V;

// Tinggi tangki (cm), dipakai untuk hitung raw_distance_cm
extern const float TANK_HEIGHT_CM;

// PENTING - CEK POLARITAS SENSOR:
// false = 4mA berarti tangki KOSONG (0%), 20mA berarti PENUH (100%).
// Kalau radar dipasang di ATAS tangki (mengukur jarak ke permukaan air),
// biasanya terbalik -> set true.
extern const bool SENSOR_INVERTED;

// ============================================================
// TIMING WIFI
// ============================================================

extern const unsigned long WIFI_CONNECT_TIMEOUT;
extern const unsigned long WIFI_RECONNECT_INTERVAL;

// ============================================================
// THRESHOLD
// ============================================================

extern float thLow;    // diambil dari Supabase, bisa berubah runtime
extern float thHigh;

extern const float TH_LOW_LOW;
extern const float TH_HIGH_HIGH;

extern String lastStatus;

// ============================================================
// TIMING PENGIRIMAN DATA
// ============================================================

extern const unsigned long SEND_INTERVAL_MS;
extern const unsigned long THRESHOLD_FETCH_INTERVAL_MS;