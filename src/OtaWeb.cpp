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
// BATASAN: hanya lewat WiFi. Lewat GSM, gsmHttp() menampung respons di String dan tidak bisa
// mengalirkan 1 MB biner; perlu unduhan berpotongan (HTTP Range) -- belum dibuat.
#include "OtaWeb.h"
#include "Config.h"
#include "HttpKlien.h"
#include "Network.h"
#include "OTA.h"
#include "Pompa.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>

static const unsigned long INTERVAL_CEK_OTA_MS = 20000UL;
static unsigned long timerCekOtaTerakhir = 0;
static unsigned long timerLogGsm = 0;

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

// true = firmware terpasang & siap reboot. Isi `galat` bila gagal.
static bool unduhDanPasang(const String &file, const String &shaHarapan, String &galat) {
  String url = String(SUPABASE_URL) + "/storage/v1/object/public/firmware/" + file;
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
  if (!Update.begin(ukuran)) {
    galat = String("Update.begin: ") + Update.errorString();
    http.end();
    return false;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[1024];
  int sisa = ukuran;
  unsigned long terakhirAda = millis();
  unsigned int persenLog = 101;
  bool ok = true;
  while (sisa > 0) {
    int ada = s->available();
    if (ada <= 0) {
      if (!http.connected() || millis() - terakhirAda > 20000UL) {
        galat = "unduhan terputus";
        ok = false;
        break;
      }
      delay(2);
      continue;
    }
    int n = s->readBytes(buf, min((int)sizeof(buf), min(ada, sisa)));
    if (n <= 0)
      continue;
    terakhirAda = millis();
    if (Update.write(buf, n) != (size_t)n) {
      galat = String("tulis flash: ") + Update.errorString();
      ok = false;
      break;
    }
    mbedtls_sha256_update(&sha, buf, n);
    sisa -= n;
    unsigned int persen = (unsigned)((ukuran - sisa) * 100L / ukuran);
    if (persen / 10 != persenLog / 10) {
      persenLog = persen;
      Serial.printf("[OTA-WEB] %u%%\n", persen);
    }
    delay(1); // beri waktu task lain/watchdog
  }
  http.end();
  uint8_t hasil[32];
  mbedtls_sha256_finish(&sha, hasil);
  mbedtls_sha256_free(&sha);
  if (!ok) {
    Update.abort();
    return false;
  }
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

  if (!wifiTersambung()) {
    // Belum bisa lewat GSM: biarkan 'pending' (tidak dibuang), cukup log jarang.
    if (timerLogGsm == 0 || millis() - timerLogGsm > 300000UL) {
      timerLogGsm = millis();
      Serial.println(F("[OTA-WEB] Perintah update menunggu WiFi (GSM belum didukung)."));
    }
    return;
  }
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
  bool ok = unduhDanPasang(file, sha, galat);
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
