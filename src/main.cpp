/* =====================================================================
 * HydroSense - Sistem Kontrol & Monitoring Kualitas Air Settling Pond
 * Kerangka kode ESP32 berdasarkan flowchart "cara_kerja" + rantai
 * pengolahan data pH sensor RS485 Modbus
 * ===================================================================== */

#include "Config.h"
#include "Level_Sensor.h"
#include "Modes.h"
#include "Network.h"
#include "Pompa.h"
#include "Sensor.h"
#include "TSS_Sensor.h"
#include "Valve_Controller.h"
#include <esp_system.h>
#include <math.h>

// Nama alasan reset terakhir, buat diagnosa brownout/panic/reboot-loop
// (dicetak paling awal di setup(), sebelum inisialisasi lain apapun).
static const char *namaAlasanReset(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON:
    return "POWERON (nyala normal dari mati)";
  case ESP_RST_EXT:
    return "EXTERNAL (tombol RESET/EN ditekan)";
  case ESP_RST_SW:
    return "SOFTWARE (restart via kode)";
  case ESP_RST_PANIC:
    return "PANIC/CRASH (exception, lihat backtrace di UART0)";
  case ESP_RST_INT_WDT:
    return "WATCHDOG INTERNAL (loop macet/blocking kelamaan)";
  case ESP_RST_TASK_WDT:
    return "WATCHDOG TASK (loop macet/blocking kelamaan)";
  case ESP_RST_WDT:
    return "WATCHDOG LAIN";
  case ESP_RST_DEEPSLEEP:
    return "BANGUN DARI DEEP SLEEP";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT! Catu daya/USB drop saat WiFi/radio narik arus -> ganti "
           "kabel USB / port / adaptor";
  case ESP_RST_SDIO:
    return "SDIO";
  default:
    return "TIDAK DIKETAHUI";
  }
}

// Valve Controller (Motorized Valve) menggunakan relay
Valve_Controller valveController;

// TSS Sensor Modbus
TSS_Sensor tssSensor;

// ========================= VARIABEL GLOBAL RUNTIME =========================
SystemState state = STATE_INIT;
unsigned long timerIsiChamberMulai = 0;
unsigned long timerStabilisasiMulai = 0;
unsigned long timerPollTerakhir = 0;
unsigned long timerRetryWifiTerakhir = 0;
unsigned long timerCekOverrideTerakhir = 0;
const unsigned long INTERVAL_CEK_OVERRIDE_MS = 1000; // dipercepat dari 3000ms
unsigned long timerCekKalibrasiTssTerakhir = 0;
const unsigned long INTERVAL_CEK_KALIBRASI_TSS_MS =
    300000UL; // 5 menit -- faktor kalibrasi jarang berubah
unsigned long timerCekKalibrasiHwTssTerakhir = 0;
const unsigned long INTERVAL_CEK_KALIBRASI_HW_TSS_MS =
    2000UL; // wizard interaktif -- operator nunggu respons tiap klik
long idTerakhirDiprosesKalibrasiHwTss = -1;

int jumlahUlangBaca = 0;
HasilPH hasilPhTerakhir;
float tssTerakhir = 0.0;
float tssRawTerakhir = 0.0; // rata-rata raw sensor (sebelum kalibrasi) --
                            // dipakai untuk sample kalibrasi TSS di dashboard
bool modeOtomatis = true;
bool wifiSudahDilaporkan =
    false; // supaya "WiFi tersambung!" cuma dicetak sekali

const float TINGGI_PASANG_CM = 100.0;
const float AMBANG_TINGGI_AIR_CM = 20.0;
const unsigned long TIMEOUT_ISI_MS = 5UL * 60UL * 1000UL;
const unsigned long DELAY_STABIL_MS = 10UL * 1000UL;
const int MAKS_ULANG_BACA = 3;
const unsigned long INTERVAL_POLL_MS =
    1000; // dipercepat dari 5000ms - polling command manual dari web
const unsigned long INTERVAL_RETRY_WIFI_MS = 3000;

// ---------------- Aktuator ----------------
void pompaOn() { pumpON(); }
void pompaOff() { pumpOFF(); }
bool valveBuka() { return valveController.openValve1(); }
bool valveTutup() { return valveController.closeValve1(); }
void wiperMulai() { tssSensor.startScraping(); }

