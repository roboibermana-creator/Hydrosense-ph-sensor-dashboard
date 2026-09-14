#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
//  Konfigurasi modul VALVE & POMPA (Hydrosense)
//  Nilai bertanda "// <-- ISI" belum dikonfirmasi: sesuaikan dengan wiring.
// =====================================================================

// --- PIN RELAY -------------------------------------------------------
#define PUMP_PIN          25    // <-- ISI: relay pompa sampling
#define VALVE1_PIN        26    // <-- ISI: relay valve 1 (drain pipa sampling)
#define VALVE2_OPEN_PIN   27    // <-- ISI: relay "buka" valve 2 (dosis tawas, motorized)
#define VALVE2_CLOSE_PIN  14    // <-- ISI: relay "tutup" valve 2

// Modul relay umum aktif LOW (IN=LOW -> relay ON). Ubah ke 0 bila aktif HIGH.
#define RELAY_ACTIVE_LOW  1

// --- TIMING VALVE 2 (motorized ball valve) ---------------------------
// Dosis dihitung sebagai lama valve dibuka:
//   durasi = START_DELAY + (FULL_OPEN - START_DELAY) * persen/100
// START_DELAY = waktu mati sebelum aliran mulai keluar,
// FULL_OPEN   = waktu sampai valve terbuka penuh.
#define VALVE2_START_DELAY_MS     2000UL   // <-- ISI
#define VALVE2_FULL_OPEN_TIME_MS 10000UL   // <-- ISI

// --- SAMPLING MANUAL -------------------------------------------------
#define SAMPLING_DURATION  300000UL        // 5 menit jendela sampling setelah PUMP_ON

// --- SERIAL ------------------------------------------------------------
#define SERIAL_BAUD 115200

#endif
