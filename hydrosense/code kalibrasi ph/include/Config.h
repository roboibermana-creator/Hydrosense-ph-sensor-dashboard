#ifndef CONFIG_H
#define CONFIG_H

// --- KREDENSIAL WIFI & FIREBASE ---
#define WIFI_SSID     "SPRM-CORP"
#define WIFI_PASSWORD "e$kr1mN@N@s3000"
#define DATABASE_URL  "https://ph-sensor-monitor-11e27-default-rtdb.asia-southeast1.firebasedatabase.app"
#define DEVICE_ID     "hydrosense-inlet-01"

// --- HARDWARE PIN (sensor pH Modbus RS485 via MAX485 di Serial2) ---
#define RS485_RX_PIN    16
#define RS485_TX_PIN    15
#define RS485_RE_DE_PIN 14
#define RS485_BAUD      9600          // datasheet: 9600 8N1

// --- MODBUS SENSOR: register pembacaan ---
// Alamat pH & suhu diambil dari hasil dump 'D' pada sensor Anda
// (0x0001 = 284 -> 28,4 C ; 0x0006 = 519 -> pH 5,19), BUKAN dari
// dokumen SEN0708 yang menaruh pH di 0x0000.
#define SENSOR_SLAVE_ID       1
#define PH_REGISTER_ADDRESS   0x0006  // pH x100, unsigned
#define PH_SCALE              100.0
#define TEMP_REGISTER_ADDRESS 0x0001  // suhu x10, signed
#define TEMP_SCALE            10.0
#define MV_REGISTER_ADDRESS   0x0007  // dugaan: tegangan elektroda x10 (mV); hanya untuk laporan
#define MV_SCALE              10.0

// --- MODBUS SENSOR: register kalibrasi (dokumen rumus_kalibrasi_ph.md) ---
// 0x0120/0x0121 ditulis dengan function 0x10, 0x0050 dengan 0x06.
// Belum diverifikasi pada sensor ini -> cek datasheet sebelum kalibrasi.
#define CAL_REGISTER_CMD      0x0120  // 1 = titik 1 (asam), 2 = titik 2 (basa)
#define CAL_REGISTER_VALUE    0x0121  // round(pH_buffer x 100)
#define DEVIATION_REGISTER    0x0050  // round(delta_pH x 100), signed

// --- TITIK KALIBRASI & VERIFIKASI ---
#define BUFFER_PH4_VALUE  4.01        // titik kalibrasi 1 (ditulis ke sensor)
#define BUFFER_PH7_VALUE  7.01        // verifikasi saja
#define BUFFER_PH10_VALUE 9.01        // titik kalibrasi 2 (ditulis ke sensor)

// --- GERBANG VALIDITAS ---
#define PH_VALID_MIN     0.0
#define PH_VALID_MAX    14.0
#define TEMP_VALID_MIN -10.0
#define TEMP_VALID_MAX  85.0

// --- AGREGASI (median N sampel) ---
#define MEDIAN_SAMPLES     5
#define SAMPLE_INTERVAL_MS 250
#define READ_PERIOD_MS     2000

// --- GERBANG STABILITAS (satuan pH, jendela 2 menit) ---
// Nama _MV dipertahankan karena dipakai PhStability.h; nilainya pH.
#define STB_SD_GOOD_MV     0.02   // sd <= 0,02 pH        -> good
#define STB_SD_OK_MV       0.05   // sd <= 0,05 pH        -> fair
#define STB_DRIFT_GOOD_MV  0.01   // |drift| <= 0,01 pH/menit
#define STB_DRIFT_OK_MV    0.03   // |drift| <= 0,03 pH/menit
#define STB_UNIT           "pH"
#define PH_BOARD_GAIN      1.0    // mV dari register sensor, tanpa penguat papan

// --- KRITERIA PENERIMAAN VERIFIKASI (bagian 4.4) ---
#define VERIF_SLOPE_TOL   0.05    // |m - 1|
#define VERIF_OFFSET_TOL  0.15    // |b|
#define VERIF_R2_MIN      0.99
#define VERIF_MAX_ERROR   0.15    // spesifikasi sensor

// --- BAKU MUTU (PermenLH No. 9/2006, pertambangan bijih nikel) ---
#define BAKU_MUTU_PH_MIN 6.0
#define BAKU_MUTU_PH_MAX 9.0

// --- PARAMETER KALIBRASI ---
#define CAL_WARMUP_MS 120000UL            // 2 menit warm-up

// --- PARAMETER LOGGING ---
#define FIREBASE_LOG_INTERVAL_MS 60000UL
#define FIREBASE_COMMAND_CHECK_MS 3000UL

#endif
