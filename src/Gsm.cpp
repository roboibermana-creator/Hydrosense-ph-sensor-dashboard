// GSM/4G node HydroSense: modem SIM7600 lewat UART (TinyGSM). Cadangan WiFi di lapangan.
//   - gsmTask    : menyalakan modem, mendaftar ke jaringan, menyambung data (APN), sinkron jam,
//                  lalu menjaga koneksi (cek tiap 15 dtk, sambung ulang bila putus).
//   - gsmHttp()  : satu permintaan HTTPS ke Supabase (koneksi dipertahankan agar hemat waktu/kuota).
#include "Gsm.h"
#include "Config.h"
#include "Network.h"

#if PAKAI_GSM

#define TINY_GSM_MODEM_SIM7600
// Bawaan 0 = delay(0): tidak memberi waktu ke task IDLE, jadi saat menunggu balasan modem (mis.
// modem belum menyala) task GSM berprioritas 2 memonopoli CPU dan task watchdog mereset board.
#define TINY_GSM_YIELD_MS 2
#define TINY_GSM_RX_BUFFER 1024
#include <SSLClient.h>
#include <TinyGsmClient.h>
#include "certificates.h"
#include <sys/time.h>
#include <time.h>

extern const char *SUPABASE_URL;

static TinyGsm modem(GSM_SERIAL);
// TCP biasa lewat modem, TLS dikerjakan ESP32 (BearSSL, trust anchor di certificates.h --
// pola yang sama dengan gateway). Modem SIM7600 di TinyGSM belum mendukung SSL.
// TinyGSM membuka soket dengan batas tunggu 75 dtk: satu koneksi yang macet menahan modem (dan
// semua task yang antre) lebih dari semenit. Dipersingkat ke 20 dtk.
class TcpCepat : public TinyGsmClient {
public:
  explicit TcpCepat(TinyGsm &m) : TinyGsmClient(m) {}
  int connect(const char *host, uint16_t port) override {
    return TinyGsmClient::connect(host, port, 20);
  }
  int connect(IPAddress ip, uint16_t port) override {
    return TinyGsmClient::connect(ip, port, 20);
  }
};
static TcpCepat tcp(modem);
static SSLClient klien(tcp, TAs, (size_t)TAs_NUM, GSM_SSL_ENTROPY_PIN, 1, SSLClient::SSL_WARN);
static SemaphoreHandle_t gsmMutex = nullptr;
static volatile bool online = false;
static volatile int rssiDbm = 0;
static char statusTeks[96] = "belum dimulai";
// Alasan gagal request terakhir (ditampilkan perintah 'U' tanpa perlu menunggu modem bebas).
static const char *alasanGagal = "belum ada request";
static unsigned long logGagalMs = 0;
// Siapa yang sedang memegang modem & sejak kapan (diagnosa "modem terus sibuk").
static char pemegang[72] = "-";
static volatile unsigned long pemegangSejak = 0;
static char alasanBuf[160];
static void pegang(const char *siapa) {
  strncpy(pemegang, siapa, sizeof(pemegang) - 1);
  pemegang[sizeof(pemegang) - 1] = 0;
  pemegangSejak = millis();
}
static void lepas() {
  strcpy(pemegang, "-");
  pemegangSejak = 0;
}

bool gsmSiap() { return online; }
int gsmRssiDbm() { return rssiDbm; }
const char *gsmStatusTeks() { return statusTeks; }

static void setStatus(const char *t) {
  strncpy(statusTeks, t, sizeof(statusTeks) - 1);
  statusTeks[sizeof(statusTeks) - 1] = 0;
}

static int csqKeDbm(int csq) { // 0..31 -> dBm; 99 = tidak diketahui
  return (csq >= 0 && csq <= 31) ? -113 + 2 * csq : 0;
}

