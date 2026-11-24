// Lane "Mode Manual" (state_machine.workflow.json):
//   Poll DB -> Aman? -> Eksekusi -> Kirim Hasil -> (kembali ke Poll DB)
#include "Modes.h"
#include "Pompa.h"

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
// ---------------- Baca & Eksekusi Command Manual (Valve & Pompa via Supabase)
// ----------------
static KontrolManual perintahTerakhir;
// id baris command yang sudah ada saat boot (-2 = belum diketahui). Selama
// command aktif masih baris ini, aktuator ditahan (lihat terimaKontrolAwal).
static long idCommandBoot = -2;
static bool tahanAktuatorBoot = true;

// Pengaman pompa manual (lihat POMPA_MANUAL_* di Config.h).
static unsigned long pompaManualMulai = 0;   // 0 = pompa sedang mati
static unsigned long pollSuksesTerakhir = 0; // millis() fetch command terakhir yang berhasil
static bool kunciPengamanPompa = false;      // true = pompa terkunci OFF sampai command diulang

static void periksaPengamanPompaManual() {
  if (!pumpState) {
    pompaManualMulai = 0;
    return;
  }
  if (pompaManualMulai == 0)
    pompaManualMulai = millis();
  const char *alasan = nullptr;
  if (millis() - pompaManualMulai >= POMPA_MANUAL_MAKS_MS)
    alasan = "melewati batas waktu nyala manual";
  else if (millis() - pollSuksesTerakhir >= POMPA_MANUAL_PUTUS_MS)
    alasan = "command web tidak terbaca (komunikasi putus)";
  if (alasan) {
    pompaOff();
    kunciPengamanPompa = true;
    pompaManualMulai = 0;
    Serial.printf("[PENGAMAN] Pompa dimatikan: %s. Kirim 'berhenti' lalu 'jalan' "
                  "dari web untuk menyalakan lagi.\n",
                  alasan);
  }
}

void handleModeManual() {
  periksaPengamanPompaManual();
  // KUNCI MUTLAK: float switch HIGH -> pompa langsung dimatikan SEKARANG,
  // tidak menunggu siklus poll perintah (INTERVAL_POLL_MS). Selama float
  // HIGH, pompa tetap terkunci OFF walau command web = "jalan".
  if (pumpState && bacaFloatSwitch()) {
    pompaOff();
    Serial.println("[INTERLOCK] Float switch HIGH -> pompa dimatikan & dikunci OFF");
  }
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
      siklusPertama = true; // dipindah ke otomatis dari web = siklus langsung mulai
      state = STATE_A_BACA_MODE;
      return;
    }
    jalankanPerintahPompaValve();
  }
}

// Dipanggil dari STATE_A_BACA_MODE dengan data yang SUDAH di-fetch di sana --
// langsung eksekusi tanpa fetch ulang, dan reset timer poll supaya siklus
// poll berikutnya menunggu penuh INTERVAL_POLL_MS (bukan langsung fetch lagi).
//
// SAFETY BOOT: command yang SUDAH ada di Supabase saat boot TIDAK dieksekusi
// (aktuator diam, pompa dianggap berhenti). Baru bergerak saat ada command
// BARU (id baris lebih besar) dari dashboard. Boot hanya untuk WiFi/jaringan.
void terimaKontrolAwal(KontrolManual k) {
  if (!k.ok)
    return;
  perintahTerakhir = k;
  idCommandBoot = k.id;
  timerPollTerakhir = millis();
  pollSuksesTerakhir = millis();
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
  pollSuksesTerakhir = millis();
  return true;
}

// Interlock pompa (cegah pompa nyala saat chamber penuh) AKTIF di mode manual:
// dipakai di jalankanPerintahPompaValve(). Valve tetap tanpa syarat.
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
  if (idCommandBoot == -2)
    idCommandBoot = perintahTerakhir.id; // fetch awal gagal: command pertama = baseline
  if (perintahTerakhir.id != idCommandBoot)
    tahanAktuatorBoot = false; // ada command baru dari dashboard
  if (!tahanAktuatorBoot && perintahTerakhir.valveCmd != valveTerakhirDieksekusi) {
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
  String pumpEfektif = tahanAktuatorBoot ? "berhenti" : perintahTerakhir.pumpCmd;
  // Kunci pengaman dilepas hanya setelah command web bukan "jalan" (operator
  // menekan berhenti), supaya pompa tidak menyala lagi sendiri.
  if (perintahTerakhir.pumpCmd != "jalan")
    kunciPengamanPompa = false;
  if (kunciPengamanPompa)
    pumpEfektif = "berhenti";
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
  // Backoff: kalau kirim gagal (HTTPS putus-putus), jangan coba tiap poll --
  // satu percobaan gagal bisa menahan loop sampai ~8 detik dan menunda
  // eksekusi perintah pompa berikutnya.
  static unsigned long percobaanTerakhir = 0;
  static bool pernahMencoba = false;
  bool bolehMencoba =
      !pernahMencoba || millis() - percobaanTerakhir >= 10000UL;
  if ((laporanSekarang != laporanTerakhir || butuhHeartbeat) && bolehMencoba) {
    const char *catatan = laporanSekarang == 2   ? "ditolak_chamber_penuh"
                          : laporanSekarang == 1 ? "jalan"
                                                 : "berhenti";
    pernahMencoba = true;
    percobaanTerakhir = millis();
    if (kirimStatusPompaKeSupabase(pumpState, catatan)) {
      laporanTerakhir = laporanSekarang;
      heartbeatTerakhir = millis();
    }
  }
}
