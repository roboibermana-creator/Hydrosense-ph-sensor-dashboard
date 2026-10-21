#include "Modes.h"
#include "Pompa.h"
#include <Preferences.h>

// Mode manual (dipicu dari web via kolom "mode" di Supabase): relay HARUS
// langsung nurut command terbaru secepat mungkin, TANPA syarat/interlock
// dan TANPA menunggu apapun -- itu permintaan eksplisit.
//
// PENTING: logging data pH/TSS ke Supabase SENGAJA TIDAK dijalankan lagi di
// sini. Sempat dicoba dipisah ke timer sendiri (tiap 30s), tapi karena
// loop() ini single-threaded, pembacaan pH+TSS tetap BLOCKING tiap kali
// jalan -- dan kalau sensor pH/TSS belum terpasang fisik, itu bisa macet
// 20-30+ detik (timeout Modbus + retry berkali-kali), yang selama itu
// perintah relay dari web ikut ketunda. Sampai ada solusi non-blocking
// (mis. task FreeRTOS terpisah), command relay diprioritaskan mutlak dan
// logging data di mode manual dilewati dulu.
static String valveTerakhirDieksekusi = "";
static String pumpTerakhirDieksekusi = "";
// Wiper TSS itu aksi momentary (sekali sapu), bukan status level seperti
// valve/pompa, jadi dilacak pakai id baris (bukan teks command) supaya tiap
// penekanan tombol baru di web (baris baru di Supabase) tetap memicu sapuan
// meski teksnya sama ("bersihkan").
//
// Disimpan di NVS (Preferences), BUKAN cuma variabel RAM -- device ini pernah
// kedapatan reboot berulang (brownout/watchdog, lihat "[SISTEM] Alasan reset
// terakhir" di Serial Monitor) yang sebelumnya bikin wiper ke-trigger ULANG
// tiap reboot karena command lama di Supabase masih "bersihkan" dan pelacak
// RAM-nya ikut ke-reset ke -1.
static Preferences wiperPrefs;
static long wiperIdTerakhirDieksekusi = -1;
static bool wiperPrefsSiap = false;

static void pastikanWiperPrefsSiap() {
  if (wiperPrefsSiap)
    return;
  wiperPrefs.begin("tss-wiper", false);
  wiperIdTerakhirDieksekusi = wiperPrefs.getLong("last_id", -1);
  wiperPrefsSiap = true;
}

// ---------------- Baca & Eksekusi Command Manual (Valve & Pompa via Supabase)
// ----------------
static KontrolManual perintahTerakhir;

void handleModeManual() {
  // Cek & eksekusi command SECEPAT mungkin (throttle hanya oleh
  // INTERVAL_POLL_MS di dalam bacaPerintahManual(), tidak ada gerbang lain).
  if (bacaPerintahManual()) {
    // Sebelum perbaikan ini, mode "otomatis" dari web TIDAK PERNAH dicek di
    // sini -- cekOverrideManualDariWeb() di main.cpp cuma nangani arah
    // otomatis->manual, jadi begitu masuk manual, device macet permanen di
    // manual sampai reboot walau kolom "mode" di Supabase sudah balik ke
    // "otomatis" (logging pH/TSS pun ikut berhenti selamanya, lihat catatan
    // di atas). Sekarang dicek simetris: begitu web bilang "otomatis" lagi,
    // langsung lepas dari manual dan balik ke siklus otomatis.
    if (!FORCE_MODE_MANUAL && perintahTerakhir.mode == "otomatis") {
      Serial.println(F("\n[MODE] Mode OTOMATIS terdeteksi dari web -> kembali "
                       "ke siklus otomatis."));
      modeOtomatis = true;
      state = STATE_A_BACA_MODE;
      return;
    }
    jalankanPerintahPompaValve();
  }
}

// Dipanggil dari STATE_A_BACA_MODE dengan data yang SUDAH di-fetch di sana --
// langsung eksekusi tanpa fetch ulang, dan reset timer poll supaya siklus
// poll berikutnya menunggu penuh INTERVAL_POLL_MS (bukan langsung fetch lagi).
void terimaKontrolAwal(KontrolManual k) {
  if (!k.ok)
    return;
  perintahTerakhir = k;
  timerPollTerakhir = millis();
  jalankanPerintahPompaValve();
}

bool bacaPerintahManual() {
  // Throttle maksimal INTERVAL_POLL_MS supaya tidak spam HTTP tiap iterasi
  // loop().
  if (millis() - timerPollTerakhir < INTERVAL_POLL_MS)
    return false;
  timerPollTerakhir = millis();

  KontrolManual k = bacaKontrolDariSupabase();
  if (!k.ok)
    return false;

  perintahTerakhir = k;
  return true;
}

