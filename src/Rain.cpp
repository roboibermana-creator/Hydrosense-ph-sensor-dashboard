// Rain gauge tipping bucket: kontak reed antara RAIN_PIN dan GND (INPUT_PULLUP).
//  - ISR menghitung ketukan (FALLING) dengan debounce RAIN_DEBOUNCE_MS.
//  - Task tiap 60 detik memindahkan hitungan ke ember per-menit (60 ember =
//    jendela bergulir 1 jam -> mm/jam) dan ke total hari ini.
//  - Total hari ini disimpan di NVS (bertahan saat reboot) dan di-nol-kan pada
//    tengah malam waktu lokal (butuh jam NTP valid; sebelum itu hanya menghitung).
//  - Hasil dikirim ke "Hydrosense table" (rain_mm_per_hour, rain_mm_today),
//    kolom yang sudah dibaca halaman Rain Gauge dashboard.
#include "Rain.h"
#include "App.h"
#include "SdQueue.h"
#include <Preferences.h>
#include <time.h>

static volatile uint32_t tipsTotal = 0;
static volatile uint32_t tipTerakhirMs = 0;

static void IRAM_ATTR isrRain() {
  uint32_t now = millis();
  if (now - tipTerakhirMs >= RAIN_DEBOUNCE_MS) {
    tipTerakhirMs = now;
    tipsTotal++;
  }
}

static const int EMBER = 60;
static uint16_t ember[EMBER]; // ketukan per menit, 60 menit terakhir
static int emberIdx = 0;
static uint32_t tipsSnapshot = 0;
static uint32_t tipsHariIni = 0;
static uint32_t hariSekarang = 0; // indeks hari lokal; 0 = belum diketahui
static uint32_t hariTersimpan = 0;
static Preferences rainPrefs;
static portMUX_TYPE rainMux = portMUX_INITIALIZER_UNLOCKED;
static float mmJam = 0, mmHari = 0;
static float mmPerTip = RAIN_MM_PER_TIP; // bisa diubah dari Serial (disimpan di NVS)
static uint32_t tipsKalibrasiAwal = 0;
static bool kalibrasiJalan = false;

float rainMmPerJam() { return mmJam; }
float rainMmHariIni() { return mmHari; }

static const time_t EPOCH_VALID_MIN = 1700000000;

static void simpanNvs() {
  rainPrefs.putUInt("hari", hariSekarang ? hariSekarang : hariTersimpan);
  rainPrefs.putUInt("tips", tipsHariIni);
}

// Hitung ulang mm/jam & mm hari ini dari jumlah ketukan; return ketukan 60 menit terakhir.
static uint32_t hitungUlang() {
  uint32_t tipsJam = 0;
  for (int i = 0; i < EMBER; i++)
    tipsJam += ember[i];
  mmJam = tipsJam * mmPerTip;
  mmHari = tipsHariIni * mmPerTip;
  return tipsJam;
}

static void perbarui(unsigned long &kirimBerikutnya) {
  uint32_t total;
  portENTER_CRITICAL(&rainMux);
  total = tipsTotal;
  portEXIT_CRITICAL(&rainMux);
  uint32_t delta = total - tipsSnapshot;
  tipsSnapshot = total;

  // Batas hari: nol-kan total saat hari lokal berganti (jam harus valid).
  bool hariBerganti = false;
  time_t now = time(nullptr);
  if (now > EPOCH_VALID_MIN) {
    uint32_t hari = (uint32_t)((now + RAIN_TZ_OFFSET_S) / 86400);
    if (hariSekarang == 0) {
      if (hariTersimpan != hari) { tipsHariIni = 0; hariBerganti = true; }
      hariSekarang = hari;
    } else if (hari != hariSekarang) {
      tipsHariIni = 0;
      hariSekarang = hari;
      hariBerganti = true;
    }
  }

  ember[emberIdx] = (uint16_t)min<uint32_t>(delta, 65535);
  emberIdx = (emberIdx + 1) % EMBER;
  tipsHariIni += delta;

  uint32_t tipsJam = hitungUlang();

  if (delta > 0 || hariBerganti)
    simpanNvs();
  if (delta > 0)
    Serial.printf("[RAIN] +%u ketukan | %.1f mm/jam | hari ini %.1f mm\n",
                  (unsigned)delta, mmJam, mmHari);

  // Kirim tiap menit selagi hujan (ada ketukan dalam 1 jam), tiap 5 menit kalau kering.
  if (millis() < kirimBerikutnya)
    return;
  kirimBerikutnya = millis() + (tipsJam > 0 ? 60000UL : 300000UL);
  if (!internetSiap())
    return;
  String payload = "{\"rain_mm_per_hour\":" + String(mmJam, 1) +
                   ",\"rain_mm_today\":" + String(mmHari, 1) + "}";
  int kode = -1;
  if (!kirimPayloadMentah(payload, kode)) {
    Serial.printf("[RAIN] gagal kirim (HTTP %d)\n", kode);
    sdQueueTambah(payload); // hanya berhasil bila antrean SD aktif
  }
}

static void taskRain(void *) {
  unsigned long kirimBerikutnya = 0;
  TickType_t terakhir = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&terakhir, pdMS_TO_TICKS(60000));
    perbarui(kirimBerikutnya);
  }
}

void rainMulai() {
  rainPrefs.begin("rain", false);
  hariTersimpan = rainPrefs.getUInt("hari", 0);
  tipsHariIni = rainPrefs.getUInt("tips", 0);
  mmPerTip = rainPrefs.getFloat("mmtip", RAIN_MM_PER_TIP);
  mmHari = tipsHariIni * mmPerTip;
  pinMode(RAIN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RAIN_PIN), isrRain, FALLING);
  Serial.printf("[RAIN] siap di GPIO%d, %.2f mm/ketukan, hari ini (tersimpan) %.1f mm\n",
                RAIN_PIN, (double)mmPerTip, mmHari);
  xTaskCreatePinnedToCore(taskRain, "rain", 8192, nullptr, 1, nullptr, 0);
}

// ---------------- Perintah Serial (dipanggil dari SerialMenu.cpp) ----------------
static uint32_t ketukanTotal() {
  portENTER_CRITICAL(&rainMux);
  uint32_t t = tipsTotal;
  portEXIT_CRITICAL(&rainMux);
  return t;
}

float rainMmPerTip() { return mmPerTip; }

bool rainSetMmPerTip(float mm) {
  if (!(mm >= 0.01f && mm <= 5.0f))
    return false;
  mmPerTip = mm;
  rainPrefs.putFloat("mmtip", mm);
  hitungUlang();
  return true;
}

void rainNolkanHariIni() {
  tipsHariIni = 0;
  hitungUlang();
  simpanNvs();
}

void rainKalibrasiMulai() {
  tipsKalibrasiAwal = ketukanTotal();
  kalibrasiJalan = true;
}

bool rainKalibrasiJalan() { return kalibrasiJalan; }

uint32_t rainKetukanKalibrasi() { return ketukanTotal() - tipsKalibrasiAwal; }

void rainKalibrasiSelesai() { kalibrasiJalan = false; }

void rainCetakStatus() {
  Serial.printf("[RAIN] GPIO%d | %.3f mm/ketukan | total ketukan sejak boot %u | "
                "hari ini %u ketukan = %.1f mm | 60 menit terakhir %.1f mm\n",
                RAIN_PIN, (double)mmPerTip, (unsigned)ketukanTotal(),
                (unsigned)tipsHariIni, (double)mmHari, (double)mmJam);
}