// Siklus otomatis punya beberapa state tunggu yang BISA MACET LAMA/tanpa
// batas waktu kalau sensor fisiknya belum terpasang (mis.
// STATE_AUTO_TUNGGU_KOSONG nunggu float switch LOW, padahal pin float switch
// yang belum dikabel akan selalu terbaca HIGH lewat INPUT_PULLUP -> macet
// permanen). Selama macet di state-state ini, mode dari Supabase TIDAK pernah
// dicek ulang, jadi command manual dari web (toggle valve/pompa) kelihatan
// "delay lama" padahal sebenarnya tidak pernah terbaca sampai siklus otomatis
// kebetulan lepas.
static bool sedangDiTengahSiklusOtomatisPanjang() {
  switch (state) {
  case STATE_TUNGGU_AIR:
  case STATE_TUNGGU_FLOAT_SWITCH:
  case STATE_AUTO_ISI_CHAMBER:
  case STATE_AUTO_STABILISASI:
  case STATE_AUTO_BACA_KUALITAS:
  case STATE_AUTO_VALIDASI:
  case STATE_AUTO_TUNGGU_KOSONG:
    return true;
  default:
    return false;
  }
}

// Dipanggil tiap loop(): kalau lagi macet di siklus otomatis DAN Supabase
// bilang mode sudah "manual", langsung potong siklus otomatis saat itu juga
// (pompa dimatikan demi aman) dan pindah ke mode manual -- supaya command
// dari web tidak nunggu siklus otomatis selesai/macet dulu.
void cekOverrideManualDariWeb() {
  if (!sedangDiTengahSiklusOtomatisPanjang())
    return;
  if (millis() - timerCekOverrideTerakhir < INTERVAL_CEK_OVERRIDE_MS)
    return;
  timerCekOverrideTerakhir = millis();

  if (!bacaModeOperasiDariSupabase()) {
    Serial.println(F("\n[OVERRIDE] Mode MANUAL terdeteksi dari web -> memotong "
                     "siklus otomatis."));
    pompaOff();
    modeOtomatis = false;
    state = STATE_M_BACA_PERINTAH;
  }
}

// Dipanggil tiap loop() (throttled): tarik faktor kalibrasi TSS (slope,
// cal_offset) terbaru dari Supabase dan terapkan ke tssSensor, supaya operator
// bisa kalibrasi ulang probe TSS dari dashboard web tanpa reflash firmware.
void cekKalibrasiTSSDariWeb() {
  if (millis() - timerCekKalibrasiTssTerakhir < INTERVAL_CEK_KALIBRASI_TSS_MS)
    return;
  timerCekKalibrasiTssTerakhir = millis();

  KalibrasiTSS k = bacaKalibrasiTSSDariSupabase();
  if (k.ok) {
    float slopeLama, offsetLama;
    tssSensor.getCalibration(slopeLama, offsetLama);
    if (slopeLama != k.slope || offsetLama != k.offset) {
      tssSensor.setCalibration(k.slope, k.offset);
      Serial.printf("[KALIBRASI TSS] Faktor diperbarui dari web -> slope=%.6f "
                    "offset=%.6f\n",
                    k.slope, k.offset);
    }
  }
}

