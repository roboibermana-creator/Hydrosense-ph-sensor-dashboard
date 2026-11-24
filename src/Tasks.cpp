// Semua task FreeRTOS HydroSense. Pembagian:
//   taskKontrol  (core 1, prio 3) state machine + perintah web + aktuator
//   taskSerial   (core 1, prio 2) menu Serial Monitor
//   logManual    (core 0, prio 1) log pH+TSS di mode manual
//   kirimLevel   (core 0, prio 1) kirim level air tiap 5 detik
//   ota          (core 0, prio 2) lihat OTA.cpp
#include "Tasks.h"
#include "App.h"
#include "OTA.h"
#include "SdQueue.h"
#include "Kolam.h"
#include "Rain.h"

// ---------------- Kontrol: state machine utama ----------------
static void langkahKontrol() {
  // 0. Relay valve tidak dibiarkan diberi tegangan setelah motor sampai ujung.
  valveUpdate();
  // 1b. Override manual dari web -- lihat catatan di atas
  // cekOverrideManualDariWeb().
  cekOverrideManualDariWeb();
  // 1c-1g. Poller sekunder dari web (kalibrasi TSS/pH, jadwal, alat Modbus).
  // Tiap request HTTPS = handshake TLS baru (3-6 dtk). Kalau semuanya jalan
  // di satu putaran, perintah pompa/valve baru terbaca setelah 25-55 dtk.
  // Maka SATU poller per putaran, bergiliran; masing-masing tetap punya
  // throttle sendiri, jadi slot yang belum waktunya lewat tanpa biaya.
  static uint8_t giliran = 0;
  switch (giliran++ % 4) {
  case 0:
    // Wizard kalibrasi hardware TSS. Selalu dipanggil: perintah faktor sensor
    // (baca/tulis/uji) diizinkan; two-point lama ditolak selama flag mati.
    cekPerintahKalibrasiTssHwDariWeb();
    break;
  case 1:
    cekPerintahKalibrasiPhDariWeb();
    break;
  case 2:
    cekPerintahModbusDariWeb();
    break;
  default:
    cekKalibrasiTSSDariWeb();
    cekJadwalOtomatisDariWeb();
    cekMinAirDariWeb();
    cekPerintahOtaDariWeb();
    break;
  }
  // 2. Jalankan State Machine -- dibagi per lane sesuai
  //    state_machine.workflow.json
  if (stateInisialisasi(state)) {
    handleInisialisasi();
  } else if (state == STATE_AUTO_TUNGGU_KOSONG) {
    handleKurasOtomatis();
  } else if (modeOtomatis) {
    handleModeOtomatis();
  } else {
    handleModeManual();
  }
  cobaKirimUlangBuffer();
}

static void taskKontrol(void *) {
  for (;;) {
    langkahKontrol();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------------- Serial Monitor ----------------
// Perintah Serial (kalibrasi pH, dump register, uji TSS) memakai bus Modbus,
// jadi dieksekusi sambil memegang mutex supaya tidak tabrakan dengan task
// logging / siklus otomatis.
static void taskSerial(void *) {
  for (;;) {
    if (Serial.available()) {
      char command = Serial.read();
      if (command == '\r' || command == '\n' || command == ' ')
        continue;
      if (!kunciModbus(30000)) {
        Serial.println(F("[SERIAL] Bus Modbus sibuk, coba lagi."));
        continue;
      }
      prosesPerintahSerial(command);
      lepasModbus();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---------------- Logging data real-time di MODE MANUAL ----------------
// Berjalan di task FreeRTOS sendiri (core 0) supaya pembacaan sensor yang
// lambat/timeout (Modbus) dan HTTPS TIDAK menahan polling command pompa di
// loop() utama. Di mode otomatis task ini diam (logging ditangani state
// machine). Interval diperpanjang kalau sensor tidak menjawab, supaya tidak
// spam bus RS485.
#if GSM_HEMAT
static const unsigned long INTERVAL_LOG_MANUAL_MS = 30000; // hemat kuota data seluler
#else
static const unsigned long INTERVAL_LOG_MANUAL_MS = 15000;
#endif
static const unsigned long INTERVAL_LOG_GAGAL_MS = 60000;

static void taskLoggingManual(void *) {
  unsigned long tunggu = INTERVAL_LOG_MANUAL_MS;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(tunggu));
    tunggu = INTERVAL_LOG_MANUAL_MS;
    if (modeOtomatis || !internetSiap())
      continue;

    HasilPH hasil;
    hasil.valid = false;
    float tssRaw = 0.0f, tss = 0.0f;
    if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(30000)) != pdTRUE)
      continue;
    hasil = bacaProsesPH();
    tss = bacaRataRataTSS(3, &tssRaw);
    xSemaphoreGive(modbusMutex);

    // Kedua sensor diam -> jangan kirim angka 0 palsu ke dashboard.
    if (!hasil.valid && tssRaw == 0.0f) {
      Serial.println(F("[LOG] Sensor pH & TSS tidak menjawab -- data tidak "
                       "dikirim."));
      // Sensor diam: perpanjang interval supaya tidak spam bus RS485.
      tunggu = INTERVAL_LOG_GAGAL_MS;
      continue;
    }
    bool ok = kirimDataKeAPI(hasil, tss);
    Serial.printf("[LOG] pH=%s TSS=%.2f -> %s\n",
                  hasil.valid ? String(hasil.ph, 2).c_str() : "n/a", tss,
                  ok ? "terkirim" : "GAGAL kirim");
  }
}

// Kirim level air ke "Hydrosense table" (baris level-only) tiap 5 detik, terpisah dari
// logging pH/TSS -- tetap jalan walau sensor RS485 tidak terpasang, dan di
// mode manual maupun otomatis.
#if GSM_HEMAT
static const unsigned long INTERVAL_KIRIM_LEVEL_MS = 15000;
#else
static const unsigned long INTERVAL_KIRIM_LEVEL_MS = 5000;
#endif

static void taskKirimLevel(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(INTERVAL_KIRIM_LEVEL_MS));
    if (!internetSiap())
      continue;
    float levelPercent = 0.0, distanceCm = 0.0, currentMa = 0.0;
    readTankSensor(levelPercent, distanceCm, currentMa);
    if (!kirimLevelKeAPI(levelPercent, distanceCm))
      Serial.println(F("[LEVEL] GAGAL kirim level ke Supabase"));
  }
}

