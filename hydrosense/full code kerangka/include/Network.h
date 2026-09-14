#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Config.h"

// ========================= HASIL PROSES pH =========================
struct HasilPH {
  bool   valid;          // lolos gerbang validitas & cukup sample sukses?
  float  ph;             // sudah median + dibulatkan 2 desimal
  float  suhu;            // dibulatkan 1 desimal
  float  sd;              // standar deviasi sample (pH)
  float  drift;            // pH per menit
  String kualitas;         // "good" | "fair" | "unstable" | "error"
  bool   compliant;        // baku mutu 6-9 DAN kualitas good/fair
  uint16_t rawPh;
  int16_t  rawTemp;
  int    modbusErrorCount;
};

// ========================= BUFFER OFFLINE =========================
struct DataSample {
  float ph, suhu, tss;
  String kualitas;
  bool compliant;
  uint16_t rawPh;
  int16_t rawTemp;
  float sd, drift;
  int modbusErrorCount;
  bool phValid;     // false -> ini record status error (ph tidak dikirim sbg angka)
  bool terisi;
};

extern const int UKURAN_BUFFER;
extern DataSample buffer[];
extern int bufferHead;

// Konfigurasi API
extern const char* SUPABASE_URL;
extern const char* SUPABASE_API_KEY;
extern const char* TABLE_READINGS;
extern const char* TABLE_MODE;

// ========================= PERINTAH MANUAL (VALVE & POMPA) =========================
// Dibaca dari baris TERBARU (order by updated_at desc) di TABLE_MODE.
// Web menulis baris baru setiap kali operator menekan toggle valve/pompa.
struct KontrolManual {
  bool   ok;        // true jika berhasil diambil & di-parse dari Supabase
  String mode;       // "otomatis" | "manual"
  String valveCmd;    // "buka" | "tutup"
  String pumpCmd;      // "jalan" | "berhenti"
};

void connectWiFi();
bool wifiTersambung();
bool supabaseSiap();
bool kirimDataKeAPI(HasilPH hasilPh, float tss);
bool kirimStatusErrorKeAPI(int modbusErrorCount);
void simpanKeBuffer(HasilPH hasilPh, float tss);
void cobaKirimUlangBuffer();
bool bacaModeOperasiDariSupabase();
KontrolManual bacaKontrolDariSupabase();

// Catat satu sample mentah (ph, suhu, mV) yang diambil selama proses kalibrasi/
// verifikasi ke tabel calibration_samples. "titik" = "ph4"|"ph7"|"ph10",
// "aksi" = "kalibrasi"|"verifikasi". Dipanggil per-sample, bukan per-siklus median.
bool kirimSampleKalibrasi(const char* titik, const char* aksi, int indexSample,
                           double ph, double suhu, double mv);

// Catat satu sample TSS (tss, suhu, ntu, raw) yang diambil selama kalibrasi/verifikasi
// ke tabel tss_calibration_samples. "standard" = "std_a"|"std_b"|"std_c",
// "aksi" = "kalibrasi"|"verifikasi". Dipanggil per-sample.
bool kirimSampleKalibrasiTSS(const char* standard, const char* aksi, int indexSample,
                              double tss, double suhu, double ntu, double rawValue);

#endif // NETWORK_H