static void tekanPwrKey() {
#if GSM_PWRKEY_PIN >= 0
  pinMode(GSM_PWRKEY_PIN, OUTPUT);
  digitalWrite(GSM_PWRKEY_PIN, GSM_PWRKEY_AKTIF_LOW ? HIGH : LOW);
  delay(100);
  digitalWrite(GSM_PWRKEY_PIN, GSM_PWRKEY_AKTIF_LOW ? LOW : HIGH);
  delay(GSM_PWRKEY_MS);
  digitalWrite(GSM_PWRKEY_PIN, GSM_PWRKEY_AKTIF_LOW ? HIGH : LOW);
#endif
}

static bool tungguAT(unsigned long batasMs) {
  unsigned long t0 = millis();
  while (millis() - t0 < batasMs) {
    if (modem.testAT(1000))
      return true;
  }
  return false;
}

// Jam sistem diset dari jam jaringan seluler (dipakai "hari ini" rain gauge dan SD).
static void sinkronJam() {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  float tz = 0;
  if (!modem.getNetworkTime(&y, &mo, &d, &h, &mi, &s, &tz) || y < 2024)
    return;
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min = mi;
  t.tm_sec = s;
  // Zona sistem ESP32 = UTC, jadi mktime() = epoch dari waktu lokal jaringan; kurangi offset.
  time_t epoch = mktime(&t) - (time_t)(tz * 3600.0f);
  struct timeval tv = {epoch, 0};
  settimeofday(&tv, nullptr);
  Serial.printf("[GSM] Jam disinkron dari jaringan: %04d-%02d-%02d %02d:%02d:%02d (tz %+.1f)\n", y,
                mo, d, h, mi, s, tz);
}

static bool sambungkan() {
  setStatus("menyalakan modem");
  if (!modem.testAT(1500)) { // modem belum menjawab: tekan PWRKEY lalu tunggu boot
    tekanPwrKey();
    if (!tungguAT(25000)) {
      setStatus("modem TIDAK menjawab AT (cek kabel TX/RX, catu, PWRKEY)");
      return false;
    }
  }
  modem.sendAT("+CTZU=1"); // jam otomatis dari jaringan
  modem.waitResponse();
  if (!modem.init()) {
    setStatus("modem.init() gagal");
    return false;
  }
  setStatus("mencari jaringan");
  if (!modem.waitForNetwork(90000L)) {
    setStatus("tidak ada sinyal jaringan (cek SIM/antena)");
    return false;
  }
  setStatus("menyambung data (APN)");
  if (!modem.gprsConnect(GSM_APN, GSM_APN_USER, GSM_APN_PASS)) {
    setStatus("gprsConnect gagal (cek APN/kuota)");
    return false;
  }
  rssiDbm = csqKeDbm(modem.getSignalQuality());
  Serial.printf("[GSM] Tersambung. Operator: %s | IP: %s | sinyal %d dBm\n",
                modem.getOperator().c_str(), modem.localIP().toString().c_str(), (int)rssiDbm);
  sinkronJam();
  // Modem tidak ikut reset saat ESP32 reset: soket sesi sebelumnya bisa masih menyimpan data TLS lama
  // (terlihat sebagai "1728 byte tersedia" di soket 0). Tutup semua soket supaya sesi baru bersih.
  for (int mux = 0; mux < 2; mux++) {
    modem.sendAT(String("+CIPCLOSE=") + mux);
    modem.waitResponse(3000L);
  }
  setStatus("tersambung");
  return true;
}

// ---------------- GNSS (GPS) bawaan SIM7600 ----------------
// Alat diam di satu tempat: cari posisi tiap 30 dtk sampai dapat fix, kirim ke tabel devices,
// lalu perbarui tiap GPS_INTERVAL_MS saja (hemat daya dan kuota).
static const unsigned long GPS_COBA_MS = 30000UL;
static const unsigned long GPS_INTERVAL_MS = 6UL * 3600UL * 1000UL;
static const unsigned long GPS_ULANG_KIRIM_MS = 300000UL;
static bool gpsMenyala = false;
static volatile bool posisiAda = false;
static float posLat = 0, posLon = 0, posAlt = 0;
static bool posisiBelumTerkirim = false;
static unsigned long gpsBerikut = 0;

