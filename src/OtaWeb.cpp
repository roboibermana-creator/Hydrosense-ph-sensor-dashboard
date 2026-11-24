// Update firmware lewat Supabase Storage. Alur:
//   1. Dashboard memilih firmware di bucket publik "firmware" dan menulis
//      device_commands {type:'ota', status:'pending', params:{file, sha256, versi}}.
//   2. Alat membaca baris itu, menandainya 'proses', mengunduh
//      <SUPABASE_URL>/storage/v1/object/public/firmware/<file> lewat HTTPS, menulis ke
//      partisi OTA cadangan sambil menghitung SHA-256, dan HANYA bila hash cocok memanggil
//      Update.end() (yang mengganti partisi boot). Gagal di tengah = firmware lama tetap jalan.
//   3. Hasil ('sukses'/'gagal') ditulis ke baris command SEBELUM reboot.
// Keamanan: alat hanya mengunduh dari bucket firmware milik proyek ini (URL dibangun dari
// SUPABASE_URL + nama file yang dicek karakternya), bukan URL bebas dari command. Penulisan ke
// bucket harus dibatasi pemilik (lihat migrasi 0015).
// Jalur: WiFi bila tersambung (satu aliran), selain itu GSM (potongan HTTP Range, gsmHttpRange()).
#include "OtaWeb.h"
#include "Config.h"
#include "HttpKlien.h"
#include "Gsm.h"
#include "Network.h"
#include "OTA.h"
#include "Pompa.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>

static const unsigned long INTERVAL_CEK_OTA_MS = 20000UL;
static unsigned long timerCekOtaTerakhir = 0;

static bool namaFileAman(const String &f) {
  if (f.length() == 0 || f.length() > 80 || !f.endsWith(".bin"))
    return false;
  for (size_t i = 0; i < f.length(); i++) {
    char c = f[i];
    bool ok = isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_';
    if (!ok)
      return false;
  }
  return f.indexOf("..") < 0;
}

static bool patchCommand(long id, const char *status, const String &pesan) {
  HttpKlien http;
  if (!http.begin(String(SUPABASE_URL) + "/rest/v1/device_commands?id=eq." + String(id)))
    return false;
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  String p = pesan;
  p.replace("\"", "'");
  int kode = http.PATCH(String("{\"status\":\"") + status + "\",\"pesan\":\"" + p + "\"}");
  http.end();
  return kode == 200 || kode == 204;
}

static String hexSha(const uint8_t *h) {
  static const char *d = "0123456789abcdef";
  String s;
  for (int i = 0; i < 32; i++) {
    s += d[h[i] >> 4];
    s += d[h[i] & 15];
  }
  return s;
}

// Penulis firmware: tulis ke partisi OTA cadangan sambil menghitung SHA-256. Dipakai bersama
// oleh jalur WiFi (satu aliran) dan GSM (potongan Range).
struct Pemasang {
  mbedtls_sha256_context sha;
  bool dimulai = false;
  void mulai(size_t ukuran, String &galat, bool &ok) {
    ok = Update.begin(ukuran);
    if (!ok) {
      galat = String("Update.begin: ") + Update.errorString();
      return;
    }
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    dimulai = true;
  }
  bool tulis(const uint8_t *d, size_t n, String &galat) {
    if (Update.write((uint8_t *)d, n) != n) {
      galat = String("tulis flash: ") + Update.errorString();
      return false;
    }
    mbedtls_sha256_update(&sha, d, n);
    return true;
  }
  void batal() {
    if (dimulai) {
      mbedtls_sha256_free(&sha);
      dimulai = false;
    }
    Update.abort();
  }
  // Periksa hash lalu ganti partisi boot hanya bila cocok.
  bool selesai(const String &shaHarapan, String &galat) {
    uint8_t hasil[32];
    mbedtls_sha256_finish(&sha, hasil);
    mbedtls_sha256_free(&sha);
    dimulai = false;
    if (!hexSha(hasil).equalsIgnoreCase(shaHarapan)) {
      Update.abort();
      galat = "SHA-256 tidak cocok";
      return false;
    }
    if (!Update.end(false)) {
      galat = String("Update.end: ") + Update.errorString();
      return false;
    }
    return true;
  }
};