// Interlock pompa (cegah pompa nyala saat chamber penuh) AKTIF di mode manual:
// dipakai di jalankanPerintahPompaValve(). Valve dan wiper tetap tanpa syarat.
bool interlockAman() {
  if (perintahTerakhir.pumpCmd == "jalan" && bacaFloatSwitch()) {
    return false;
  }
  return true;
}

void tolakPerintahDenganAlert() {
  tampilkanError(
      "Perintah manual ditolak - interlock tidak aman (chamber sudah penuh)");
}

void jalankanPerintahPompaValve() {
  // Hanya aktuasi relay saat command benar-benar berubah (hindari re-pulse
  // relay tiap poll). valveTerakhirDieksekusi CUMA di-update kalau aksinya
  // BENERAN jalan (bukan diabaikan debounce) -- kalau tidak, command yang gagal
  // dieksekusi akan dianggap "sudah dijalankan" dan tidak pernah dicoba ulang.
  if (perintahTerakhir.valveCmd != valveTerakhirDieksekusi) {
    bool berhasil =
        (perintahTerakhir.valveCmd == "buka") ? valveBuka() : valveTutup();
    if (berhasil)
      valveTerakhirDieksekusi = perintahTerakhir.valveCmd;
  }

  // Interlock pompa: command "jalan" ditolak (dan pompa dimatikan bila sedang
  // jalan) selama float switch menandakan chamber penuh. Dievaluasi tiap poll,
  // jadi pompa juga berhenti kalau chamber penuh saat sedang berjalan, dan
  // otomatis jalan lagi kalau command masih "jalan" dan chamber sudah turun.
  static bool interlockSedangMenolak = false;
  String pumpEfektif = perintahTerakhir.pumpCmd;
  if (INTERLOCK_POMPA_MANUAL && pumpEfektif == "jalan" && !interlockAman()) {
    pumpEfektif = "berhenti";
    if (!interlockSedangMenolak) {
      Serial.printf("[INTERLOCK] Pompa DITOLAK: float switch (GPIO%d) = HIGH "
                    "(dianggap chamber penuh). Kalau float switch belum "
                    "terpasang, pin mengambang ke HIGH lewat INPUT_PULLUP.\n",
                    PIN_FLOAT_SWITCH);
      tolakPerintahDenganAlert();
      interlockSedangMenolak = true;
    }
  } else {
    interlockSedangMenolak = false;
  }

  // Bandingkan ke status relay SEBENARNYA (pumpState), bukan cuma command
  // terakhir: mode otomatis / override / STATE_STOPPED juga bisa mematikan
  // pompa, sehingga pelacak command jadi basi dan "jalan" tidak pernah
  // dieksekusi ulang saat kembali ke manual.
  bool pumpHarusOn = (pumpEfektif == "jalan");
  if (pumpHarusOn != pumpState) {
    if (pumpHarusOn)
      pompaOn();
    else
      pompaOff();
  }
  pumpTerakhirDieksekusi = pumpEfektif;

  // Laporkan status asli pompa ke dashboard hanya saat berubah (diulang tiap
  // poll kalau kirim gagal).
  static int laporanTerakhir = -1; // 0 = berhenti, 1 = jalan, 2 = ditolak
  int laporanSekarang = interlockSedangMenolak ? 2 : (pumpState ? 1 : 0);
  // Heartbeat tiap 30 detik walau status tidak berubah, supaya dashboard tahu
  // alat masih online (di mode manual data pH/TSS tidak dikirim).
  static unsigned long heartbeatTerakhir = 0;
  bool butuhHeartbeat = millis() - heartbeatTerakhir >= 30000UL;
  if (laporanSekarang != laporanTerakhir || butuhHeartbeat) {
    const char *catatan = laporanSekarang == 2   ? "ditolak_chamber_penuh"
                          : laporanSekarang == 1 ? "jalan"
                                                 : "berhenti";
    if (kirimStatusPompaKeSupabase(pumpState, catatan)) {
      laporanTerakhir = laporanSekarang;
      heartbeatTerakhir = millis();
    }
  }

  pastikanWiperPrefsSiap();
  if (perintahTerakhir.wiperCmd == "bersihkan" &&
      perintahTerakhir.id != wiperIdTerakhirDieksekusi) {
    wiperMulai();
    wiperIdTerakhirDieksekusi = perintahTerakhir.id;
    wiperPrefs.putLong("last_id", wiperIdTerakhirDieksekusi);
  }
}
