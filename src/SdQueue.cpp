#include "SdQueue.h"
#include "Config.h"
#include "Network.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

// ---------------------------------------------------------------------
// Antrean data offline di SD card (lihat SdQueue.h).
// Format baris: {"ts":<epoch|0>,"ms":<millis>,"b":<id boot>,"d":{payload}}
//   ts = waktu pengukuran (detik epoch UTC) kalau jam sudah valid saat itu;
//   0  = jam belum valid (belum NTP). Kalau baris itu dari boot yang SAMA dan
//        NTP sudah berhasil sesudahnya, waktunya dihitung dari ms.
// ---------------------------------------------------------------------

static SPIClass sdSpi(FSPI);
static SemaphoreHandle_t sdMutex = nullptr;
static bool sdSiap = false;
static const char *FILE_ANTRE = "/queue.jsonl";
static const char *FILE_POS = "/queue.pos";
static const time_t EPOCH_VALID_MIN = 1700000000; // sekitar Nov 2023
static const unsigned long INTERVAL_TASK_MS = 3000;
static const unsigned long INTERVAL_COBA_MOUNT_MS = 30000;
static const int MAKS_BARIS_PER_PUTARAN = 5;

static uint32_t bootId = 0;
static time_t epochSaatBoot = 0; // 0 = belum diketahui
static bool ntpDimulai = false;

static bool waktuValid() { return time(nullptr) > EPOCH_VALID_MIN; }

static String isoUtc(time_t t) {
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  return String(buf);
}

bool sdQueueSiap() { return sdSiap; }

static bool kunci(uint32_t ms) {
  return sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static void lepas() {
  if (sdMutex)
    xSemaphoreGive(sdMutex);
}

static bool mountSd() {
  sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, sdSpi, 4000000)) {
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    return false;
  }
  Serial.printf("[SD] Siap. Tipe=%d, ukuran=%llu MB\n", (int)SD.cardType(),
                SD.cardSize() / (1024ULL * 1024ULL));
  return true;
}

static uint32_t bacaPos() {
  File f = SD.open(FILE_POS, FILE_READ);
  if (!f)
    return 0;
  String s = f.readStringUntil('\n');
  f.close();
  return (uint32_t)s.toInt();
}

static void tulisPos(uint32_t pos) {
  SD.remove(FILE_POS);
  File f = SD.open(FILE_POS, FILE_WRITE);
  if (!f)
    return;
  f.print(pos);
  f.close();
}

bool sdQueueTambah(const String &payload) {
  if (!sdSiap)
    return false;
  time_t now = time(nullptr);
  uint32_t ts = (now > EPOCH_VALID_MIN) ? (uint32_t)now : 0;
  String baris = "{\"ts\":" + String(ts) + ",\"ms\":" + String(millis()) +
                 ",\"b\":" + String(bootId) + ",\"d\":" + payload + "}\n";
  if (!kunci(3000))
    return false;
  File f = SD.open(FILE_ANTRE, FILE_APPEND);
  bool ok = false;
  if (f) {
    ok = f.print(baris) == baris.length();
    f.close(); // ditutup tiap tulis supaya aman kalau listrik mati
  }
  lepas();
  if (ok)
    Serial.println(F("[SD] Data disimpan ke antrean (offline/gagal kirim)."));
  else
    Serial.println(F("[SD] GAGAL menulis ke antrean."));
  return ok;
}