static void logPersen(long selesai, long total) {
  static unsigned int persenLog = 101;
  if (selesai <= 0)
    persenLog = 101;
  unsigned int persen = (unsigned)(selesai * 100L / total);
  if (persen / 10 != persenLog / 10) {
    persenLog = persen;
    Serial.printf("[OTA-WEB] %u%%\n", persen);
  }
}

static const char *PATH_FIRMWARE = "/storage/v1/object/public/firmware/";

// Jalur WiFi: satu aliran HTTPS langsung ke partisi OTA.
static bool unduhWifi(const String &file, const String &shaHarapan, String &galat) {
  String url = String(SUPABASE_URL) + PATH_FIRMWARE + file;
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(url)) {
    galat = "begin gagal";
    return false;
  }
  int kode = http.GET();
  if (kode != 200) {
    galat = "HTTP " + String(kode);
    http.end();
    return false;
  }
  int ukuran = http.getSize();
  if (ukuran <= 0) {
    galat = "ukuran tidak diketahui";
    http.end();
    return false;
  }
  Pemasang pm;
  bool ok;
  pm.mulai(ukuran, galat, ok);
  if (!ok) {
    http.end();
    return false;
  }
  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[1024];
  int sisa = ukuran;
  unsigned long terakhirAda = millis();
  logPersen(0, ukuran);
  while (sisa > 0) {
    int ada = s->available();
    if (ada <= 0) {
      if (!http.connected() || millis() - terakhirAda > 20000UL) {
        galat = "unduhan terputus";
        http.end();
        pm.batal();
        return false;
      }
      delay(2);
      continue;
    }
    int n = s->readBytes(buf, min((int)sizeof(buf), min(ada, sisa)));
    if (n <= 0)
      continue;
    terakhirAda = millis();
    if (!pm.tulis(buf, n, galat)) {
      http.end();
      pm.batal();
      return false;
    }
    sisa -= n;
    logPersen(ukuran - sisa, ukuran);
    delay(1); // beri waktu task lain/watchdog
  }
  http.end();
  return pm.selesai(shaHarapan, galat);
}

// Jalur GSM: unduh per potongan (HTTP Range). Tiap potongan langsung ditulis ke flash dan
// di-hash, jadi tidak ada penampung besar. Potongan yang gagal diulang tanpa mulai dari awal.
// BELUM TERUJI di modem sungguhan (modem belum menjawab saat ini dibuat). Mengandalkan
// Supabase Storage membalas 206 untuk header Range.
static const size_t POTONGAN_GSM = 8192;
static bool unduhGsm(const String &file, const String &shaHarapan, String &galat) {
  String path = String(PATH_FIRMWARE) + file;
  uint8_t *buf = (uint8_t *)malloc(POTONGAN_GSM);
  if (!buf) {
    galat = "memori habis (penampung potongan)";
    return false;
  }
  Pemasang pm;
  long total = 0, posisi = 0;
  bool mulai = false;
  bool ok = true;
  while (!mulai || posisi < total) {
    long sampai = posisi + (long)POTONGAN_GSM - 1;
    size_t dapat = 0;
    int kode = -1;
    for (int coba = 0; coba < 5; coba++) {
      long totalBaru = total;
      kode = gsmHttpRange(path, posisi, sampai, buf, POTONGAN_GSM, &dapat, &totalBaru);
      if (kode == 206) {
        total = totalBaru;
        break;
      }
      if (kode == 200 || (kode >= 400 && kode < 500))
        break; // Range tidak didukung / berkas tidak ada: mengulang percuma
      Serial.printf("[OTA-WEB] potongan @%ld gagal (kode %d), ulang %d/5\n", posisi, kode, coba + 1);
      delay(2000);
    }
    if (kode != 206) {
      galat = kode == 200 ? "server mengabaikan Range (butuh 206)"
                          : "potongan @" + String(posisi) + " gagal, HTTP " + String(kode);
      ok = false;
      break;
    }
    if (!mulai) {
      if (total <= 0 || total > 3000000L) {
        galat = "ukuran firmware tidak valid: " + String(total);
        ok = false;
        break;
      }
      pm.mulai((size_t)total, galat, ok);
      if (!ok)
        break;
      mulai = true;
      logPersen(0, total);
    }
    if (!pm.tulis(buf, dapat, galat)) {
      ok = false;
      break;
    }
    posisi += (long)dapat;
    logPersen(posisi, total);
    delay(1);
  }
  free(buf);
  if (!ok) {
    if (mulai)
      pm.batal();
    return false;
  }
  return pm.selesai(shaHarapan, galat);
}

