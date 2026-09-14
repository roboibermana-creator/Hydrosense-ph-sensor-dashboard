#ifndef MODES_H
#define MODES_H

#include <Arduino.h>
#include "Network.h"

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
extern unsigned long timerStabilisasiMulai;
extern unsigned long timerPollTerakhir;
extern unsigned long timerRetryWifiTerakhir;
extern int jumlahUlangBaca;
extern HasilPH hasilPhTerakhir;
extern float tssTerakhir;
extern bool modeOtomatis;

// Konstanta Parameter (di-define di main.cpp)
extern const float TINGGI_PASANG_CM;
extern const float AMBANG_TINGGI_AIR_CM;
extern const unsigned long TIMEOUT_ISI_MS;
extern const unsigned long DELAY_STABIL_MS;
extern const int MAKS_ULANG_BACA;
extern const unsigned long INTERVAL_POLL_MS;
extern const unsigned long INTERVAL_RETRY_WIFI_MS;

// Handler Function
void handleModeOtomatis();
void handleModeManual();
bool permintaanBerhenti();

// ---------------- Aktuator & Sensor (Implementasi di main.cpp) ----------------
void pompaOn();
void pompaOff();
void valveBuka();
void valveTutup();

float bacaJarakAir();
bool bacaFloatSwitch();
HasilPH bacaProsesPH();
float bacaRataRataTSS(int nSample);

// ---------------- Network & Helper ----------------
void tampilkanError(const char* pesan);
bool kirimDataKeAPI(HasilPH hasilPh, float tss);
bool kirimStatusErrorKeAPI(int errorCount);
void simpanKeBuffer(HasilPH hasilPh, float tss);

// ---------------- Fungsi Dummy Mode Manual ----------------
bool bacaPerintahManual();
bool interlockAman();
void tolakPerintahDenganAlert();
void jalankanPerintahPompaValve();

#endif // MODES_H
