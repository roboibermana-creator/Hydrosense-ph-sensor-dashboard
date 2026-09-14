#pragma once

#include <Arduino.h>

// Setup mode STA + auto reconnect
void wifiBegin();

// Koneksi blocking dengan timeout. true kalau berhasil.
bool connectWiFi();

// Dipanggil tiap loop. Non-blocking, reconnect otomatis kalau putus.
void maintainWiFi();

// Cetak detail koneksi ke Serial
void printWiFiStatus();