// Denyut alat tiap 5 detik (mode, fase, countdown siklus berikutnya).
#if GSM_HEMAT
static const unsigned long INTERVAL_HEARTBEAT_MS = 10000; // dashboard menganggap hidup bila < 20 dtk
#else
static const unsigned long INTERVAL_HEARTBEAT_MS = 5000;
#endif

static void taskHeartbeat(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(INTERVAL_HEARTBEAT_MS));
    if (!internetSiap())
      continue;
    if (!modeOtomatis) {
      kirimHeartbeatKeSupabase("manual", "manual", -1, pumpState,
                               valveTerbuka(), bacaFloatSwitch());
      continue;
    }
    long sisa = 0;
    const char *fase = "menunggu";
    bool siklusJalan = state != STATE_AUTO_BACA_LEVEL &&
                       state != STATE_A_CEK_STOP && state != STATE_A_BACA_MODE;
    if (siklusJalan) {
      switch (state) {
      case STATE_AUTO_ISI_CHAMBER:
        fase = "siklus: pompa mengisi";
        break;
      case STATE_AUTO_STABILISASI:
        fase = "siklus: menunggu stabil";
        break;
      case STATE_AUTO_BACA_KUALITAS:
      case STATE_AUTO_VALIDASI:
        fase = "siklus: membaca sensor";
        break;
      default:
        fase = "siklus: valve membuang";
        break;
      }
      sisa = -1; // jeda baru dihitung setelah siklus selesai
    } else if (!siklusPertama) {
      unsigned long lewat = millis() - waktuSiklusMulai;
      sisa = lewat >= jedaSiklusMs ? 0 : (long)((jedaSiklusMs - lewat) / 1000UL);
    }
    // Siklus ditolak gerbang air kolam: kabarkan alasannya ke dashboard lewat kolom fase.
    if (!siklusJalan && kolamDitolak())
      fase = kolamStatusTeks();
    kirimHeartbeatKeSupabase("otomatis", fase, sisa, pumpState, valveTerbuka(),
                             bacaFloatSwitch());
  }
}

void mulaiSemuaTask() {
  modbusMutex = xSemaphoreCreateMutex();
  sdQueueMulai(); // antrean data offline di SD card
  rainMulai();    // rain gauge tipping bucket (GPIO4)
  gsmMulai();     // modem SIM7600: data seluler (no-op bila PAKAI_GSM 0)
  xTaskCreatePinnedToCore(taskKontrol, "kontrol", 16384, nullptr, 3, nullptr,
                          1);
  xTaskCreatePinnedToCore(taskSerial, "serial", 16384, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(taskLoggingManual, "logManual", 16384, nullptr, 1,
                          nullptr, 0);
  xTaskCreatePinnedToCore(taskKirimLevel, "kirimLevel", 8192, nullptr, 1,
                          nullptr, 0);
  xTaskCreatePinnedToCore(taskHeartbeat, "heartbeat", 8192, nullptr, 1,
                          nullptr, 0);
  mulaiTaskOTA();
}
