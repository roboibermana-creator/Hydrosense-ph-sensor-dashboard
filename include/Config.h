#ifndef CONFIG_H
#define CONFIG_H
#define WIFI_SSID "SPRM-CORP"
#define WIFI_PASSWORD "e$kr1mN@N@s3000" // PERTAHANKAN nilai lama secara lokal
//#define WIFI_SSID "similiarly"
//#define WIFI_PASSWORD "111222333"
#define DEVICE_ID "hydrosense-inlet-01"

// --- OTA (upload firmware lewat WiFi, lihat OTA.cpp) ---
#define OTA_HOSTNAME DEVICE_ID
#define OTA_PASSWORD "hydrosense-ota" // GANTI sebelum dipasang di lapangan
// data seluler (PAKAI_GSM) saja. Bila WiFi putus dan PAKAI_GSM 1, data otomatis lewat GSM.
// Bila keduanya 0, node tanpa internet: mulai di mode MANUAL dan tidak mengirim data.
#define PAKAI_WIFI 1
#define FORCE_MODE_MANUAL 0 // mode ikut kolom "mode" dari dashboard (tombol pilih mode)

// --- HARDWARE PIN (sensor pH Modbus RS485 via MAX485 di Serial2) ---
#define RS485_RX_PIN 16
#define RS485_TX_PIN 15
#define RS485_RE_DE_PIN 14
#define RS485_BAUD 9600 // datasheet/manual: 9600 8N1; pastikan Serial2.begin dipanggil sekali

// --- PIN AKTUATOR & SENSOR LAIN ---
// GPIO26-32 dipakai internal SPI flash ESP32-S3 -> JANGAN dipakai untuk apapun
// (bisa crash / korup akses flash). Pompa dipindah ke GPIO6 (GPIO4 = rain gauge,
// GPIO5 = float switch, jadi tidak boleh dipakai pompa).
#define PUMP_PIN 6 // relay pompa peristaltik (Pompa.cpp)
#define VALVE_TRAVEL_MS 15000UL
// Pemetaan fisik di board ini: relay BUKA = GPIO47, relay TUTUP = GPIO21.
// GPIO21 = relay TUTUP.
#define VALVE1_OPEN_PIN \
  47 // relay motorized valve - arah buka (Valve_Controller.cpp)
#define VALVE1_CLOSE_PIN \
  21 // relay motorized valve - arah tutup (Valve_Controller.cpp)
#define PIN_FLOAT_SWITCH \
  5 // GPIO4 = RAIN_PIN, GPIO9 = ADC_PIN (radar 4-20mA) -> JANGAN dipakai ganda.
    // Wiring: kabel kuning 1 -> GPIO5, kabel kuning 2 -> GND (INPUT_PULLUP).
#define INTERLOCK_POMPA_MANUAL 1 // float switch HIGH = pompa dikunci OFF (mutlak)
#define PIN_LED_ERROR 2

// --- SD CARD (slot di Shield ESP32-S3 Pro, SPI) ---
// Antrean data offline: pembacaan yang gagal terkirim disimpan di SD lalu
// dikirim ulang saat online (lihat SdQueue.cpp). GPIO40 = CS SD.
#define ENABLE_SD_QUEUE 0 // 1 = aktifkan antrean SD (butuh kartu FAT32 terpasang)
#define SD_CS_PIN 40
#define SD_MOSI_PIN 11
#define SD_SCK_PIN 12
#define SD_MISO_PIN 13

