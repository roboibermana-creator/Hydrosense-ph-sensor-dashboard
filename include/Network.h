#ifndef NETWORK_H
#define NETWORK_H

#include "Config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

// ========================= HASIL PROSES pH =========================
struct HasilPH {
  bool valid;      // lolos gerbang validitas & cukup sample sukses?
  float ph;        // sudah median + dibulatkan 2 desimal
  float suhu;      // dibulatkan 1 desimal
  float sd;        // standar deviasi sample (pH)
  float drift;     // pH per menit
  String kualitas; // "good" | "fair" | "unstable" | "error"
  bool compliant;  // baku mutu 6-9 DAN kualitas good/fair
  uint16_t rawPh;
  int16_t rawTemp;
  int modbusErrorCount;
};

// ========================= BUFFER OFFLINE =========================
struct DataSample {
  float ph, suhu, tss, tssRaw;
  String kualitas;
  bool compliant;
  uint16_t rawPh;
  int16_t rawTemp;
  float sd, drift;
  int modbusErrorCount;
  bool phValid; // false -> ini record status error (ph tidak dikirim sbg angka)
  bool terisi;
};

extern const int UKURAN_BUFFER;
extern DataSample buffer[];
extern int bufferHead;

// Konfigurasi API
extern const char *SUPABASE_URL;
extern const char *SUPABASE_API_KEY;
extern const char *TABLE_READINGS;
extern const char *TABLE_MODE;
extern const char *TABLE_TSS_CAL_CONFIG;
extern const char *TABLE_TSS_HW_CAL_CMD;

// ========================= PERINTAH MANUAL (VALVE & POMPA)
// ========================= Dibaca dari baris TERBARU (order by updated_at
// desc) di TABLE_MODE. Web menulis baris baru setiap kali operator menekan
// toggle valve/pompa.
struct KontrolManual {
  bool ok;         // true jika berhasil diambil & di-parse dari Supabase
  long id;         // id baris (identity, selalu naik) -- dipakai untuk deteksi
                   // command momentary (wiper) yang butuh trigger ulang walau
                   // teksnya sama dengan command sebelumnya
  String mode;     // "otomatis" | "manual"
  String valveCmd; // "buka" | "tutup"
  String pumpCmd;  // "jalan" | "berhenti"
  String wiperCmd; // "bersihkan" | "diam" -- momentary, dipicu sekali per id
                   // baris baru
};

// ========================= KALIBRASI TSS (SLOPE & OFFSET) =========================
// Faktor regresi (Y = slope*X + offset, dari least squares gravimetri vs
// raw sensor) disimpan di tabel tss_calibration_config, diisi dashboard web
// (hitung otomatis dari tss_calibration_samples, atau input manual). Device
// membaca baris TERBARU (order by id desc) dan menerapkannya via
// tssSensor.setCalibration().
struct KalibrasiTSS {
  bool ok; // true jika berhasil diambil & di-parse dari Supabase
  float slope;
  float offset;
};

void connectWiFi();
bool wifiTersambung();
bool supabaseSiap();
void tampilkanStatusWiFi();
bool kirimDataKeAPI(HasilPH hasilPh, float tss, float tssRaw);
bool kirimStatusErrorKeAPI(int modbusErrorCount);
void simpanKeBuffer(HasilPH hasilPh, float tss, float tssRaw);
void cobaKirimUlangBuffer();
bool bacaModeOperasiDariSupabase();
KontrolManual bacaKontrolDariSupabase();
KalibrasiTSS bacaKalibrasiTSSDariSupabase();

// ========================= KALIBRASI HARDWARE TSS (two-point native) =========
// Perintah step-by-step dari dashboard (menggantikan Modbus Poll manual --
// lihat Panduan Kalibrasi Sensor TSS). Device polling tabel
// tss_hw_calibration_cmd, eksekusi begitu ada baris BARU (id > terakhir
// diproses), lalu lapor balik hasilnya via PATCH ke baris yang sama.
struct PerintahKalibrasiTssHw {
  bool ok;   // true jika berhasil diambil & baris ini BELUM pernah diproses
  long id;
  String perintah;    // "cek_status" | "siapkan" | "set_titik1" |
                       // "tulis_titik1" | "set_titik2" | "tulis_titik2" |
                       // "verifikasi_keluar" | "reset_factor"
  float nilaiTarget;   // nilai Partech (P) -- dipakai tulis_titik1/2
  bool adaNilaiAktual; // true kalau operator override manual nilai S
  float nilaiAktual;   // nilai sensor ini (S) -- kalau tidak ada, device
                       // pakai raw TSS live (readTSS())
};

PerintahKalibrasiTssHw
bacaPerintahKalibrasiTssHwDariSupabase(long idTerakhirDiproses);
bool laporkanHasilKalibrasiTssHw(long id, bool sukses, const char *pesan,
                                 int16_t reg13, int16_t reg14);

// Catat satu sample mentah (ph, suhu, mV) yang diambil selama proses kalibrasi/
// verifikasi ke tabel calibration_samples. "titik" = "ph4"|"ph7"|"ph10",
// "aksi" = "kalibrasi"|"verifikasi". Dipanggil per-sample, bukan per-siklus
// median.
bool kirimSampleKalibrasi(const char *titik, const char *aksi, int indexSample,
                          double ph, double suhu, double mv);

// Catat satu sample TSS (tss, suhu, ntu, raw) yang diambil selama
// kalibrasi/verifikasi ke tabel tss_calibration_samples. "standard" =
// "std_a"|"std_b"|"std_c", "aksi" = "kalibrasi"|"verifikasi". Dipanggil
// per-sample.
bool kirimSampleKalibrasiTSS(const char *standard, const char *aksi,
                             int indexSample, double tss, double suhu,
                             double ntu, double rawValue);

#endif // NETWORK_H
