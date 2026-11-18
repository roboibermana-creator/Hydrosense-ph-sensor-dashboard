#pragma once
#include <stdint.h>
// Rain gauge tipping bucket (kontak reed ke GPIO RAIN_PIN, lihat Rain.cpp).

// Pasang interrupt, muat total hari ini dari NVS, jalankan task pembaruan/kirim.
// Panggil sekali dari mulaiSemuaTask().
void rainMulai();

// Curah hujan 60 menit terakhir (mm/jam, jendela bergulir) dan total hari ini (mm).
float rainMmPerJam();
float rainMmHariIni();

// --- Dipakai perintah Serial (SerialMenu.cpp) ---
float rainMmPerTip();
bool rainSetMmPerTip(float mm); // 0,01..5 mm; disimpan di NVS (menimpa RAIN_MM_PER_TIP)
void rainNolkanHariIni();
void rainCetakStatus();
// Kalibrasi air takaran: Mulai -> tuang air -> ketukan terhitung sejak Mulai.
void rainKalibrasiMulai();
bool rainKalibrasiJalan();
uint32_t rainKetukanKalibrasi();
void rainKalibrasiSelesai();
