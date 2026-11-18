#include <Arduino.h>
/* =====================================================================
 * HydroSense - Sistem Kontrol & Monitoring Kualitas Air Settling Pond
 * Kerangka kode ESP32 berdasarkan flowchart "cara_kerja" + rantai
 * pengolahan data pH sensor RS485 Modbus
 *
 *
 * Struktur file:
 *   main.cpp       -> objek global, variabel runtime, setup()
 *   Tasks.cpp      -> semua task FreeRTOS (kontrol, serial, display, logging)
 *   Hardware.cpp   -> pembungkus aktuator & sensor + mutex Modbus
 *   WebSync.cpp    -> sinkronisasi override/kalibrasi dari dashboard web
 *   SerialMenu.cpp -> menu & perintah Serial Monitor
 *   OTA.cpp        -> upload firmware lewat WiFi
 * ===================================================================== */
#include "App.h"
#include "Tasks.h"
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

// Mutex bus Modbus (RS485): semua akses Serial2 (pH & TSS) dari task mana pun
// wajib lewat kunciModbus()/lepasModbus() (Hardware.cpp). Dibuat di
// mulaiSemuaTask() sebelum task pertama jalan.
SemaphoreHandle_t modbusMutex = nullptr;

// ========================= VARIABEL GLOBAL RUNTIME =========================
SystemState state = STATE_INIT;
unsigned long timerIsiChamberMulai = 0;
unsigned long jedaSiklusMs = 30UL * 60UL * 1000UL;
unsigned long pompaMaksMs = 5UL * 60UL * 1000UL;
unsigned long valveMaksMs = 5UL * 60UL * 1000UL;
unsigned long waktuSiklusMulai = 0;
bool siklusPertama = false; // boot: siklus baru mulai setelah satu jeda penuh
unsigned long timerKurasMulai = 0;
unsigned long intervalBacaSensorMs = 15UL * 1000UL;
unsigned long intervalSampelMs = 1000UL;
int trimPersen = 15;
unsigned long stabilMs = 10UL * 1000UL;
unsigned long timerStabilisasiMulai = 0;
unsigned long timerPollTerakhir = 0;
unsigned long timerRetryWifiTerakhir = 0;
int jumlahUlangBaca = 0;
HasilPH hasilPhTerakhir;
float tssTerakhir = 0.0;
float suhuTssTerakhir = NAN;
float ntuTerakhir = NAN; // register 6-7 TSS (dugaan NTU)
float tssRawTerakhir = 0.0; // rata-rata raw sensor (sebelum kalibrasi) --
                            // dipakai untuk sample kalibrasi TSS di dashboard
bool modeOtomatis = true;
const float TINGGI_PASANG_CM = 100.0;
const float AMBANG_TINGGI_AIR_CM = 20.0;
const unsigned long TIMEOUT_ISI_MS = 5UL * 60UL * 1000UL;
const unsigned long DELAY_STABIL_MS = 10UL * 1000UL;
const int MAKS_ULANG_BACA = 3;
#if GSM_HEMAT
const unsigned long INTERVAL_POLL_MS = 3000; // data seluler: hemat kuota
#else
const unsigned long INTERVAL_POLL_MS =
    1000; // dipercepat dari 5000ms - polling command manual dari web
#endif
const unsigned long INTERVAL_RETRY_WIFI_MS = 3000;

// ========================= SETUP =========================

void setup() {
  // SAFETY: matikan pompa PALING AWAL (pin pompa di-HIGH-kan = relay OFF).
  // Sebelumnya pumpBegin() baru jalan setelah blink LED + tunggu Serial
  // (hingga ~4 detik), jadi pompa sempat menyala di jendela itu.
  pumpBegin();
  valveController.begin(); // valve juga dimatikan sedini mungkin
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
  tssSensor.begin();
  sensorBegin(); // Inisialisasi ADC Level Sensor
  digitalWrite(PIN_LED_ERROR, LOW);
  for (int i = 0; i < UKURAN_BUFFER; i++)
    buffer[i].terisi = false;
  // Menu langsung tampil di sini -- TIDAK menunggu WiFi/Supabase konek.
  // Koneksi WiFi & polling Supabase berjalan async di state machine (loop()),
  // jadi command kalibrasi tetap bisa dipakai walau device masih offline.
  tampilkanMenuSerial();
  state = STATE_INIT;
  mulaiSemuaTask();
}

// loop() Arduino tidak dipakai: semua kerja ada di task FreeRTOS (Tasks.cpp).
// Hapus task loop supaya tidak makan waktu CPU.
void loop() { vTaskDelete(nullptr); }
