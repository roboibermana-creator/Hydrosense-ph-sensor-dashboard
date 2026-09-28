#ifndef CONFIG_H
#define CONFIG_H

// --- KREDENSIAL WIFI & SUPABASE ---
// SUPABASE_URL & SUPABASE_KEY SENGAJA TIDAK di-#define di sini -- nilainya
// sudah didefinisikan sebagai variabel (bukan macro) di Network.cpp
// (SUPABASE_URL, SUPABASE_API_KEY) dan dideklarasikan extern di Network.h.
// Kalau di-#define juga di sini dengan nama yang sama, preprocessor akan
// menyubstitusi teks "extern const char* SUPABASE_URL;" di Network.h jadi
// "extern const char* <string literal>;" yang gagal compile.
#define WIFI_SSID "SPRM-CORP"
#define WIFI_PASSWORD "e$kr1mN@N@s3000"
#define DEVICE_ID "hydrosense-inlet-01"

// --- HARDWARE PIN (sensor pH Modbus RS485 via MAX485 di Serial2) ---
#define RS485_RX_PIN 16
#define RS485_TX_PIN 15
#define RS485_RE_DE_PIN 14
#define RS485_BAUD 9600 // datasheet: 9600 8N1

// --- PIN AKTUATOR & SENSOR LAIN ---
// GPIO26-32 dipakai internal SPI flash ESP32-S3 -> JANGAN dipakai untuk apapun
// (bisa crash / korup akses flash). Pompa dipindah ke GPIO9.
#define PUMP_PIN 9 // relay pompa peristaltik (Pompa.cpp)
// Kebalik dari kabel fisik motorized valve (waktu dites 'buka' malah nutup) --
// makanya nomor pin ditukar di sini, BUKAN di kode logikanya, supaya
// openValve1()/closeValve1() tetap berarti sama seperti namanya.
#define VALVE1_OPEN_PIN                                                        \
  48 // relay motorized valve - arah buka (Valve_Controller.cpp)
#define VALVE1_CLOSE_PIN                                                       \
  47 // relay motorized valve - arah tutup (Valve_Controller.cpp)
#define PIN_FLOAT_SWITCH                                                       \
  6 // JANGAN pakai 19/20 -> itu pin native USB D-/D+ ESP32-S3,
    // menyentuhnya bikin koneksi USB CDC (Serial Monitor) putus/crash
#define PIN_LED_ERROR 2

// --- MODBUS SENSOR: register pembacaan ---
// Alamat pH & suhu diambil dari hasil dump 'D' pada sensor Anda
// (0x0001 = 284 -> 28,4 C ; 0x0006 = 519 -> pH 5,19), BUKAN dari
// dokumen SEN0708 yang menaruh pH di 0x0000.
#define SENSOR_SLAVE_ID 1
#define PH_REGISTER_ADDRESS 0x0006 // pH x100, unsigned
#define PH_SCALE 100.0
#define TEMP_REGISTER_ADDRESS 0x0001 // suhu x10, signed
#define TEMP_SCALE 10.0
#define MV_REGISTER_ADDRESS                                                    \
  0x0007 // dugaan: tegangan elektroda x10 (mV); hanya untuk laporan
#define MV_SCALE 10.0

// --- MODBUS SENSOR TSS (RD-SSBWT-01, keluarga BOQU ZDYG-2087-01) ---
// Sensor fisik yang terpasang berlabel RD-SSBWT-01 -- konstruksi & spek (RS485,
// 9600 baud, 0-50.000 mg/L, wiper motor) sangat dekat dengan BOQU ZDYG-2087-01,
// manual komunikasinya dipakai sebagai acuan register di bawah.
#define TSS_SLAVE_ID                                                           \
  2 // Sudah terverifikasi jalan di hardware (bukan 0x01 default manual --
    // dipisah dari pH di address 1)
#define REG_TSS_VALUE 0x0002     // Baca TSS (function 03, quantity 2 -> float)
#define REG_BRUSHING_TIME 0x000B // Baca waktu brushing terakhir (function 03)
#define REG_MANUAL_SCRAPING                                                    \
  0x0014 // Tulis manual brush/wiper ON (function 06, value = 66) -- HANYA
         // berlaku saat mode NORMAL (REG_CAL_MODE = 1). Register yang SAMA
         // (0x0014) dipakai ulang sensor sebagai REG_CAL_TARGET saat mode
         // two-point kalibrasi aktif (REG_CAL_MODE = 2) -- lihat Panduan
         // Kalibrasi Sensor TSS resmi. JANGAN panggil startScraping() di
         // tengah proses kalibrasi (lihat TSS_Sensor::*Kalibrasi*()).
#define REG_AUTO_SCRAPING_INTERVAL                                             \
  0x0015 // Tulis interval auto-brush dalam menit (function 06)
#define TSS_MANUAL_SCRAPING_VALUE 66