// Eksekusi kalibrasi hardware TSS two-point (native di sensor, lihat Panduan
// Kalibrasi Sensor TSS) berdasarkan perintah step-by-step dari dashboard --
// menggantikan alur manual Modbus Poll + USB-RS485. Dipanggil tiap loop()
// (throttled). Tiap perintah baru langsung dilapor balik hasilnya ke
// Supabase supaya dashboard bisa update statusnya real-time.
void cekPerintahKalibrasiTssHwDariWeb() {
  if (millis() - timerCekKalibrasiHwTssTerakhir <
      INTERVAL_CEK_KALIBRASI_HW_TSS_MS)
    return;
  timerCekKalibrasiHwTssTerakhir = millis();

  PerintahKalibrasiTssHw p =
      bacaPerintahKalibrasiTssHwDariSupabase(idTerakhirDiprosesKalibrasiHwTss);
  if (!p.ok)
    return;
  idTerakhirDiprosesKalibrasiHwTss = p.id;

  Serial.printf("\n[KALIBRASI HW TSS] Perintah baru #%ld: %s\n", p.id,
                p.perintah.c_str());

  bool sukses = false;
  String pesan;
  int16_t reg13 = -1, reg14 = -1;

  if (p.perintah == "cek_status") {
    bool ok13 = tssSensor.bacaStatusSensor(reg13);
    bool ok14 = tssSensor.bacaKelembabanProbe(reg14);
    float tssLive = tssSensor.readTSS();
    sukses = ok13 && ok14;
    pesan = "status=" + String(reg13) + " kelembaban=" + String(reg14) +
            " TSS_raw=" + String(tssLive, 2);

  } else if (p.perintah == "siapkan") {
    tssSensor.bacaKelembabanProbe(reg14);
    if (reg14 > 10) {
      pesan = "Kelembaban probe tinggi (reg14=" + String(reg14) +
              "), kemungkinan air masuk. Batal.";
    } else if (!tssSensor.keluarModeKalibrasi()) {
      pesan = "Gagal keluar mode kalibrasi (Modbus error).";
    } else {
      delay(200);
      tssSensor.bacaStatusSensor(reg13);
      if (reg13 != TSS_STATUS_NORMAL) {
        pesan = "Register 13 belum normal (=" + String(reg13) +
                ") setelah keluar mode kalibrasi.";
      } else if (!tssSensor.setModeKalibrasi(TSS_CAL_MODE_FACTOR)) {
        pesan = "Gagal set mode factor.";
      } else if (!tssSensor.tulisFactor(1.0f)) {
        pesan = "Gagal reset factor ke 1.0.";
      } else {
        sukses = true;
        pesan = "Siap kalibrasi. reg13=" + String(reg13) +
                ", reg14=" + String(reg14) + ", factor direset ke 1.0";
      }
    }

  } else if (p.perintah == "set_titik1" || p.perintah == "set_titik2") {
    uint16_t titik = (p.perintah == "set_titik1") ? 1 : 2;
    if (!tssSensor.setModeKalibrasi(TSS_CAL_MODE_TWO_POINT)) {
      pesan = "Gagal set mode two-point.";
    } else {
      delay(150);
      if (!tssSensor.setTitikKalibrasi(titik)) {
        pesan = "Gagal set titik kalibrasi " + String(titik);
      } else {
        sukses = true;
        pesan = "Mode two-point aktif, titik " + String(titik) +
                " dipilih. Silakan celup probe & tunggu stabil.";
      }
    }

  } else if (p.perintah == "tulis_titik1" || p.perintah == "tulis_titik2") {
    float nilaiAktual = p.adaNilaiAktual ? p.nilaiAktual : tssSensor.readTSS();
    if (!tssSensor.tulisTargetKalibrasi(p.nilaiTarget)) {
      pesan = "Gagal tulis target (P) kalibrasi.";
    } else {
      delay(150);
      if (!tssSensor.tulisAktualKalibrasi(nilaiAktual)) {
        pesan = "Gagal tulis actual (S) kalibrasi.";
      } else {
        sukses = true;
        pesan = "Tersimpan: P=" + String(p.nilaiTarget, 2) +
                " S=" + String(nilaiAktual, 2);
      }
    }

  } else if (p.perintah == "verifikasi_keluar") {
    if (!tssSensor.keluarModeKalibrasi()) {
      pesan = "Gagal keluar mode kalibrasi.";
    } else {
      delay(300);
      tssSensor.bacaStatusSensor(reg13);
      float tssLive = tssSensor.readTSS();
      sukses = (reg13 == TSS_STATUS_NORMAL);
      pesan = (sukses ? String("Kalibrasi selesai. ")
                      : String("Peringatan: reg13 belum normal. ")) +
              "reg13=" + String(reg13) + " TSS sekarang=" + String(tssLive, 2);
    }

  } else if (p.perintah == "reset_factor") {
    if (!tssSensor.setModeKalibrasi(TSS_CAL_MODE_FACTOR)) {
      pesan = "Gagal set mode factor.";
    } else if (!tssSensor.tulisFactor(1.0f)) {
      pesan = "Gagal reset factor.";
    } else {
      sukses = true;
      pesan = "Factor direset ke 1.0 (mode normal).";
    }

  } else {
    pesan = "Perintah tidak dikenal: " + p.perintah;
  }

  Serial.printf("[KALIBRASI HW TSS] %s: %s\n", sukses ? "SUKSES" : "GAGAL",
                pesan.c_str());
  laporkanHasilKalibrasiTssHw(p.id, sukses, pesan.c_str(), reg13, reg14);
}

