#ifndef MODES_H
#define MODES_H

#include "Network.h"
#include <Arduino.h>

// ========================= STATE MACHINE =========================
enum SystemState {
  STATE_INIT,
  STATE_CEK_KONEKSI,
  STATE_A_CEK_STOP,
  STATE_A_BACA_MODE,

  STATE_TUNGGU_AIR,
  STATE_TUNGGU_FLOAT_SWITCH,
  STATE_AUTO_BACA_LEVEL,
  STATE_AUTO_ISI_CHAMBER,
  STATE_AUTO_STABILISASI,
  STATE_AUTO_BACA_KUALITAS,
  STATE_AUTO_VALIDASI,
  STATE_AUTO_KIRIM_DATA,
  STATE_AUTO_TUNGGU_KOSONG,

  STATE_M_BACA_PERINTAH,
  STATE_M_CEK_INTERLOCK,
  STATE_M_JALANKAN_PERINTAH,
  STATE_M_KIRIM_DATA,

  STATE_STOPPED
};

// Global State Variables (di-define di main.cpp)
extern SystemState state;
extern unsigned long timerIsiChamberMulai;
// Jadwal siklus otomatis (diatur dari web, lihat WebSync.cpp). Default dipakai
// sampai jadwal pertama berhasil diambil dari Supabase.
extern unsigned long jedaSiklusMs;   // jeda antar awal siklus
extern unsigned long pompaMaksMs;    // batas waktu pompa isi chamber
extern unsigned long valveMaksMs;    // batas waktu valve buang
extern unsigned long waktuSiklusMulai;
extern bool siklusPertama;           // true = belum ada siklus sejak boot
extern unsigned long timerKurasMulai;
extern unsigned long intervalSampelMs; // jeda antar sampel di jendela baca
extern int trimPersen;                 // % dibuang tiap sisi (trimmed mean)
extern unsigned long intervalBacaSensorMs; // interval baca+kirim pH/TSS (manual)
extern unsigned long stabilMs;             // tunggu stabil sebelum baca (otomatis)
extern unsigned long timerStabilisasiMulai;
extern unsigned long timerPollTerakhir;
extern unsigned long timerRetryWifiTerakhir;
extern int jumlahUlangBaca;
extern HasilPH hasilPhTerakhir;
extern float tssTerakhir;
extern float tssRawTerakhir;
extern float suhuTssTerakhir; // suhu dari sensor TSS (NAN kalau belum terbaca)
extern float ntuTerakhir;    // register 6-7 sensor TSS (dugaan NTU), NAN kalau belum terbaca
extern bool modeOtomatis;

// Konstanta Parameter (di-define di main.cpp)
extern const float TINGGI_PASANG_CM;
extern const float AMBANG_TINGGI_AIR_CM;
extern const unsigned long TIMEOUT_ISI_MS;
extern const unsigned long DELAY_STABIL_MS;
extern const int MAKS_ULANG_BACA;
extern const unsigned long INTERVAL_POLL_MS;
extern const unsigned long INTERVAL_RETRY_WIFI_MS;

// Handler per lane (state_machine.workflow.json), dipanggil dari taskKontrol
bool stateInisialisasi(SystemState s); // true = state milik lane Inisialisasi
void handleInisialisasi();   // ModeInisialisasi.cpp
void handleModeOtomatis();   // ModeOtomatis.cpp
void handleKurasOtomatis();  // ModeKuras.cpp
void handleModeManual();     // ModeManual.cpp
bool permintaanBerhenti();

// ---------------- Aktuator & Sensor (Implementasi di main.cpp)
// ----------------
void pompaOn();
void pompaOff();
bool valveBuka();  // false kalau diabaikan (debounce, motor masih jalan)
bool valveTutup(); // false kalau diabaikan (debounce, motor masih jalan)
bool valveTerbuka(); // status valve terakhir yang diperintahkan (untuk dashboard)
void valveUpdate(); // matikan relay valve setelah VALVE_TRAVEL_MS (panggil rutin)

// Mutex bus Modbus RS485 (Hardware.cpp). Wajib dipegang setiap baca/tulis
// sensor pH/TSS karena beberapa task berbagi Serial2.
bool kunciModbus(uint32_t timeoutMs);
void lepasModbus();

float bacaJarakAir();
bool bacaFloatSwitch();
HasilPH bacaProsesPH();
float bacaRataRataTSS(int nSample, float *rawOut = nullptr);
// Trimmed mean generik (urut naik di tempat, buang `frac` tiap sisi).
float trimmedMean(float *data, int n, float frac);
// Trimmed mean (buang `frac` terendah & tertinggi, rata-rata sisanya) dari n
// sampel RAW TSS, lalu kalibrasi. rawOut = trimmed mean sebelum kalibrasi.
// `data` diurutkan di tempat.
float trimmedMeanTSS(float *data, int n, float frac, float *rawOut);

// ---------------- Network & Helper ----------------
void tampilkanError(const char *pesan);
bool kirimDataKeAPI(HasilPH hasilPh, float tss);
bool kirimStatusErrorKeAPI(int errorCount);
void simpanKeBuffer(HasilPH hasilPh, float tss);

// ---------------- Fungsi Dummy Mode Manual ----------------
bool bacaPerintahManual();
bool interlockAman();
void tolakPerintahDenganAlert();
void jalankanPerintahPompaValve();
void terimaKontrolAwal(
    KontrolManual k); // dipakai STATE_A_BACA_MODE supaya tidak fetch dobel

#endif // MODES_H