// --- RAIN GAUGE tipping bucket (kontak reed, langsung ke pin; lihat Rain.cpp) ---
// Satu kabel -> GPIO4, kabel lain -> GND (INPUT_PULLUP internal, 3V3 saja).
// Modul DFRobot SEN0575 (UART GPIO17/18) rusak dan tidak dipakai lagi.
#define RAIN_PIN 4
#define RAIN_MM_PER_TIP 0.39f // mm per ketukan. Kalibrasi 300 mL / luas 5500 mm2 / 139 ketukan = 0,392 (spesifikasi 0,28 tidak cocok). Koreksi dengan B/E.
// Luas TANGKAPAN = bukaan DALAM corong (mm^2), dipakai kalibrasi 'E'. Perkiraan dari foto:
// 110 x 50 mm = 5500 mm^2 (bodi luar 118 x 59 BUKAN luas tangkapan). UKUR ULANG dengan
// penggaris di bibir dalam corong, karena kesalahan luas langsung menggeser hasil kalibrasi.
// Corong bulat: 3.14159 * (diameter/2)^2.
#define RAIN_LUAS_CORONG_MM2 (110.0f * 50.0f)
#define RAIN_DEBOUNCE_MS 50  // abaikan ketukan yang datang lebih cepat dari ini
#define RAIN_TZ_OFFSET_S (7 * 3600) // WIB: batas "hari ini" = tengah malam lokal

// --- GSM/4G: modul SIMCom SIM7600 (lihat Gsm.cpp) ---
// Node memakai data seluler untuk bicara langsung ke Supabase (cadangan bila WiFi putus).
// Wiring: TX ESP (GPIO18) -> RXD modem, RX ESP (GPIO1) <- TXD modem, GND bersama,
// PWRKEY modem <- GPIO48. Catu modem 5 V TERPISAH, arus puncak ~2 A (jangan dari 3V3 ESP).
// OTA ArduinoOTA hanya berjalan lewat WiFi; lewat GSM firmware di-flash via USB.
#define PAKAI_GSM 0 // sementara 0: uji WiFi saja (kembalikan ke 1 bila modem terpasang)
#define GSM_APN "internet"         // Telkomsel (simPATI/kartuHALO): "internet"; user/pass kosong
#define GSM_APN_USER ""
#define GSM_APN_PASS ""
#define GSM_SERIAL Serial1
#define GSM_BAUD 115200
#define GSM_TX_PIN 18               // TX ESP -> RXD modem
#define GSM_RX_PIN 1                // RX ESP <- TXD modem
#define GSM_PWRKEY_PIN 48           // -1 = tidak dipakai (modem selalu menyala)
#define GSM_PWRKEY_AKTIF_LOW 1      // 1 = pin ditarik LOW untuk menekan PWRKEY, 0 = HIGH
#define GSM_PWRKEY_MS 1200
#define GSM_SSL_ENTROPY_PIN 3       // pin ADC mengambang untuk data acak TLS (biarkan tidak tersambung)          // lama menekan PWRKEY
// Hemat kuota: tanpa WiFi (data seluler) polling web diperlambat.
#if PAKAI_GSM && !PAKAI_WIFI
#define GSM_HEMAT 1
#else
#define GSM_HEMAT 0
#endif

// --- MODBUS SENSOR: register pembacaan pH ---
// Alamat pH & suhu diambil dari hasil dump 'D' pada sensor Anda
// (0x0001 = 284 -> 28,4 C ; 0x0006 = 519 -> pH 5,19), BUKAN dari
// dokumen SEN0708 yang menaruh pH di 0x0000.
#define SENSOR_SLAVE_ID 1
#define PH_REGISTER_ADDRESS 0x0006 // pH x100, unsigned
#define PH_SCALE 100.0
#define TEMP_REGISTER_ADDRESS 0x0001 // suhu x10, signed
#define TEMP_SCALE 10.0
#define MV_REGISTER_ADDRESS \
  0x0007
#define MV_SCALE 10.0
#define PH_KOMPENSASI_SUHU 1
#define PH_SUHU_REFERENSI_C 25.0

// >>> [DIGANTI] MULAI BLOK MODBUS SENSOR TSS
// Register berikut dicocokkan dengan tabel komunikasi BOQU ZDYG-2087-01.
// Sensor fisik berlabel RD-SSBWT-01: konfirmasi ke vendor jika perilaku
// firmware/varian perangkat berbeda dari manual acuan.
#define TSS_SLAVE_ID 2 // Harus cocok dengan alamat sensor fisik; pH menggunakan ID 1.