// --- KALIBRASI HARDWARE TSS (two-point, native di sensor -- lihat Panduan
// Kalibrasi Sensor TSS resmi RD-SSBWT-01/BOQU) ---
// Menggantikan alur manual "Modbus Poll + USB-RS485" -- ESP32 yang langsung
// baca/tulis register ini, operator tinggal input nilai Partech (P) & pilih
// langkah lewat dashboard web.
#define REG_STATUS_SENSOR 0x000D    // Baca status (function 03) -- harus 2 = mode ukur normal
#define REG_KELEMBABAN_PROBE 0x000E // Baca kelembaban probe (function 03) -- >10 = air masuk, stop
#define REG_CAL_FACTOR 0x0006       // Factor (float, RW) 0.1-10 -- set 1.0 sebelum kalibrasi
#define REG_CAL_TARGET                                                        \
  0x0014 // Target kalibrasi (float, RW) = nilai Partech (P) -- SAMA ALAMAT
         // dgn REG_MANUAL_SCRAPING, hanya valid saat REG_CAL_MODE = 2
#define REG_CAL_ACTUAL 0x0016 // Actual kalibrasi (float, RW) = nilai sensor ini (S)
#define REG_CAL_MODE 0x001B   // 1 = factor, 2 = two-point, 3 = four-point
#define REG_CAL_POINT 0x001C  // Nomor titik kalibrasi: 1, 2, (3, 4)
#define REG_CAL_EXIT_CONTROL 0x003B // Tulis 33 = keluar mode kalibrasi
#define TSS_CAL_EXIT_VALUE 33
#define TSS_CAL_MODE_FACTOR 1
#define TSS_CAL_MODE_TWO_POINT 2
#define TSS_STATUS_NORMAL 2

// --- SENSOR LEVEL RADAR 4-20mA ---
#define ADC_PIN                                                                \
  4 // (Diubah ke 4) A_OUT converter -> GPIO4. (ESP32-S3 wajib pin 1-10 untuk
    // ADC1).
#define ADC_SAMPLES                                                            \
  32 // BUKAN PIN! Ini artinya: "ambil data 32 kali lalu dirata-rata"
const float I_MIN_MA = 4.0;
const float I_MAX_MA = 20.0;
const float V_MIN_V = 0.0;          // output terendah converter
const float V_MAX_V = 3.0;          // output tertinggi converter
const float TANK_HEIGHT_CM = 100.0; // Tinggi tangki
const bool SENSOR_INVERTED = true;

// Threshold status untuk level
const float TH_LOW_LOW = 10.0;
const float TH_HIGH_HIGH = 90.0;
extern float thLow;
extern float thHigh;

// --- MODBUS SENSOR: register kalibrasi (dokumen rumus_kalibrasi_ph.md) ---
// 0x0120/0x0121 ditulis dengan function 0x10, 0x0050 dengan 0x06.
// Belum diverifikasi pada sensor ini -> cek datasheet sebelum kalibrasi.
#define CAL_REGISTER_CMD 0x0120   // 1 = titik 1 (asam), 2 = titik 2 (basa)
#define CAL_REGISTER_VALUE 0x0121 // round(pH_buffer x 100)
#define DEVIATION_REGISTER 0x0050 // round(delta_pH x 100), signed

// --- TITIK KALIBRASI & VERIFIKASI ---
#define BUFFER_PH4_VALUE 4.01  // titik kalibrasi 1 (ditulis ke sensor)
#define BUFFER_PH7_VALUE 7.01  // verifikasi saja
#define BUFFER_PH10_VALUE 9.01 // titik kalibrasi 2 (ditulis ke sensor)

// --- GERBANG VALIDITAS ---
#define PH_VALID_MIN 0.0
#define PH_VALID_MAX 14.0
#define TEMP_VALID_MIN -10.0
#define TEMP_VALID_MAX 85.0

// --- AGREGASI (median N sampel) ---
#define MEDIAN_SAMPLES 5
#define SAMPLE_INTERVAL_MS 250
#define READ_PERIOD_MS 2000

// --- GERBANG STABILITAS (satuan pH, jendela 2 menit) ---
// Nama _MV dipertahankan karena dipakai PhStability.h; nilainya pH.
#define STB_SD_GOOD_MV 0.02    // sd <= 0,02 pH        -> good
#define STB_SD_OK_MV 0.05      // sd <= 0,05 pH        -> fair
#define STB_DRIFT_GOOD_MV 0.01 // |drift| <= 0,01 pH/menit
#define STB_DRIFT_OK_MV 0.03   // |drift| <= 0,03 pH/menit
#define STB_UNIT "pH"
#define PH_BOARD_GAIN 1.0 // mV dari register sensor, tanpa penguat papan

// --- KRITERIA PENERIMAAN VERIFIKASI (bagian 4.4) ---
#define VERIF_SLOPE_TOL 0.05  // |m - 1|
#define VERIF_OFFSET_TOL 0.15 // |b|
#define VERIF_R2_MIN 0.99
#define VERIF_MAX_ERROR 0.15 // spesifikasi sensor

// --- BAKU MUTU (PermenLH No. 9/2006, pertambangan bijih nikel) ---
#define BAKU_MUTU_PH_MIN 6.0
#define BAKU_MUTU_PH_MAX 9.0

// --- PARAMETER KALIBRASI ---
#define CAL_WARMUP_MS 120000UL // 2 menit warm-up

// --- PARAMETER LOGGING ---
#define SUPABASE_LOG_INTERVAL_MS 60000UL
#define SUPABASE_COMMAND_CHECK_MS 3000UL

#endif