void cekPerintahOtaDariWeb() {
  if (millis() - timerCekOtaTerakhir < INTERVAL_CEK_OTA_MS)
    return;
  timerCekOtaTerakhir = millis();
  if (!jaringanSiap())
    return;

  HttpKlien http;
  String url = String(SUPABASE_URL) +
               "/rest/v1/device_commands?select=id,params&type=eq.ota&status=eq.pending"
               "&device_id=eq." + String(DEVICE_ID) + "&order=id.desc&limit=1";
  if (!http.begin(url))
    return;
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  int kode = http.GET();
  String respon = kode == 200 ? http.getString() : String();
  http.end();
  if (kode != 200)
    return;
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, respon) || !doc.is<JsonArray>())
    return;
  // Pengaman boot: perintah update yang SUDAH menunggu sebelum alat menyala dibuang
  // (kedaluwarsa), supaya firmware lama tidak terpasang sendiri saat alat nyala kembali.
  // Hanya perintah yang dikirim operator SETELAH boot yang dijalankan.
  static bool bootDiperiksa = false;
  if (!bootDiperiksa) {
    bootDiperiksa = true;
    if (doc.size() > 0) {
      long idLama = doc[0]["id"] | -1L;
      if (idLama >= 0) {
        patchCommand(idLama, "gagal", "kedaluwarsa: dikirim sebelum alat boot, kirim ulang");
        Serial.println(F("[OTA-WEB] Perintah update lama (sebelum boot) dibuang."));
      }
    }
    return;
  }
  if (doc.size() == 0)
    return;
  long id = doc[0]["id"] | -1L;
  String file = String((const char *)(doc[0]["params"]["file"] | ""));
  String sha = String((const char *)(doc[0]["params"]["sha256"] | ""));
  String versi = String((const char *)(doc[0]["params"]["versi"] | "?"));
  if (id < 0)
    return;

  if (!namaFileAman(file) || sha.length() != 64) {
    patchCommand(id, "gagal", "parameter tidak valid (file/sha256)");
    return;
  }

  Serial.printf("\n[OTA-WEB] Update ke versi %s (%s)\n", versi.c_str(), file.c_str());
  // Tandai dulu supaya command ini tidak diulang tanpa henti kalau alat reset di tengah jalan.
  patchCommand(id, "proses", "mengunduh " + versi);
  pumpOFF(); // aktuator aman selama flash; valve dibiarkan di posisinya
  otaTandaiSibuk(true);

  String galat;
  bool lewatWifi = wifiTersambung();
  Serial.printf("[OTA-WEB] Jalur unduhan: %s\n", lewatWifi ? "WiFi" : "GSM (potongan Range)");
  bool ok = lewatWifi ? unduhWifi(file, sha, galat) : unduhGsm(file, sha, galat);
  otaTandaiSibuk(false);
  if (!ok) {
    Serial.printf("[OTA-WEB] GAGAL: %s -- firmware lama tetap dipakai\n", galat.c_str());
    patchCommand(id, "gagal", galat);
    return;
  }
  patchCommand(id, "sukses", "terpasang " + versi + ", reboot");
  Serial.println(F("[OTA-WEB] Selesai, reboot..."));
  delay(500);
  ESP.restart();
}
