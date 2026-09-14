#include "Modes.h"

void handleModeManual() {
  switch (state) {
    case STATE_M_BACA_PERINTAH:
      if (bacaPerintahManual()) {
        state = STATE_M_CEK_INTERLOCK;
      } else if (millis() - timerPollTerakhir >= INTERVAL_POLL_MS) {
        timerPollTerakhir = millis();
        state = STATE_A_CEK_STOP;
      }
      break;

    case STATE_M_CEK_INTERLOCK:
      if (interlockAman()) {
        state = STATE_M_JALANKAN_PERINTAH;
      } else {
        tolakPerintahDenganAlert();
        state = STATE_A_CEK_STOP;
      }
      break;

    case STATE_M_JALANKAN_PERINTAH:
      jalankanPerintahPompaValve();
      state = STATE_M_KIRIM_DATA;
      break;

    case STATE_M_KIRIM_DATA:
      hasilPhTerakhir = bacaProsesPH();
      tssTerakhir     = bacaRataRataTSS(5); // MEDIAN_SAMPLES = 5
      if (hasilPhTerakhir.valid) {
        if (!kirimDataKeAPI(hasilPhTerakhir, tssTerakhir)) {
          simpanKeBuffer(hasilPhTerakhir, tssTerakhir);
        }
      } else {
        kirimStatusErrorKeAPI(hasilPhTerakhir.modbusErrorCount);
      }
      state = STATE_M_BACA_PERINTAH;
      break;

    default:
      // State lainnya bukan milik manual
      break;
  }
}

// ---------------- Implementasi Perintah Manual (Valve & Pompa via Supabase) ----------------
static KontrolManual perintahTerakhir;

bool bacaPerintahManual() {
  // Poll Supabase maksimal setiap INTERVAL_POLL_MS, jangan setiap iterasi loop()
  if (millis() - timerPollTerakhir < INTERVAL_POLL_MS) return false;
  timerPollTerakhir = millis();

  KontrolManual k = bacaKontrolDariSupabase();
  if (!k.ok) return false;

  perintahTerakhir = k;
  return true;
}

bool interlockAman() {
  // Cegah pompa dinyalakan manual kalau chamber sudah penuh (float switch HIGH) -> hindari overflow
  if (perintahTerakhir.pumpCmd == "jalan" && bacaFloatSwitch()) {
    return false;
  }
  return true;
}

void tolakPerintahDenganAlert() {
  tampilkanError("Perintah manual ditolak - interlock tidak aman (chamber sudah penuh)");
}

void jalankanPerintahPompaValve() {
  // Hanya aktuasi relay saat command benar-benar berubah (hindari re-pulse relay tiap poll)
  static String valveTerakhirDieksekusi = "";
  static String pumpTerakhirDieksekusi  = "";

  if (perintahTerakhir.valveCmd != valveTerakhirDieksekusi) {
    if (perintahTerakhir.valveCmd == "buka") valveBuka();
    else                                     valveTutup();
    valveTerakhirDieksekusi = perintahTerakhir.valveCmd;
  }

  if (perintahTerakhir.pumpCmd != pumpTerakhirDieksekusi) {
    if (perintahTerakhir.pumpCmd == "jalan") pompaOn();
    else                                     pompaOff();
    pumpTerakhirDieksekusi = perintahTerakhir.pumpCmd;
  }
}