// Kirim satu baris tertua. Mengembalikan: 1 = sudah beres (terkirim atau
// dilewati permanen), 0 = gagal sementara (coba lagi nanti), -1 = antrean kosong.
static int kirimSatuBaris() {
  String baris;
  uint32_t selesaiPos = 0;

  if (!kunci(3000))
    return 0;
  File f = SD.open(FILE_ANTRE, FILE_READ);
  if (!f) {
    lepas();
    return -1;
  }
  uint32_t pos = bacaPos();
  if (pos >= f.size()) {
    f.close();
    // semua sudah terkirim -> bersihkan file
    SD.remove(FILE_ANTRE);
    SD.remove(FILE_POS);
    lepas();
    return -1;
  }
  f.seek(pos);
  baris = f.readStringUntil('\n');
  selesaiPos = f.position();
  f.close();
  lepas(); // lepas mutex selama HTTP supaya penulisan baru tidak tertahan

  baris.trim();
  bool lewati = false;
  String payload;
  if (baris.length() == 0) {
    lewati = true;
  } else {
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, baris)) {
      lewati = true; // baris rusak (mis. listrik mati saat menulis)
    } else {
      time_t ts = (time_t)(doc["ts"] | 0UL);
      uint32_t ms = doc["ms"] | 0UL;
      uint32_t b = doc["b"] | 0UL;
      if (ts == 0 && b == bootId && epochSaatBoot != 0)
        ts = epochSaatBoot + (time_t)(ms / 1000UL);
      JsonObject d = doc["d"];
      if (d.isNull()) {
        lewati = true;
      } else {
        if (ts > EPOCH_VALID_MIN)
          d["timestamp"] = isoUtc(ts);
        serializeJson(d, payload);
      }
    }
  }

  if (!lewati) {
    int kode = -1;
    bool ok = kirimPayloadMentah(payload, kode);
    if (!ok) {
      // 4xx (selain 408/429) = payload ditolak permanen -> lewati supaya
      // antrean tidak macet selamanya; selain itu coba lagi nanti.
      bool permanen = kode >= 400 && kode < 500 && kode != 408 && kode != 429;
      if (!permanen) {
        Serial.printf("[SD] Kirim ulang gagal (HTTP %d), coba lagi nanti.\n",
                      kode);
        return 0;
      }
      Serial.printf("[SD] Baris ditolak server (HTTP %d), dilewati.\n", kode);
    }
  }

  if (!kunci(3000))
    return 0;
  tulisPos(selesaiPos);
  lepas();
  return 1;
}

static void kirimAntrean() {
  for (int i = 0; i < MAKS_BARIS_PER_PUTARAN; i++) {
    int r = kirimSatuBaris();
    if (r != 1)
      break;
  }
}

void sdQueueStatus() {
  Serial.println(F("\n=== ANTREAN SD ==="));
  Serial.printf("SD card      : %s\n", sdSiap ? "siap" : "TIDAK terdeteksi");
  Serial.printf("Waktu NTP    : %s\n",
                waktuValid() ? isoUtc(time(nullptr)).c_str() : "belum sinkron");
  if (sdSiap && kunci(3000)) {
    File f = SD.open(FILE_ANTRE, FILE_READ);
    if (f) {
      uint32_t ukuran = f.size();
      f.close();
      uint32_t pos = bacaPos();
      Serial.printf("Antrean      : %u byte total, %u byte belum terkirim\n",
                    (unsigned)ukuran, (unsigned)(ukuran > pos ? ukuran - pos : 0));
    } else {
      Serial.println(F("Antrean      : kosong"));
    }
    lepas();
  }
  Serial.println(F("=================="));
}

static void taskSdQueue(void *) {
  unsigned long terakhirCobaMount = 0;
  bool pernahCoba = false;
  for (;;) {
    if (!sdSiap &&
        (!pernahCoba || millis() - terakhirCobaMount >= INTERVAL_COBA_MOUNT_MS)) {
      pernahCoba = true;
      terakhirCobaMount = millis();
      if (kunci(3000)) {
        sdSiap = mountSd();
        lepas();
        if (!sdSiap)
          Serial.println(F("[SD] Tidak terdeteksi (cek kartu/pin). Data offline "
                           "hanya tertahan di RAM."));
      }
    }

    if (wifiTersambung() && !ntpDimulai) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      ntpDimulai = true;
    }
    if (waktuValid() && epochSaatBoot == 0) {
      epochSaatBoot = time(nullptr) - (time_t)(millis() / 1000UL);
      Serial.println(F("[SD] Jam tersinkron (NTP)."));
    }

    if (sdSiap && internetSiap())
      kirimAntrean();

    vTaskDelay(pdMS_TO_TICKS(INTERVAL_TASK_MS));
  }
}

void sdQueueMulai() {
  if (!ENABLE_SD_QUEUE) {
    Serial.println(F("[SD] Antrean SD dimatikan (ENABLE_SD_QUEUE 0)."));
    return; // sdSiap tetap false -> sdQueueTambah() menolak, tidak ada mount
  }
  bootId = esp_random();
  sdMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(taskSdQueue, "sdQueue", 12288, nullptr, 1, nullptr, 0);
}