bool gsmPosisi(float *lat, float *lon) {
  if (!posisiAda)
    return false;
  *lat = posLat;
  *lon = posLon;
  return true;
}

static void gpsLangkah() {
  if ((long)(millis() - gpsBerikut) < 0)
    return;
  if (posisiBelumTerkirim) { // fix sudah ada, pengiriman ke Supabase gagal: ulangi
    if (kirimPosisiKeSupabase(posLat, posLon, posAlt)) {
      posisiBelumTerkirim = false;
      gpsBerikut = millis() + GPS_INTERVAL_MS;
      Serial.printf("[GPS] Posisi terkirim: %.6f, %.6f\n", posLat, posLon);
    } else {
      gpsBerikut = millis() + GPS_ULANG_KIRIM_MS;
      Serial.println(F("[GPS] Kirim posisi GAGAL (kolom latitude/longitude sudah ada di devices?)"));
    }
    return;
  }
  gpsBerikut = millis() + GPS_COBA_MS;
  float lat = 0, lon = 0, alt = 0;
  bool fix = false;
  if (xSemaphoreTake(gsmMutex, pdMS_TO_TICKS(10000)) != pdTRUE)
    return;
  pegang("gsmTask: GPS");
  if (!gpsMenyala)
    gpsMenyala = modem.enableGPS();
  if (gpsMenyala)
    fix = modem.getGPS(&lat, &lon, nullptr, &alt);
  lepas();
  xSemaphoreGive(gsmMutex);
  if (!fix || (lat == 0 && lon == 0))
    return; // belum dapat satelit (antena GNSS terpasang dan menghadap langit?)
  posLat = lat;
  posLon = lon;
  posAlt = alt;
  posisiAda = true;
  Serial.printf("[GPS] Fix: %.6f, %.6f (alt %.0f m)\n", lat, lon, alt);
  posisiBelumTerkirim = true;
  gpsBerikut = 0; // kirim sekarang
}