// ========================= DEKLARASI FUNGSI =========================
void kalibrasiAwal();
void connectWiFi();
bool wifiTersambung();
bool supabaseSiap();
void tampilkanError(const char *pesan);
bool permintaanBerhenti();
float bacaJarakAir();
bool bacaFloatSwitch();
float bacaRataRataTSS(int nSample, float *rawOut);
HasilPH bacaProsesPH();
bool kirimDataKeAPI(HasilPH hasilPh, float tss, float tssRaw);
bool kirimStatusErrorKeAPI(int errorCount);
void simpanKeBuffer(HasilPH hasilPh, float tss, float tssRaw);
void cobaKirimUlangBuffer();
bool bacaModeOperasiDariSupabase();
void kirimDataManualSekarang();

// ========================= SETUP =========================
void setup() {
  // DIAGNOSTIK: kedipkan LED error SEBELUM apapun lain (termasuk sebelum
  // Serial.begin) supaya kelihatan fisik dari LED apakah board benar-benar
  // boot/jalan, independen dari Serial/USB CDC yang bermasalah.
  pinMode(PIN_LED_ERROR, OUTPUT);
  for (int i = 0; i < 6; i++) {
    digitalWrite(PIN_LED_ERROR, i % 2);
    delay(150);
  }
  digitalWrite(PIN_LED_ERROR, LOW);

  Serial.begin(115200);

  // ESP32-S3 pakai USB CDC native (ARDUINO_USB_CDC_ON_BOOT=1), BUKAN chip
  // USB-UART (CP2102/CH340) seperti board ESP32 biasa. Konsekuensinya: board
  // TIDAK auto-reset saat Serial Monitor dibuka, jadi banner boot di bawah
  // sering sudah lewat SEBELUM monitor sempat konek -> layar kelihatan kosong
  // padahal board sudah jalan normal. Tunggu host benar-benar konek (Serial
  // jadi true saat DTR di-assert), tapi dibatasi 3 detik supaya board tetap
  // boot normal walau tidak ada Serial Monitor yang dibuka (operasi mandiri).
  unsigned long tungguSerial = millis();
  while (!Serial && millis() - tungguSerial < 3000) {
    delay(10);
  }
  delay(200); // beri waktu terminal di sisi host settle sebelum banner dikirim

  Serial.println("\n[SISTEM] Booting Hydrosense...");
  Serial.print("[SISTEM] Alasan reset terakhir: ");
  Serial.println(namaAlasanReset(esp_reset_reason()));

  setupSensor(); // Inisialisasi Modbus pH sensor dari Sensor.cpp

  pinMode(PIN_FLOAT_SWITCH, INPUT_PULLUP);

  valveController.begin();
  tssSensor.begin();
  sensorBegin(); // Inisialisasi ADC Level Sensor
  pumpBegin();   // Inisialisasi Pompa (sudah otomatis dimatikan saat startup)
  digitalWrite(PIN_LED_ERROR, LOW);

  for (int i = 0; i < UKURAN_BUFFER; i++)
    buffer[i].terisi = false;

  // Menu langsung tampil di sini -- TIDAK menunggu WiFi/Supabase konek.
  // Koneksi WiFi & polling Supabase berjalan async di state machine (loop()),
  // jadi command kalibrasi tetap bisa dipakai walau device masih offline.
  Serial.println("\n=== Hydrosense Inlet System ===");
  Serial.println("COMMAND KALIBRASI PH SENSOR:");
  Serial.println(" 4 = Kalibrasi titik 1 (pH 4.01)");
  Serial.println(" 1 = Kalibrasi titik 2 (pH 9.01)");
  Serial.println(" A = Verifikasi pH 4.01");
  Serial.println(" 7 = Verifikasi pH 7.01");
  Serial.println(" C = Verifikasi pH 9.01");
  Serial.println(" S = Tampilkan Status Sensor & Sistem");
  Serial.println(" T = Baca Status & Data TSS Sensor");
  Serial.println(" W = Jalankan Wiper/Scraping TSS");
  Serial.println(" H = Health Report");
  Serial.println(" D = Dump Modbus Registers");
  Serial.println(
      " N = Info Status WiFi (SSID/IP/RSSI) - bisa dicek kapan saja");
  Serial.println(" K = Kirim manual pH+TSS ke Supabase SEKARANG (paksa, tanpa "
                 "nunggu siklus otomatis/level air)");
  Serial.println();

  state = STATE_INIT;
}