// Terverifikasi lewat baca register slave 2 (2 Nov): TSS dan suhu adalah FLOAT32
// dua register, word BAWAH dulu (CDAB). Alamat protokol 0-based (Modbus Poll).
//   0x0000-0x0001 = TSS (mg/L)   contoh: 33658,17677 -> 2264.2
//   0x0002-0x0003 = suhu (C)     contoh: 36460,16879 -> 29.9
// (Catatan lama "register 1 = Int16 TSS" keliru: itu hanya word ATAS float TSS.)
#define REG_TSS_VALUE             0x0000 // FC03; Float32 CDAB, 2 register
#define REG_TSS_TEMPERATURE       0x0002 // FC03; Float32 CDAB, 2 register
#define REG_TSS_NTU               0x0006 // FC03; Float32 CDAB, DUGAAN turbidity (NTU), belum terbukti
// Pengali dari nilai float ke satuan asli (float sudah dalam mg/L dan C).
#define TSS_VALUE_SCALE           1.0f
#define TSS_TEMP_SCALE            1.0f
// TERBUKTI (2 Nov, dari dashboard): faktor 2.0 -> TSS 472.13, faktor 3.0 -> 706.39
// (rasio 1.496 ~ 3/2), jadi TSS sensor = faktor x nilai dasar. Peta float sensor
// ini bergeser -2 dari manual OEM (faktor di 4-5, bukan 6-7).
#define REG_CAL_FACTOR            0x0004 // FC03/FC16; Float32 (CDAB) pada register 4-5
#define REG_BRUSHING_TIME         0x000B // FC03; Int16
#define REG_RESPONSE_TIME         0x000C // FC03/FC06; response time dalam detik, 3-60
#define REG_MEASUREMENT_PARAMETER 0x000D // FC03; 1=turbidity, 2=suspended solids
#define REG_KELEMBABAN_PROBE      0x000E // FC03; manual merekomendasikan <10
// Wiper: register kontrolnya tidak ditemukan di sensor ini (manual OEM 0x14/0x15
// terbaca 0 dan tidak menggerakkan wiper), jadi fitur wiper dihapus dari firmware.
// Peta sensor ini bergeser -2 dari manual OEM (faktor 4-5, mode TSS/turbidity 13).

// Alias kompatibilitas untuk source lama. Gunakan REG_MEASUREMENT_PARAMETER
// di kode baru karena register 13 BUKAN register status umum.
#define REG_STATUS_SENSOR REG_MEASUREMENT_PARAMETER
#define TSS_PARAMETER_TURBIDITY 1
#define TSS_PARAMETER_SUSPENDED_SOLIDS 2
#define TSS_STATUS_NORMAL TSS_PARAMETER_SUSPENDED_SOLIDS // alias lama saja
// <<< [DIGANTI] AKHIR BLOK MODBUS SENSOR TSS

// >>> [DIGANTI] MULAI BLOK KALIBRASI HARDWARE TSS
#define REG_CAL_MODE  0x001B // FC06; 1=factor, 2=two-point, 3=four-point
#define REG_CAL_POINT 0x001C // FC06; titik 1-2 atau 1-4 sesuai mode
#define REG_CAL_TARGET 0x0014 // FC16; Float32 reg 20-21 saat kalibrasi multipoint
#define REG_CAL_ACTUAL 0x0016 // FC16; Float32 reg 22-23 saat kalibrasi multipoint
#define TSS_CAL_MODE_FACTOR 1
#define TSS_CAL_MODE_TWO_POINT 2
#define TSS_FACTOR_MIN 0.11f // Rentang manual: 0.11-9.99
#define TSS_FACTOR_MAX 9.99f

// Kompatibilitas source lama saja. Manual yang dilampirkan tidak mendokumentasikan
// perintah register 59/value 33. Driver perbaikan tidak mengirim perintah ini.
#define REG_CAL_EXIT_CONTROL 0x003B
#define TSS_CAL_EXIT_VALUE 33
// <<< [DIGANTI] AKHIR BLOK KALIBRASI HARDWARE TSS