static void gsmTask(void *) {
  // Buffer terima bawaan 256 byte: balasan modem yang besar (sertifikat TLS, >1 KB) bisa kehilangan
  // byte, lalu TinyGSM menunggu byte yang tidak pernah datang (macet tanpa batas). Harus sebelum begin().
  GSM_SERIAL.setRxBufferSize(4096);
  GSM_SERIAL.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  unsigned long cekBerikut = 0;
  // klien.stop() -> SSLClient::connected() -> TinyGSM maintain() memanggil
  // parseInt() yang busy-wait tanpa yield (tiap soket bisa sampai 1 dtk) kalau
  // modem diam. Itu memicu task_wdt di core 0 (abort). Jadi stop() HANYA bila
  // sebelumnya pernah benar-benar online.
  bool pernahOnline = false;
  for (;;) {
    if (!online) {
      xSemaphoreTake(gsmMutex, portMAX_DELAY);
      if (pernahOnline) {
        klien.stop();
        pernahOnline = false;
      }
      pegang("gsmTask: sambungkan");
      bool ok = sambungkan();
      lepas();
      xSemaphoreGive(gsmMutex);
      if (ok) {
        online = true;
        pernahOnline = true;
        cekBerikut = millis() + 15000UL;
      } else {
        Serial.printf("[GSM] %s -- coba lagi 20 dtk\n", statusTeks);
        vTaskDelay(pdMS_TO_TICKS(20000));
      }
      continue;
    }
    if ((long)(millis() - cekBerikut) >= 0) {
      cekBerikut = millis() + 15000UL;
      if (xSemaphoreTake(gsmMutex, pdMS_TO_TICKS(30000)) == pdTRUE) {
        pegang("gsmTask: cek koneksi");
        rssiDbm = csqKeDbm(modem.getSignalQuality());
        if (!modem.isNetworkConnected() || !modem.isGprsConnected()) {
          Serial.println(F("[GSM] Koneksi data putus -- menyambung ulang"));
          setStatus("putus, menyambung ulang");
          online = false;
        }
        lepas();
        xSemaphoreGive(gsmMutex);
      }
    }
    gpsLangkah();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void gsmMulai() {
  if (gsmMutex)
    return;
  gsmMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(gsmTask, "gsm", 8192, nullptr, 2, nullptr, 0);
}

void gsmCetakStatus() {
  Serial.println(F("\n=== STATUS GSM (SIM7600) ==="));
  Serial.printf("Pin             : TX ESP=GPIO%d (-> RXD modem) RX ESP=GPIO%d (<- TXD modem) PWRKEY=GPIO%d\n",
                GSM_TX_PIN, GSM_RX_PIN, GSM_PWRKEY_PIN);
  Serial.printf("APN             : %s\n", GSM_APN);
  Serial.printf("Status task     : %s\n", statusTeks);
  Serial.printf("Data tersambung : %s\n", online ? "YA" : "TIDAK");
  if (!gsmMutex) {
    Serial.println(F("Task GSM belum dibuat."));
    return;
  }
  {
    // Info yang tidak butuh modem: tetap tampil walau modem sedang sibuk.
    time_t t = time(nullptr);
    Serial.printf("Jam sistem      : %s\n",
                  t > 1700000000L ? "tersinkron" : "BELUM tersinkron (semua request ke Supabase gagal)");
    Serial.printf("Sinyal terakhir : %d dBm\n", (int)rssiDbm);
    Serial.printf("Gagal terakhir  : %s\n", alasanGagal);
    unsigned long sejak = pemegangSejak;
    if (sejak)
      Serial.printf("Pemegang modem  : %s (sudah %lu dtk)\n", pemegang, (millis() - sejak) / 1000UL);
    else
      Serial.println(F("Pemegang modem  : tidak ada (bebas)"));
  }
  if (xSemaphoreTake(gsmMutex, pdMS_TO_TICKS(8000)) != pdTRUE) {
    Serial.println(F("Modem sedang sibuk (task lain menahan modem), coba lagi."));
    return;
  }
  bool at = modem.testAT(1500);
  Serial.printf("Modem jawab AT  : %s\n",
                at ? "YA" : "TIDAK (cek TX/RX, GND bersama, catu modem, PWRKEY)");
  if (at) {
    int sim = modem.getSimStatus();
    Serial.printf("Kartu SIM       : %s\n",
                  sim == 1 ? "SIAP" : (sim == 2 ? "TERKUNCI PIN" : (sim == 0 ? "ERROR/tidak terbaca" : "belum siap (kode lain)")));
    int csq = modem.getSignalQuality();
    Serial.printf("Sinyal          : CSQ %d (%d dBm)\n", csq, csqKeDbm(csq));
    Serial.printf("Terdaftar       : %s | operator: %s\n",
                  modem.isNetworkConnected() ? "YA" : "TIDAK", modem.getOperator().c_str());
    Serial.printf("Data (GPRS/LTE) : %s | IP: %s\n", modem.isGprsConnected() ? "YA" : "TIDAK",
                  modem.localIP().toString().c_str());
    // Tes DNS nama Supabase (tes TCP dihapus: soket 0 dipakai sesi TLS aktif, tes itu memutusnya).
    String host = String(SUPABASE_URL);
    host.replace("https://", "");
    int gm = host.indexOf('/');
    if (gm > 0)
      host = host.substring(0, gm);
    modem.sendAT(String("+CDNSGIP=\"") + host + "\"");
    if (modem.waitResponse(15000L, "+CDNSGIP:") == 1) {
      String baris = GSM_SERIAL.readStringUntil('\n');
      baris.trim();
      Serial.printf("Tes DNS         : %s -> %s\n", host.c_str(), baris.c_str());
      modem.waitResponse(500);
    } else {
      Serial.printf("Tes DNS         : GAGAL / tidak ada jawaban untuk %s (DNS operator bermasalah)\n",
                    host.c_str());
    }
  }
  xSemaphoreGive(gsmMutex);
  if (posisiAda)
    Serial.printf("GPS             : fix %.6f, %.6f (alt %.0f m)\n", posLat, posLon, posAlt);
  else
    Serial.printf("GPS             : belum fix (%s) -- antena GNSS ke port GNSS, menghadap langit\n",
                  gpsMenyala ? "GNSS menyala, mencari satelit" : "GNSS belum dinyalakan");
  time_t now = time(nullptr);
  Serial.printf("Jam sistem      : %s\n",
                now > 1700000000L ? "tersinkron" : "BELUM tersinkron (TLS ke Supabase gagal)");
  if (online) {
    unsigned long t0 = millis();
    bool ok = supabaseSiap();
    Serial.printf("Tes Supabase    : %s (%lu ms)\n", ok ? "OK" : "GAGAL", millis() - t0);
  }
  Serial.println();
}

// ---------------- HTTPS ke Supabase ----------------
static String hostSupabase() {
  String host = String(SUPABASE_URL);
  host.replace("https://", "");
  host.replace("http://", "");
  int g = host.indexOf('/');
  if (g > 0)
    host = host.substring(0, g);
  return host;
}

// Baca satu baris (tanpa CRLF) dengan batas waktu. false bila waktu habis / koneksi putus.
static bool bacaBaris(String &out, unsigned long batasMs) {
  out = "";
  unsigned long t0 = millis();
  while (millis() - t0 < batasMs) {
    if (klien.available()) {
      char c = (char)klien.read();
      if (c == '\n')
        return true;
      if (c != '\r' && out.length() < 300)
        out += c;
      t0 = millis();
    } else if (!klien.connected()) {
      return false;
    } else {
      delay(2);
    }
  }
  return false;
}

// Baca tepat n byte badan ke resp. false bila waktu habis / putus sebelum lengkap.
static bool bacaBadan(String &resp, long n, unsigned long batasMs) {
  unsigned long t0 = millis();
  while (n != 0 && millis() - t0 < batasMs) {
    if (klien.available()) {
      resp += (char)klien.read();
      if (n > 0)
        n--;
      t0 = millis();
    } else if (!klien.connected()) {
      return n < 0; // panjang tak diketahui: putus = selesai
    } else {
      delay(2);
    }
  }
  return n <= 0;
}

static int gagal(const char *alasan) {
  alasanGagal = alasan;
  if (millis() - logGagalMs > 10000UL) {
    logGagalMs = millis();
    Serial.printf("[GSM] Request gagal: %s\n", alasan);
  }
  return -1;
}

static int satuPermintaan(const char *metode, const String &path, const String &header,
                          const String &badan, String &resp) {
  String host = hostSupabase();
  time_t now = time(nullptr);
  if (now < 1700000000L) // jam belum tersinkron: sertifikat tidak bisa diverifikasi
    return gagal("jam sistem belum tersinkron (TLS tidak bisa diverifikasi)");
  if (!klien.connected()) {
    klien.setVerificationTime((uint32_t)(now / 86400UL + 719528UL), (uint32_t)(now % 86400UL));
    klien.setTimeout(15000);
    if (!klien.connect(host.c_str(), 443))
      return gagal("tidak bisa membuka koneksi TLS ke Supabase (sinyal/DNS/TLS)");
  }
  bool adaBadan = strcmp(metode, "GET") != 0;
  String req = String(metode) + " " + path + " HTTP/1.1\r\nHost: " + host +
               "\r\nConnection: keep-alive\r\n" + header;
  if (adaBadan)
    req += "Content-Length: " + String(badan.length()) + "\r\n";
  req += "\r\n";
  if (adaBadan)
    req += badan;
  klien.print(req);
  klien.flush();

  String baris;
  if (!bacaBaris(baris, 20000)) // "HTTP/1.1 200 OK"
    return gagal("server tidak membalas dalam 20 dtk (sinyal lemah/putus)");
  int sp = baris.indexOf(' ');
  int kode = sp > 0 ? baris.substring(sp + 1).toInt() : -1;
  if (kode <= 0)
    return gagal("balasan HTTP tidak dikenali");
  long panjang = -1;
  bool chunked = false, tutup = false;
  while (bacaBaris(baris, 20000) && baris.length()) {
    baris.toLowerCase();
    if (baris.startsWith("content-length:"))
      panjang = baris.substring(15).toInt();
    else if (baris.startsWith("transfer-encoding:") && baris.indexOf("chunked") > 0)
      chunked = true;
    else if (baris.startsWith("connection:") && baris.indexOf("close") > 0)
      tutup = true;
  }
  bool ok = true;
  if (kode == 204 || kode == 304 || !strcmp(metode, "HEAD")) {
    panjang = 0;
  } else if (chunked) {
    for (;;) {
      if (!bacaBaris(baris, 20000)) {
        ok = false;
        break;
      }
      long ukuran = strtol(baris.c_str(), nullptr, 16);
      if (ukuran == 0) { // chunk penutup; buang trailer sampai baris kosong
        while (bacaBaris(baris, 3000) && baris.length()) {
        }
        break;
      }
      if (!bacaBadan(resp, ukuran, 20000) || !bacaBaris(baris, 5000)) {
        ok = false;
        break;
      }
    }
  } else if (panjang >= 0) {
    ok = bacaBadan(resp, panjang, 20000);
  } else {
    bacaBadan(resp, -1, 20000); // tanpa panjang: baca sampai server menutup
    tutup = true;
  }
  if (tutup || !ok)
    klien.stop();
  return ok ? kode : -1;
}

int gsmHttp(const char *metode, const String &path, const String &header, const String &badan,
            String &resp) {
  resp = "";
  if (!gsmMutex || !online)
    return -1;
  if (xSemaphoreTake(gsmMutex, pdMS_TO_TICKS(90000)) != pdTRUE) {
    snprintf(alasanBuf, sizeof(alasanBuf), "modem terus sibuk >90 dtk; pemegang: %s (%lu dtk)", pemegang,
             pemegangSejak ? (millis() - pemegangSejak) / 1000UL : 0UL);
    return gagal(alasanBuf);
  }
  char ket[72];
  snprintf(ket, sizeof(ket), "%s %.40s", metode, path.c_str());
  pegang(ket);
  unsigned long t0 = millis();
  int kode = satuPermintaan(metode, path, header, badan, resp);
  if (kode <= 0) { // soket basi (server menutup koneksi diam): buang lalu coba sekali lagi
    klien.stop();
    resp = "";
    kode = satuPermintaan(metode, path, header, badan, resp);
  }
  if (kode <= 0)
    klien.stop();
  if (millis() - t0 > 5000UL)
    Serial.printf("[GSM] Request lambat: %s -> %d (%lu ms)\n", ket, kode, millis() - t0);
  lepas();
  xSemaphoreGive(gsmMutex);
  return kode;
}

// Satu permintaan GET berrentang (Range) dengan badan biner ke `buf`. Memakai soket keep-alive
// yang sama dengan satuPermintaan(). Dipanggil dengan gsmMutex dipegang.
static int satuRange(const String &path, long dari, long sampai, uint8_t *buf, size_t kap,
                     size_t *terbaca, long *total) {
  *terbaca = 0;
  String host = hostSupabase();
  time_t now = time(nullptr);
  if (now < 1700000000L)
    return gagal("jam sistem belum tersinkron (TLS tidak bisa diverifikasi)");
  if (!klien.connected()) {
    klien.setVerificationTime((uint32_t)(now / 86400UL + 719528UL), (uint32_t)(now % 86400UL));
    klien.setTimeout(15000);
    if (!klien.connect(host.c_str(), 443))
      return gagal("tidak bisa membuka koneksi TLS ke Supabase (sinyal/DNS/TLS)");
  }
  String req = String("GET ") + path + " HTTP/1.1\r\nHost: " + host +
               "\r\nConnection: keep-alive\r\nRange: bytes=" + String(dari) + "-" + String(sampai) +
               "\r\n\r\n";
  klien.print(req);
  klien.flush();

  String baris;
  if (!bacaBaris(baris, 20000))
    return gagal("server tidak membalas dalam 20 dtk (sinyal lemah/putus)");
  int sp = baris.indexOf(' ');
  int kode = sp > 0 ? baris.substring(sp + 1).toInt() : -1;
  if (kode <= 0)
    return gagal("balasan HTTP tidak dikenali");
  long panjang = -1;
  bool chunked = false;
  while (bacaBaris(baris, 20000) && baris.length()) {
    baris.toLowerCase();
    if (baris.startsWith("content-length:"))
      panjang = baris.substring(15).toInt();
    else if (baris.startsWith("content-range:")) {
      int g = baris.indexOf('/');
      if (g > 0)
        *total = baris.substring(g + 1).toInt();
    } else if (baris.startsWith("transfer-encoding:") && baris.indexOf("chunked") > 0)
      chunked = true;
  }
  if (kode == 200 && panjang > 0)
    *total = panjang; // Range diabaikan server: pemanggil memutuskan
  if (kode != 206 || chunked || panjang <= 0 || (size_t)panjang > kap) {
    klien.stop(); // badan tidak dibaca: buang soket supaya sinkron
    return kode == 206 ? gagal("potongan firmware tidak dikenali (chunked/ukuran)") : kode;
  }
  unsigned long t0 = millis();
  size_t n = 0;
  while ((long)n < panjang && millis() - t0 < 30000UL) {
    int ada = klien.available();
    if (ada > 0) {
      int baca = klien.read(buf + n, min((size_t)ada, (size_t)panjang - n));
      if (baca > 0) {
        n += baca;
        t0 = millis();
      }
    } else if (!klien.connected()) {
      break;
    } else {
      delay(2);
    }
  }
  if ((long)n != panjang) {
    klien.stop();
    return gagal("potongan firmware terputus di tengah");
  }
  *terbaca = n;
  return 206;
}

int gsmHttpRange(const String &path, long dari, long sampai, uint8_t *buf, size_t kap,
                 size_t *terbaca, long *total) {
  *terbaca = 0;
  if (!gsmMutex || !online)
    return -1;
  if (xSemaphoreTake(gsmMutex, pdMS_TO_TICKS(90000)) != pdTRUE)
    return gagal("modem terus sibuk >90 dtk (unduh firmware)");
  char ket[72];
  snprintf(ket, sizeof(ket), "RANGE %ld-%ld", dari, sampai);
  pegang(ket);
  int kode = satuRange(path, dari, sampai, buf, kap, terbaca, total);
  if (kode <= 0) { // soket basi: buang lalu coba sekali lagi
    klien.stop();
    kode = satuRange(path, dari, sampai, buf, kap, terbaca, total);
  }
  if (kode <= 0)
    klien.stop();
  lepas();
  xSemaphoreGive(gsmMutex);
  return kode;
}

#else // PAKAI_GSM == 0

bool gsmSiap() { return false; }
void gsmMulai() {}
int gsmRssiDbm() { return 0; }
const char *gsmStatusTeks() { return "nonaktif (PAKAI_GSM 0)"; }
int gsmHttp(const char *, const String &, const String &, const String &, String &) { return -1; }
int gsmHttpRange(const String &, long, long, uint8_t *, size_t, size_t *, long *) { return -1; }
bool gsmPosisi(float *, float *) { return false; }
void gsmCetakStatus() { Serial.println(F("GSM NONAKTIF (PAKAI_GSM 0)")); }

#endif
