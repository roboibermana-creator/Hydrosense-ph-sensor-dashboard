#include "Modes.h"

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
static long wiperIdTerakhirDieksekusi = -1;

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
    if (perintahTerakhir.mode == "otomatis") {
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

// Interlock (cegah pompa nyala saat chamber penuh) SENGAJA TIDAK dipakai lagi
// di mode manual -- sesuai permintaan: command dari web langsung dieksekusi
// tanpa syarat. Fungsi ini dibiarkan ada (dipanggil dari luar bila suatu saat
// interlock manual mau diaktifkan lagi), tapi tidak lagi bagian dari alur
// utama.
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

  if (perintahTerakhir.pumpCmd != pumpTerakhirDieksekusi) {
    if (perintahTerakhir.pumpCmd == "jalan")
      pompaOn();
    else
      pompaOff();
    pumpTerakhirDieksekusi = perintahTerakhir.pumpCmd;
  }

  if (perintahTerakhir.wiperCmd == "bersihkan" &&
      perintahTerakhir.id != wiperIdTerakhirDieksekusi) {
    wiperMulai();
    wiperIdTerakhirDieksekusi = perintahTerakhir.id;
  }
}