// ========================= LOOP UTAMA =========================
void loop() {
  // 1. Cek perintah kalibrasi dari Serial Monitor
  if (Serial.available()) {
    char command = Serial.read();
    if (command == '4')
      calibratePH4();
    else if (command == '1')
      calibratePH10();
    else if (command == 'A' || command == 'a')
      verifyPH4();
    else if (command == '7')
      verifyPH7();
    else if (command == 'C' || command == 'c')
      verifyPH10();
    else if (command == 'O' || command == 'o')
      writeDeviation();
    else if (command == 'R' || command == 'r')
      resetCalibration();
    else if (command == 'S' || command == 's')
      showStatus();
    else if (command == 'T' || command == 't')
      tssSensor.readStatus();
    else if (command == 'W' || command == 'w')
      tssSensor.startScraping();
    else if (command == 'H' || command == 'h')
      showHealthReport();
    else if (command == 'D' || command == 'd')
      dumpModbusRegisters();
    else if (command == 'N' || command == 'n')
      tampilkanStatusWiFi();
    else if (command == 'K' || command == 'k')
      kirimDataManualSekarang();
  }

  // 1b. Override manual dari web -- lihat catatan di atas
  // cekOverrideManualDariWeb().
  cekOverrideManualDariWeb();

  // 1c. Faktor kalibrasi TSS dari web (throttled, lihat komentar fungsi)
  cekKalibrasiTSSDariWeb();

  // 1d. Wizard kalibrasi hardware TSS two-point dari web (throttled)
  cekPerintahKalibrasiTssHwDariWeb();

  // 2. Jalankan State Machine
  switch (state) {
  case STATE_INIT:
    kalibrasiAwal();
    connectWiFi();
    state = STATE_CEK_KONEKSI;
    break;

  case STATE_CEK_KONEKSI:
    // Throttle: jangan panggil supabaseSiap() (HTTP request) tiap iterasi
    // loop(), cukup tiap INTERVAL_RETRY_WIFI_MS -- supaya loop() tetap
    // responsif membaca command Serial selagi menunggu koneksi.
    if (millis() - timerRetryWifiTerakhir >= INTERVAL_RETRY_WIFI_MS) {
      timerRetryWifiTerakhir = millis();
      if (!wifiTersambung()) {
        Serial.printf("[WIFI] Belum tersambung (status=%d), coba lagi...\n",
                      (int)WiFi.status());
        connectWiFi();
      } else if (!wifiSudahDilaporkan) {
        Serial.print("[WIFI] Tersambung! IP: ");
        Serial.println(WiFi.localIP());
        wifiSudahDilaporkan = true;
      } else if (supabaseSiap()) {
        digitalWrite(PIN_LED_ERROR, LOW);
        // Paksa ambil faktor kalibrasi TSS sekarang (bukan nunggu throttle 5
        // menit) supaya begitu online, kalibrasi terbaru dari web langsung
        // dipakai.
        timerCekKalibrasiTssTerakhir = 0;
        state = STATE_A_CEK_STOP;
      } else {
        tampilkanError("WiFi tersambung tapi Supabase belum siap");
      }
    }
    break;

  case STATE_A_CEK_STOP:
    state = permintaanBerhenti() ? STATE_STOPPED : STATE_A_BACA_MODE;
    break;

  case STATE_A_BACA_MODE: {
    // Satu fetch dipakai buat dua hal (cek mode DAN, kalau manual,
    // langsung eksekusi command-nya) -- sebelumnya ini 2 HTTP request
    // terpisah balapan minta data yang sama, buang-buang waktu TLS handshake.
    KontrolManual k = bacaKontrolDariSupabase();
    modeOtomatis = !k.ok || k.mode == "otomatis";
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
    if (modeOtomatis) {
      handleModeOtomatis();
    } else {
      handleModeManual();
    }
    break;
  }

  cobaKirimUlangBuffer();
}

// ===================================================================
//                      FUNGSI PENDUKUNG UMUM
// ===================================================================

void kalibrasiAwal() {
  // Kalibrasi awal jika perlu
}

// Command 'K' -- baca pH+TSS SEKARANG dan langsung kirim ke Supabase, TANPA
// nunggu siklus otomatis (isi chamber -> stabilisasi -> baca -> kirim) yang
// butuh level air tangki utama cukup tinggi dulu. Berguna buat testing/debug
// supaya dashboard kelihatan ada data walau tangki fisik belum terisi air.
// TSS tetap dikirim walau pH sensor tidak terpasang/invalid (lihat
// kirimDataKeAPI() -- ph & water_temperature dikirim null kalau pH invalid).
void kirimDataManualSekarang() {
  Serial.println(F("\n>>> Kirim manual pH+TSS ke Supabase (paksa, skip siklus "
                   "otomatis)..."));
  HasilPH hasil = bacaProsesPH();
  float tssRaw = 0.0f;
  float tss = bacaRataRataTSS(5, &tssRaw);
  Serial.printf("pH  : %s (valid=%d)\n",
                hasil.valid ? String(hasil.ph, 2).c_str() : "n/a", hasil.valid);
  Serial.printf("TSS : %.2f mg/L (raw=%.2f)\n", tss, tssRaw);
  bool terkirim = kirimDataKeAPI(hasil, tss, tssRaw);
  if (terkirim) {
    Serial.println(F("Terkirim ke Supabase."));
  } else {
    Serial.println(F("Gagal kirim (WiFi belum siap?). Data TIDAK dibuffer -- "
                     "coba lagi manual."));
  }
}

void tampilkanError(const char *pesan) {
  Serial.print("[ERROR] ");
  Serial.println(pesan);
  digitalWrite(PIN_LED_ERROR, HIGH);
}

bool permintaanBerhenti() {
  // TODO: baca flag "stop" dari kolom terpisah di TABLE_MODE
  return false;
}

// ---------------- Sensor Level (Radar 4-20mA) ----------------
float bacaJarakAir() {
  float levelPercent = 0.0;
  float distanceCm = 0.0;
  float currentMa = 0.0;
  readTankSensor(levelPercent, distanceCm, currentMa);
  return distanceCm;
}

bool bacaFloatSwitch() { return digitalRead(PIN_FLOAT_SWITCH) == HIGH; }

// ---------------- TSS (Modbus) ----------------
// Rata-ratakan RAW dulu baru dikalibrasi sekali di akhir (bukan rata-rata
// hasil kalibrasi per-sample) -- setara secara matematis untuk transformasi
// linear, tapi sekalian dapat rata-rata raw (rawOut) untuk dipakai sebagai
// sample kalibrasi TSS di dashboard tanpa baca sensor 2x.
float bacaRataRataTSS(int nSample, float *rawOut) {
  float sumRaw = 0.0;
  int count = 0;
  for (int i = 0; i < nSample; i++) {
    float val = tssSensor.readTSS();
    if (val >= 0) {
      sumRaw += val;
      count++;
    }
    delay(100);
  }
  float avgRaw = (count > 0) ? (sumRaw / count) : 0.0;
  if (rawOut)
    *rawOut = avgRaw;

  float slope, offset;
  tssSensor.getCalibration(slope, offset);
  float calibrated = (slope * avgRaw) + offset;
  return (calibrated < 0.0f) ? 0.0f : calibrated;
}

// ===================================================================
//        MODBUS RTU (baca sensor pH via Sensor.cpp)
// ===================================================================

HasilPH bacaProsesPH() {
  HasilPH hasil;
  PhReading phRead = readSensor();

  hasil.valid = phRead.valid;
  hasil.ph = phRead.ph;
  hasil.suhu = phRead.tempC;
  hasil.sd = phRead.sd;
  hasil.drift = phRead.drift;
  hasil.kualitas = String(phRead.quality);
  hasil.compliant = phRead.compliant;
  hasil.rawPh = phRead.rawPh;
  hasil.rawTemp = phRead.rawTemp;
  hasil.modbusErrorCount = phRead.modbusErrors;

  return hasil;
}
