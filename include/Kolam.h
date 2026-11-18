#pragma once
// Gerbang "air kolam kering" untuk jadwal otomatis. Lihat Kolam.cpp.
#include <Arduino.h>

// true = air kolam cukup (atau gerbang dimatikan) -> siklus otomatis boleh jalan.
// false = ditolak (air kering / sensor level rusak). Pembacaan di-cache beberapa detik.
bool kolamBolehSiklus();

// true selama siklus otomatis sedang ditolak oleh gerbang ini.
bool kolamDitolak();

// Teks untuk dashboard (dikirim sebagai kolom "fase"): "" bila tidak ditolak.
const char *kolamStatusTeks();

// Batas tinggi air minimal (cm) yang bisa diubah lewat Serial (perintah 'J'),
// disimpan di NVS -- tidak perlu upload ulang. Default = KOLAM_MIN_AIR_CM (Config.h).
float kolamMinAirCm();
bool kolamSetMinAirCm(float cm); // false bila di luar 0..tinggi maksimum

// Tinggi maksimum (cm) = jarak sensor ke dasar kolam; juga dipakai untuk menghitung tinggi air &
// distance_cm dari level%. Default = TANK_HEIGHT_CM. Bisa diubah dari Serial 'J' / dashboard.
float kolamMaxAirCm();
bool kolamSetMaxAirCm(float cm); // false bila di luar 10..RADAR_RANGE_CM atau di bawah minimal
