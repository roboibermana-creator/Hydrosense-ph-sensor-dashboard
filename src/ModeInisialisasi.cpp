// Lane "Inisialisasi" (state_machine.workflow.json):
//   Boot -> Cek Koneksi -> Cek Stop -> Mode? -> (Otomatis | Manual | Stopped)
#include "App.h"

static bool wifiSudahDilaporkan =
    false; // supaya "WiFi tersambung!" cuma dicetak sekali

void kalibrasiAwal() {
  // Kalibrasi awal jika perlu
}

bool stateInisialisasi(SystemState s) {
  switch (s) {
  case STATE_INIT:
  case STATE_CEK_KONEKSI:
  case STATE_A_CEK_STOP:
  case STATE_A_BACA_MODE:
  case STATE_STOPPED:
    return true;
  default:
    return false;
  }
}

void handleInisialisasi() {
  switch (state) {
  case STATE_INIT:
    kalibrasiAwal();
#if PAKAI_WIFI
    connectWiFi();
#endif
#if PAKAI_WIFI || PAKAI_GSM
    state = STATE_CEK_KONEKSI;
#else
    Serial.println(F("[WIFI] DIMATIKAN (PAKAI_WIFI 0, PAKAI_GSM 0): node tanpa internet."));
    state = STATE_A_CEK_STOP;
#endif
    break;
  case STATE_CEK_KONEKSI:
    // Throttle: jangan panggil supabaseSiap() (HTTP request) tiap iterasi
    // loop(), cukup tiap INTERVAL_RETRY_WIFI_MS -- supaya loop() tetap
    // responsif membaca command Serial selagi menunggu koneksi.
    if (millis() - timerRetryWifiTerakhir >= INTERVAL_RETRY_WIFI_MS) {
      timerRetryWifiTerakhir = millis();
      if (!internetSiap()) {
#if PAKAI_WIFI
        // JANGAN panggil connectWiFi() di sini: WiFi.begin() tiap 3 dtk
        // memotong proses auth yang sedang berjalan (hotspot butuh >3 dtk)
        // sehingga tidak pernah tersambung. Retry ditangani watchdog
        // (Network.cpp, backoff 10-60 dtk) + auto-reconnect.
        Serial.printf("[WIFI] Belum tersambung (status=%d), menunggu...\n",
                      (int)WiFi.status());
#else
        Serial.printf("[GSM] Menunggu data seluler: %s\n", gsmStatusTeks());
#endif
      } else if (!wifiSudahDilaporkan) {
        if (wifiTersambung()) {
          Serial.print("[WIFI] Tersambung! IP: ");
          Serial.println(WiFi.localIP());
        } else {
          Serial.printf("[GSM] Data seluler siap (sinyal %d dBm)\n", gsmRssiDbm());
        }
        wifiSudahDilaporkan = true;
      } else if (supabaseSiap()) {
        digitalWrite(PIN_LED_ERROR, LOW);
        // Paksa ambil faktor kalibrasi TSS sekarang (bukan nunggu throttle 5
        // menit) supaya begitu online, kalibrasi terbaru dari web langsung
        // dipakai.
        timerCekKalibrasiTssTerakhir = 0;
        state = STATE_A_CEK_STOP;
      } else {
        tampilkanError("Internet tersambung tapi Supabase belum siap");
      }
    }
    break;
  case STATE_A_CEK_STOP:
    state = permintaanBerhenti() ? STATE_STOPPED : STATE_A_BACA_MODE;
    break;
  case STATE_A_BACA_MODE: {
#if !PAKAI_WIFI && !PAKAI_GSM
    // Tanpa internet mode tidak bisa dibaca dari Supabase. Boot selalu MANUAL
    // (aman, pompa/valve diam).
    static bool bootAwal = true;
    if (bootAwal) {
      bootAwal = false;
      modeOtomatis = false;
    }
    state = modeOtomatis ? STATE_AUTO_BACA_LEVEL : STATE_M_BACA_PERINTAH;
    break;
#endif
    // Satu fetch dipakai buat dua hal (cek mode DAN, kalau manual,
    // langsung eksekusi command-nya) -- sebelumnya ini 2 HTTP request
    // terpisah balapan minta data yang sama, buang-buang waktu TLS handshake.
    KontrolManual k = bacaKontrolDariSupabase();
#if FORCE_MODE_MANUAL
    modeOtomatis = false; // dikunci manual (lihat Config.h)
#else
    // Fetch gagal -> tetap MANUAL (diam). Dulu gagal = otomatis, sehingga
    // gangguan jaringan sesaat bisa menyalakan siklus otomatis sendiri.
    modeOtomatis = k.ok && k.mode == "otomatis";
#endif
    if (!modeOtomatis) {
      terimaKontrolAwal(k); // eksekusi langsung, tanpa fetch ulang
    }
    state = modeOtomatis ? STATE_AUTO_BACA_LEVEL : STATE_M_BACA_PERINTAH;
    break;
  }
  case STATE_STOPPED:
    pompaOff();
    valveTutup();
    break;
  default:
    break;
  }
}