// --- SENSOR LEVEL RADAR 4-20mA ---
// Dipindah dari GPIO4 ke GPIO9 (GPIO4 kini rain gauge). Keluaran converter maks 3,3 V!
#define ADC_PIN \
  9 // A_OUT converter -> GPIO9. ESP32-S3: gunakan ADC1 untuk pembacaan stabil saat Wi-Fi aktif.
#define ADC_SAMPLES \
  32 // Ambil data 32 kali lalu dirata-rata.
const float I_MIN_MA = 4.0;
const float I_MAX_MA = 20.0;
// Converter DFRobot SEN0262: resistor sense 120 ohm, jadi Vout = I x 120 ohm
// (0-25 mA -> 0-3 V; 4 mA = 0,48 V, 20 mA = 2,40 V). Ukur resistor/arus dengan multimeter bila
// hasil bacaan meleset, lalu sesuaikan nilai ini.
const float SENSE_RESISTOR_OHM = 120.0;
const float V_ADC_MAKS_V = 3.1;     // batas atas baca ADC (jangan lebih dari ~3,3 V)
const float RADAR_RANGE_CM = 500.0; // rentang ukur radar: 4-20 mA = 0..5 m
// Jarak sensor ke dasar kolam = tinggi air maksimum yang dihitung. Sementara = rentang radar (5 m);
// kecilkan bila sensor dipasang lebih rendah / dasar kolam lebih dekat dari 5 m.
const float TANK_HEIGHT_CM = RADAR_RANGE_CM;
const bool SENSOR_INVERTED = true;

// --- GERBANG "AIR KOLAM KERING" untuk jadwal otomatis (Kolam.cpp) ---
// Sebelum siklus otomatis dimulai, tinggi air kolam diukur. Kurang dari batas ini = siklus DITOLAK
// (pompa tidak dinyalakan), dashboard menampilkan "air kolam kering", dan dicek lagi tiap beberapa
// detik sampai air naik. Logika siklus yang lama TIDAK diubah; ini hanya syarat tambahan di awalnya.
// Tinggi air = level% x TANK_HEIGHT_CM (4-20 mA = 0..RADAR_RANGE_CM = 5 m; arah diatur SENSOR_INVERTED).
// 0 = matikan gerbang ini.
#define KOLAM_CEK_AKTIF 1
const float KOLAM_MIN_AIR_CM = 20.0; // percobaan: >= 20 cm siklus jalan, di bawahnya ditolak

// Threshold status untuk level
const float TH_LOW_LOW = 10.0;
const float TH_HIGH_HIGH = 90.0;
extern float thLow;
extern float thHigh;

// --- MODBUS SENSOR: register kalibrasi (dokumen rumus_kalibrasi_ph.md) ---
// 0x0120/0x0121 ditulis dengan function 0x10, 0x0050 dengan 0x06.
// Cocok dengan wiki DFRobot SEN0708 (wiki.dfrobot.com/sen0708/docs/23388):
// 0x0120 = nomor titik (1/2), 0x0121 = pH standar x100, keduanya FC 0x10 dalam
// satu frame 2 register; 0x0050 = deviasi pH x100 (FC 0x06). Belum diuji tulis
// langsung di sensor fisik ini.
#define CAL_REGISTER_CMD 0x0120   // 1 = titik 1 (asam), 2 = titik 2 (basa)
#define CAL_REGISTER_VALUE 0x0121 // round(pH_buffer x 100)
#define DEVIATION_REGISTER 0x0050 // round(delta_pH x 100), signed

// --- TITIK KALIBRASI & VERIFIKASI ---
#define BUFFER_PH4_VALUE 4.01  // titik kalibrasi 1 (ditulis ke sensor)
#define BUFFER_PH7_VALUE 6.86  // verifikasi saja
#define BUFFER_PH10_VALUE 9.18 // titik kalibrasi 2 (ditulis ke sensor)

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

#endif // CONFIG_H
