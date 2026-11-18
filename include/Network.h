#ifndef NETWORK_H
#define NETWORK_H

#include "Config.h"
#include "Gsm.h"
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
  float mv = NAN; // tegangan elektroda (mV), NAN bila tak ada
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
extern const char *TABLE_WATER_LEVEL;
extern const char *TABLE_MODE;
extern const char *TABLE_TSS_CAL_CONFIG;
extern const char *TABLE_TSS_HW_CAL_CMD;

// ========================= PERINTAH MANUAL (VALVE & POMPA)
// ========================= Dibaca dari baris TERBARU (order by updated_at
// desc) di TABLE_MODE. Web menulis baris baru setiap kali operator menekan
// toggle valve/pompa.
struct KontrolManual {
  bool ok;         // true jika berhasil diambil & di-parse dari Supabase
  long id;         // id baris (identity, selalu naik)
  String mode;     // "otomatis" | "manual"
  String valveCmd; // "buka" | "tutup"
  String pumpCmd;  // "jalan" | "berhenti"
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
bool internetSiap(); // WiFi ATAU data seluler (GSM)
bool kirimPosisiKeSupabase(float lat, float lon, float alt); // GNSS -> devices.latitude/longitude
bool jaringanSiap(); // sama dengan internetSiap()
bool supabaseSiap();
void tampilkanStatusWiFi();
void scanWiFiSekitar();
bool kirimDataKeAPI(HasilPH hasilPh, float tss);
bool kirimLevelKeAPI(float levelPercent, float distanceCm);
bool kirimStatusErrorKeAPI(int modbusErrorCount);
void simpanKeBuffer(HasilPH hasilPh, float tss);
void cobaKirimUlangBuffer();
bool bacaModeOperasiDariSupabase();
KontrolManual bacaKontrolDariSupabase();
bool kirimStatusPompaKeSupabase(bool pompaNyala, const char *catatan);
KalibrasiTSS bacaKalibrasiTSSDariSupabase();
bool kirimHeartbeatKeSupabase(const char *mode, const char *fase,
                              long sisaDetik, bool pompaNyala, bool valveBuka,
                              bool floatHigh);

// Kirim satu payload JSON ke tabel pembacaan sensor. kodeHttp = kode HTTP
// (atau -1 kalau WiFi mati). Dipakai antrean SD (SdQueue.cpp).
bool kirimPayloadMentah(const String &payload, int &kodeHttp);

// Alat Modbus dari web (tabel modbus_cmd): baca/tulis register sensor langsung
// dari dashboard, seperti Modbus Poll tapi lewat ESP32. Perintah 'tulis' hanya
// bisa dibuat lewat fungsi database modbus_tulis() yang memeriksa PIN.
struct PerintahModbus {
  bool ok;
  long id;
  int slaveId;
  String aksi;  // "baca" | "tulis"
  int alamat;
  int jumlah;   // jumlah register
  String nilai; // tulis: daftar nilai dipisah koma
  String tipe;  // "u16" | "float"
};
PerintahModbus bacaPerintahModbusDariSupabase(long idTerakhirDiproses);
bool laporkanHasilModbus(long id, bool sukses, const char *hasil,
                         const char *pesan);

// Jadwal siklus mode otomatis (tabel auto_schedule, diisi dashboard web):
// tiap interval_menit satu siklus isi->ukur->buang dimulai; pompa dan valve
// masing-masing dibatasi maksimal pompa_menit / valve_menit.
struct JadwalOtomatis {
  bool ok;
  int intervalDetik; // jeda antar awal siklus
  int pompaDetik;    // batas waktu pompa nyala
  int valveDetik;    // batas waktu valve buka
  int bacaSensorDetik; // interval baca+kirim pH/TSS (mode manual / live)
  int stabilDetik;     // tunggu stabil sebelum baca sensor di siklus otomatis
  int sampelMs;        // jeda antar sampel selama jendela baca (ms)
  int trimPersen;      // % data dibuang di tiap sisi (trimmed mean)
};
JadwalOtomatis bacaJadwalOtomatisDariSupabase();
// devices.min_air_cm & max_air_cm (batas tinggi air kolam dari dashboard); ada* = kolom berisi angka.
bool bacaBatasAirDariSupabase(float &minCm, bool &adaMin, float &maxCm, bool &adaMax);

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

// Perintah kalibrasi/verifikasi pH dari dashboard web (tabel ph_calibration_cmd,
// baris status=pending dengan id > terakhir diproses). perintah:
// "kalibrasi_ph4" | "kalibrasi_ph10" | "verifikasi_ph4" | "verifikasi_ph7" |
// "verifikasi_ph10" | "reset".
struct PerintahKalibrasiPh {
  bool ok;
  long id;
  String perintah;
};
PerintahKalibrasiPh bacaPerintahKalibrasiPhDariSupabase(long idTerakhirDiproses);
bool laporkanHasilKalibrasiPh(long id, bool sukses, const char *pesan);
// Progres sementara: isi kolom pesan, status tetap "pending" (dilihat dashboard
// selama alat menunggu pembacaan stabil).
bool laporkanProgresKalibrasiPh(long id, const char *pesan);

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
