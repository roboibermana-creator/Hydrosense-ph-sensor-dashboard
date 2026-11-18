#include "Network.h"
#include "HttpKlien.h"
#include "Level_Sensor.h"
#include "SdQueue.h"
#include <ArduinoJson.h>
#include <math.h>

// Suhu dari sensor TSS (diisi Hardware.cpp, didefinisikan di main.cpp).
extern float suhuTssTerakhir;
extern float ntuTerakhir;

// ========================= KONFIGURASI SUPABASE =========================
const char *SUPABASE_URL = SUPABASE_URL_CFG;
const char *SUPABASE_API_KEY = SUPABASE_API_KEY_CFG;
// "Hydrosense table" menyimpan data OLAHAN (ph, tss, water_temperature,
// water_level), bukan register mentah/diagnostik. Dashboard web membaca
// langsung dari tabel ini.
const char *TABLE_READINGS = "Hydrosense%20table";
// Satu tabel untuk semua perintah dari web (type = modbus | ph_cal | tss_hw |
// control). type 'control' = mode/valve/pompa, tetap berupa log (baris baru tiap
// perintah) karena firmware mendeteksi perintah baru lewat id yang naik.
const char *TABLE_CMD = "device_commands";
// Faktor kalibrasi TSS aktif = baris terbaru calibration (sensor=tss, aksi=faktor).
const char *TABLE_CALIBRATION = "calibration";
// Level air (water_level + distance_cm) dikirim terpisah & lebih sering
// (taskKirimLevel) ke tabel yang sama dengan pH/TSS. Dashboard memisahkan barisnya
// lewat "kolom distance_cm tidak null".
#define TABLE_WATER_LEVEL TABLE_READINGS

const int UKURAN_BUFFER = 20;
DataSample buffer[20];
int bufferHead = 0;

// CATATAN: sempat dicoba reuse satu koneksi TLS (keep-alive) di sini buat
// mempercepat request ke Supabase, tapi di board ini malah bikin request
// KE-2 dst macet total (hang tanpa error/timeout). Dikembalikan ke koneksi
// baru tiap request (lebih lambat ~1-3 detik per call karena TLS handshake,
// tapi TERBUKTI stabil/tidak hang) sampai ada solusi reuse yang aman.
//
// Semua request WAJIB dikasih batas waktu eksplisit -- tanpa ini, kalau
// socket/TLS macet (pernah kejadian setelah beberapa request berturut-turut,
// diduga fragmentasi heap khas ESP32+HTTPS), loop() bisa nge-hang PERMANEN
// nunggu respon yang tidak pernah datang, dan command relay dari web jadi
// tidak pernah lagi diproses sampai board di-reset manual.
static void konfigurasiTimeoutHttp(HTTPClient &http) {
  // Dinaikkan dari 4000ms -- di WiFi korporat (mis. proxy/firewall yang
  // meng-inspect HTTPS) round-trip request ke Supabase bisa jauh lebih
  // lambat dari WiFi rumah biasa, sampai bikin request timeout (HTTPC_ERROR_
  // READ_TIMEOUT / -11) padahal koneksi & TLS handshake-nya sendiri berhasil.
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
}
// Lewat GSM timeout diatur oleh Gsm.cpp, bukan HTTPClient.
static void konfigurasiTimeoutHttp(HttpKlien &k) {
  if (!k.lewatGsm())
    konfigurasiTimeoutHttp(k.raw());
}

// Cetak alasan putus WiFi (kode alasan standar ESP-IDF, mis. 200 = beacon
// timeout/sinyal hilang, 201 = SSID tidak ditemukan, 15/202 = gagal
// handshake/auth) supaya kelihatan penyebab putus-nyambung di Serial.
static void logAlasanPutusWiFi(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("[WIFI] Putus, alasan=%d, RSSI terakhir=%d dBm\n",
                  (int)info.wifi_sta_disconnected.reason,
                  (int)info.wifi_sta_disconnected.rssi);
  }
}

// Watchdog reconnect: setAutoReconnect() sering berhenti mencoba setelah alasan
// 201 (NO_AP_FOUND), jadi WiFi bisa lama putus. Task ini memaksa
// disconnect+begin ulang dengan backoff (10 -> 60 dtk) selama WiFi putus.
static void taskWifiWatchdog(void *) {
  const uint32_t BACKOFF_MIN_MS = 10000, BACKOFF_MAX_MS = 60000;
  uint32_t backoff = BACKOFF_MIN_MS;
  uint32_t putusSejak = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (WiFi.status() == WL_CONNECTED) {
      putusSejak = 0;
      backoff = BACKOFF_MIN_MS;
      continue;
    }
    if (putusSejak == 0)
      putusSejak = millis();
    if (millis() - putusSejak >= backoff) {
      Serial.printf("[WIFI] Watchdog: masih putus (status=%d), sambung ulang\n",
                    (int)WiFi.status());
      WiFi.disconnect(false, false);
      vTaskDelay(pdMS_TO_TICKS(200));
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      putusSejak = millis();
      backoff = min(backoff * 2, BACKOFF_MAX_MS);
    }
  }
}

void connectWiFi() {
  static bool terkonfigurasi = false;
  Serial.print("Menghubungkan ke WiFi...");
  WiFi.mode(WIFI_STA);
  if (!terkonfigurasi) {
    terkonfigurasi = true;
    WiFi.persistent(false);       // jangan tulis kredensial ke flash tiap begin()
    WiFi.setAutoReconnect(true);  // sambung ulang otomatis kalau putus
    WiFi.setSleep(false);         // matikan power-save WiFi (sumber utama putus-nyambung)
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    // Scan semua channel & pilih AP bersinyal terkuat (SSID korporat biasanya
    // punya banyak AP; default hanya ambil AP pertama yang ketemu).
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.onEvent(logAlasanPutusWiFi);
    xTaskCreatePinnedToCore(taskWifiWatchdog, "wifiWd", 4096, nullptr, 1,
                            nullptr, 0);
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool wifiTersambung() { return WiFi.status() == WL_CONNECTED; }
// Internet tersedia lewat WiFi ATAU data seluler (SIM7600, Gsm.cpp).
bool internetSiap() { return wifiTersambung() || gsmSiap(); }

// Sama dengan internetSiap(): WiFi ATAU data seluler (GSM) tersambung.
bool jaringanSiap() { return internetSiap(); }

// Bisa dipanggil kapan saja lewat command 'N' di Serial Monitor -- tidak
// perlu nangkep momen boot buat lihat status WiFi terkini.
void tampilkanStatusWiFi() {
  Serial.println(F("\n=== STATUS WIFI ==="));
  Serial.printf("SSID target : %s\n", WIFI_SSID);
  wl_status_t status = WiFi.status();
  Serial.printf("Status      : %d (%s)\n", (int)status,
                status == WL_CONNECTED        ? "TERSAMBUNG"
                : status == WL_NO_SSID_AVAIL  ? "SSID TIDAK DITEMUKAN"
                : status == WL_CONNECT_FAILED ? "GAGAL KONEK (password salah?)"
                : status == WL_IDLE_STATUS    ? "IDLE / sedang mencoba"
                : status == WL_DISCONNECTED   ? "TERPUTUS"
                                              : "LAINNYA");
  if (status == WL_CONNECTED) {
    Serial.print("IP          : ");
    Serial.println(WiFi.localIP());
    Serial.printf("RSSI        : %d dBm\n", WiFi.RSSI());
  }
  Serial.printf("Free heap   : %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("====================");
}

// Command 'L': scan WiFi dari sisi ESP32 -- untuk cek apakah SSID target
// benar-benar terlihat (band 2.4 GHz, sinyal, nama persis sama).
void scanWiFiSekitar() {
  Serial.println(F("\n=== SCAN WIFI (ESP32 hanya melihat 2.4 GHz) ==="));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks(false, true); // show hidden
  if (n <= 0) {
    Serial.printf("Tidak ada jaringan terdeteksi (hasil=%d)\n", n);
  }
  bool ketemu = false;
  for (int i = 0; i < n; i++) {
    bool target = WiFi.SSID(i) == String(WIFI_SSID);
    ketemu = ketemu || target;
    Serial.printf("%s[%d] \"%s\" | RSSI %d dBm | CH %d | %s\n",
                  target ? ">>" : "  ", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                  WiFi.channel(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SECURED");
  }
  Serial.printf("Target \"%s\": %s\n", WIFI_SSID,
                ketemu ? "TERLIHAT" : "TIDAK TERLIHAT");
  WiFi.scanDelete();
  connectWiFi();
}

bool supabaseSiap() {
  if (!internetSiap())
    return false;
  HttpKlien http;
  String url =
      String(SUPABASE_URL) + "/rest/v1/" + TABLE_CMD + "?select=id&limit=1";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  int kodeHttp = http.GET();
  // DEBUG SEMENTARA -- kalau gagal, kodeHttp negatif (lihat tabel error
  // HTTPClient.h) berarti gagal di level koneksi/TLS (DNS, connect refused,
  // timeout, dst), BUKAN salah kode di Supabase -- biasanya jaringan WiFi
  // yang diblokir/butuh captive portal. kodeHttp positif tapi bukan 200
  // (mis. 401/403) berarti Supabase menolak API key/auth.
  if (kodeHttp != 200) {
    Serial.printf("[SUPABASE] Cek kesiapan gagal, kodeHttp=%d, IP=%s, "
                  "RSSI=%ddBm\n",
                  kodeHttp, WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  http.end();
  return (kodeHttp == 200);
}

bool kirimDataKeAPI(HasilPH hasilPh, float tss) {
  // TSS TIDAK BOLEH ikut ketahan cuma karena pH sensor belum/tidak terpasang --
  // dulu baris ini `return false` kalau !hasilPh.valid, jadi TSS yang sudah
  // valid pun ikut tidak pernah terkirim ke dashboard. Sekarang TSS+level
  // selalu dikirim; ph & water_temperature dikirim null kalau pH invalid,
  // supaya tidak mengotori tabel dengan angka pH/suhu yang belum tentu benar.
  float levelPercent = 0.0, distanceCm = 0.0, currentMa = 0.0;
  readTankSensor(levelPercent, distanceCm, currentMa);

  // Hanya kolom data OLAHAN yang dipakai dashboard â€” bukan raw
  // register/diagnostik
  String payload = "{";
  payload +=
      "\"ph\":" + (hasilPh.valid ? String(hasilPh.ph, 2) : String("null")) +
      ",";
  payload += "\"probe_mv\":" +
             ((hasilPh.valid && isfinite(hasilPh.mv)) ? String(hasilPh.mv, 1)
                                                      : String("null")) +
             ",";
  payload += "\"ntu\":" +
             (isfinite(ntuTerakhir) ? String(ntuTerakhir, 1) : String("null")) +
             ",";
  payload += "\"tss\":" + String(tss, 2) + ",";
  // Suhu utama dari sensor pH (suhu probe di larutan yang sama dengan pH);
  // cadangan suhu sensor TSS (register 2) kalau pH tidak valid.
  String suhuStr = "null";
  if (hasilPh.valid)
    suhuStr = String(hasilPh.suhu, 1);
  else if (isfinite(suhuTssTerakhir))
    suhuStr = String(suhuTssTerakhir, 1);
  payload += "\"water_temperature\":" + suhuStr + ",";
  payload += "\"water_level\":" + String(levelPercent, 1);
  payload += "}";

  // Kirim langsung kalau online. Kalau offline / gagal kirim, simpan ke SD
  // card (antrean persisten, dikirim ulang dengan waktu aslinya oleh
  // SdQueue.cpp) -- jadi tidak ada data yang hilang. Tanpa SD, false -> jatuh
  // ke buffer RAM seperti dulu.
  int kodeHttp = -1;
  if (kirimPayloadMentah(payload, kodeHttp))
    return true;
  return sdQueueTambah(payload);
}

bool kirimPayloadMentah(const String &payload, int &kodeHttp) {
  kodeHttp = -1;
  if (!jaringanSiap())
    return false;

  HttpKlien http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_READINGS;
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  kodeHttp = http.POST(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 201);
}

bool kirimLevelKeAPI(float levelPercent, float distanceCm) {
  if (!internetSiap())
    return false;

  HttpKlien http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_WATER_LEVEL;
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String payload = "{\"water_level\":" + String(levelPercent, 1) +
                   ",\"distance_cm\":" + String(distanceCm, 1) + "}";

  int kodeHttp = http.POST(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 201);
}

bool kirimStatusErrorKeAPI(int modbusErrorCount) {
  // Sensor pH gagal/tidak valid -> tidak ada data olahan yang bisa dikirim ke
  // Hydrosense table (tabel ini tidak punya kolom status error). Cukup dicatat
  // di Serial Monitor untuk debugging lokal.
  Serial.printf(
      "[SUPABASE] Lewati kirim data - pH tidak valid (modbus_errors=%d)\n",
      modbusErrorCount);
  return true;
}

void simpanKeBuffer(HasilPH hasilPh, float tss) {
  buffer[bufferHead].ph = hasilPh.ph;
  buffer[bufferHead].suhu = hasilPh.suhu;
  buffer[bufferHead].tss = tss;
  buffer[bufferHead].kualitas = hasilPh.kualitas;
  buffer[bufferHead].compliant = hasilPh.compliant;
  buffer[bufferHead].rawPh = hasilPh.rawPh;
  buffer[bufferHead].rawTemp = hasilPh.rawTemp;
  buffer[bufferHead].sd = hasilPh.sd;
  buffer[bufferHead].drift = hasilPh.drift;
  buffer[bufferHead].modbusErrorCount = hasilPh.modbusErrorCount;
  buffer[bufferHead].phValid = hasilPh.valid;
  buffer[bufferHead].terisi = true;
  bufferHead = (bufferHead + 1) % UKURAN_BUFFER;
}

void cobaKirimUlangBuffer() {
  if (!internetSiap())
    return;
  for (int i = 0; i < UKURAN_BUFFER; i++) {
    if (buffer[i].terisi) {
      HasilPH hp;
      hp.mv = NAN; // mV tidak ikut dibuffer
      hp.valid = buffer[i].phValid;
      hp.ph = buffer[i].ph;
      hp.suhu = buffer[i].suhu;
      hp.kualitas = buffer[i].kualitas;
      hp.compliant = buffer[i].compliant;
      hp.rawPh = buffer[i].rawPh;
      hp.rawTemp = buffer[i].rawTemp;
      hp.sd = buffer[i].sd;
      hp.drift = buffer[i].drift;
      hp.modbusErrorCount = buffer[i].modbusErrorCount;

      if (kirimDataKeAPI(hp, buffer[i].tss)) {
        buffer[i].terisi = false;
      } else {
        break; // masih gagal, hindari spam - coba lagi loop berikutnya
      }
    }
  }
}

KontrolManual bacaKontrolDariSupabase() {
  KontrolManual hasil;
  hasil.ok = false;
  hasil.id = -1;
  hasil.mode = "otomatis";
  hasil.valveCmd = "tutup";
  hasil.pumpCmd = "berhenti";

  if (!internetSiap())
    return hasil;

  // order by id.desc (kolom identity, selalu naik) -> jangan pakai updated_at
  // karena kolom itu bisa NULL kalau tidak di-set eksplisit saat INSERT dari
  // web, sehingga urutan "terbaru" jadi tidak reliable.
  HttpKlien http;
  String url =
      String(SUPABASE_URL) + "/rest/v1/" + TABLE_CMD +
      "?select=id,params&type=eq.control&order=id.desc&limit=1";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);

  int kodeHttp = http.GET();
  if (kodeHttp == 200) {
    String respon = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, respon);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
      JsonObject baris = doc[0];
      hasil.id = baris["id"] | -1L;
      JsonObject params = baris["params"];
      hasil.mode = String((const char *)(params["mode"] | "otomatis"));
      hasil.valveCmd = String((const char *)(params["valve_cmd"] | "tutup"));
      hasil.pumpCmd = String((const char *)(params["pump_cmd"] | "berhenti"));
      hasil.ok = true;
    } else {
      Serial.printf("[SUPABASE] Gagal parse kontrol: %s\n", respon.c_str());
    }
  } else {
    Serial.printf(
        "[SUPABASE] GET device_commands (control) gagal, HTTP %d, free heap %u, "
        "RSSI %ddBm\n",
        kodeHttp, (unsigned)ESP.getFreeHeap(), WiFi.RSSI());
  }
  http.end();
  return hasil;
}

bool bacaModeOperasiDariSupabase() {
  KontrolManual k = bacaKontrolDariSupabase();
  return k.mode == "otomatis";
}

KalibrasiTSS bacaKalibrasiTSSDariSupabase() {
  KalibrasiTSS hasil;
  hasil.ok = false;
  hasil.slope = 1.0f;
  hasil.offset = 0.0f;

  if (!jaringanSiap())
    return hasil;

  // Baris terbaru = faktor kalibrasi aktif saat ini, baik hasil hitung
  // regresi otomatis maupun input manual dari dashboard -- keduanya ditulis
  // ke tabel yang sama, jadi tinggal ambil id.desc limit 1.
  HttpKlien http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_CALIBRATION +
              "?select=id,slope,cal_offset&sensor=eq.tss&aksi=eq.faktor"
              "&order=id.desc&limit=1";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);

  int kodeHttp = http.GET();
  if (kodeHttp == 200) {
    String respon = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, respon);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
      JsonObject baris = doc[0];
      hasil.slope = baris["slope"] | 1.0f;
      hasil.offset = baris["cal_offset"] | 0.0f;
      hasil.ok = true;
    } else {
      Serial.printf("[SUPABASE] Gagal parse kalibrasi TSS: %s\n",
                    respon.c_str());
    }
  } else {
    Serial.printf("[SUPABASE] GET calibration (faktor TSS) gagal, HTTP %d\n",
                  kodeHttp);
  }
  http.end();
  return hasil;
}

PerintahKalibrasiTssHw
bacaPerintahKalibrasiTssHwDariSupabase(long idTerakhirDiproses) {
  PerintahKalibrasiTssHw hasil;
  hasil.ok = false;
  hasil.id = -1;
  hasil.nilaiTarget = 0.0f;
  hasil.adaNilaiAktual = false;
  hasil.nilaiAktual = 0.0f;

  if (!jaringanSiap())
    return hasil;

  HttpKlien http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_CMD +
              "?select=id,perintah,params&type=eq.tss_hw&status=eq."
              "pending&order=id.desc&limit=1";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);

  int kodeHttp = http.GET();
  if (kodeHttp == 200) {
    String respon = http.getString();
    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, respon);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
      JsonObject baris = doc[0];
      long id = baris["id"] | -1L;
      if (id > idTerakhirDiproses) {
        hasil.id = id;
        hasil.perintah = String((const char *)(baris["perintah"] | ""));
        JsonObject params = baris["params"];
        hasil.nilaiTarget = params["nilai_target"] | 0.0f;
        if (!params["nilai_aktual"].isNull()) {
          hasil.adaNilaiAktual = true;
          hasil.nilaiAktual = params["nilai_aktual"] | 0.0f;
        }
        hasil.ok = true;
      }
    }
  } else {
    Serial.printf("[SUPABASE] GET device_commands (tss_hw) gagal, HTTP %d\n",
                  kodeHttp);
  }
  http.end();
  return hasil;
}

bool laporkanHasilKalibrasiTssHw(long id, bool sukses, const char *pesan,
                                int16_t reg13, int16_t reg14) {
  if (!jaringanSiap())
    return false;

  HttpKlien http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_CMD +
              "?id=eq." + String(id);
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String pesanEscaped = String(pesan);
  pesanEscaped.replace("\"", "'");

  // diproses_at diisi lewat trigger/default kolom di Supabase (bukan dari
  // sini) -- literal "now()" tidak valid sebagai JSON string value.
  String payload = "{";
  payload += "\"status\":\"" + String(sukses ? "sukses" : "gagal") + "\",";
  payload += "\"pesan\":\"" + pesanEscaped + "\",";
  payload += "\"hasil\":{\"reg13\":" + String(reg13) +
             ",\"reg14\":" + String(reg14) + "}";
  payload += "}";

  int kodeHttp = http.PATCH(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 204);
}

PerintahModbus bacaPerintahModbusDariSupabase(long idTerakhirDiproses) {
  PerintahModbus hasil;
  hasil.ok = false;
  hasil.id = -1;
  hasil.slaveId = 0;
  hasil.alamat = 0;
  hasil.jumlah = 0;

  if (!jaringanSiap())
    return hasil;

  HttpKlien http;
  // Terlama dulu (id naik) supaya urutan klik dari web dihormati.
  String url = String(SUPABASE_URL) +
               "/rest/v1/device_commands?select=id,perintah,params"
               "&type=eq.modbus&status=eq.pending&id=gt." +
               String(idTerakhirDiproses) + "&order=id.asc&limit=1";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);

  int kodeHttp = http.GET();
  if (kodeHttp == 200) {
    String respon = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, respon);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
      JsonObject baris = doc[0];
      hasil.id = baris["id"] | -1L;
      JsonObject params = baris["params"];
      hasil.slaveId = params["slave_id"] | 0;
      hasil.aksi = String((const char *)(baris["perintah"] | ""));
      hasil.alamat = params["alamat"] | 0;
      hasil.jumlah = params["jumlah"] | 0;
      hasil.nilai = String((const char *)(params["nilai"] | ""));
      hasil.tipe = String((const char *)(params["tipe"] | "u16"));
      hasil.ok = hasil.id > idTerakhirDiproses;
    }
  } else {
    Serial.printf("[SUPABASE] GET device_commands (modbus) gagal, HTTP %d\n",
                  kodeHttp);
  }
  http.end();
  return hasil;
}

bool laporkanHasilModbus(long id, bool sukses, const char *hasil,
                         const char *pesan) {
  if (!jaringanSiap())
    return false;

  HttpKlien http;
  String url =
      String(SUPABASE_URL) + "/rest/v1/device_commands?id=eq." + String(id);
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String pesanEscaped = String(pesan);
  pesanEscaped.replace("\"", "'");
  String payload = "{";
  payload += "\"status\":\"" + String(sukses ? "sukses" : "gagal") + "\",";
  payload += "\"hasil\":\"" + String(hasil) + "\",";
  payload += "\"pesan\":\"" + pesanEscaped + "\"";
  payload += "}";

  int kodeHttp = http.PATCH(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 204);
}

JadwalOtomatis bacaJadwalOtomatisDariSupabase() {
  JadwalOtomatis hasil;
  hasil.ok = false;
  hasil.intervalDetik = 0;
  hasil.pompaDetik = 0;
  hasil.valveDetik = 0;
  hasil.bacaSensorDetik = 15;
  hasil.stabilDetik = 10;
  hasil.sampelMs = 1000;
  hasil.trimPersen = 15;

  if (!internetSiap())
    return hasil;

  HttpKlien http;
  String url = String(SUPABASE_URL) +
               "/rest/v1/devices?project=eq.hydrosense&select=interval_detik,"
               "pompa_detik,valve_detik,baca_sensor_detik,stabil_detik,"
               "sampel_ms,trim_persen&name=eq." +
               String(DEVICE_ID) + "&limit=1";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);

  int kodeHttp = http.GET();
  if (kodeHttp == 200) {
    String respon = http.getString();
    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, respon);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
      JsonObject baris = doc[0];
      hasil.intervalDetik = baris["interval_detik"] | 0;
      hasil.pompaDetik = baris["pompa_detik"] | 0;
      hasil.valveDetik = baris["valve_detik"] | 0;
      hasil.bacaSensorDetik = baris["baca_sensor_detik"] | 15;
      hasil.stabilDetik = baris["stabil_detik"] | 10;
      hasil.sampelMs = baris["sampel_ms"] | 1000;
      hasil.trimPersen = baris["trim_persen"] | 15;
      hasil.ok = hasil.intervalDetik > 0 && hasil.pompaDetik > 0 &&
                 hasil.valveDetik > 0;
    }
  } else {
    Serial.printf("[SUPABASE] GET devices (jadwal) gagal, HTTP %d\n",
                  kodeHttp);
  }
  http.end();
  return hasil;
}

// Batas tinggi air dari dashboard (devices.min_air_cm & max_air_cm). adaMin/adaMax true hanya bila
// kolomnya berisi angka. Request terpisah dari jadwal supaya jadwal tetap jalan walau migrasi 0014
// belum dijalankan (kolom belum ada -> HTTP 400 hanya di sini).
bool bacaBatasAirDariSupabase(float &minCm, bool &adaMin, float &maxCm, bool &adaMax) {
  adaMin = adaMax = false;
  if (!internetSiap())
    return false;
  HttpKlien http;
  String url = String(SUPABASE_URL) +
               "/rest/v1/devices?project=eq.hydrosense&select=min_air_cm,max_air_cm&name=eq." +
               String(DEVICE_ID) + "&limit=1";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  bool ok = false;
  int kodeHttp = http.GET();
  if (kodeHttp == 200) {
    StaticJsonDocument<192> doc;
    if (!deserializeJson(doc, http.getString()) && doc.is<JsonArray>() && doc.size() > 0) {
      JsonObject b = doc[0];
      if (!b["min_air_cm"].isNull()) {
        minCm = b["min_air_cm"].as<float>();
        adaMin = true;
      }
      if (!b["max_air_cm"].isNull()) {
        maxCm = b["max_air_cm"].as<float>();
        adaMax = true;
      }
      ok = true;
    }
  } else {
    Serial.printf("[KOLAM] GET devices (batas air) gagal, HTTP %d (migrasi 0014 sudah dijalankan?)\n",
                  kodeHttp);
  }
  http.end();
  return ok;
}

PerintahKalibrasiPh
bacaPerintahKalibrasiPhDariSupabase(long idTerakhirDiproses) {
  PerintahKalibrasiPh hasil;
  hasil.ok = false;
  hasil.id = -1;

  if (!jaringanSiap())
    return hasil;

  HttpKlien http;
  String url = String(SUPABASE_URL) +
               "/rest/v1/device_commands?select=id,perintah&type=eq.ph_cal"
               "&status=eq.pending&order=id.desc&limit=1";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);

  int kodeHttp = http.GET();
  if (kodeHttp == 200) {
    String respon = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, respon);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
      JsonObject baris = doc[0];
      long id = baris["id"] | -1L;
      if (id > idTerakhirDiproses) {
        hasil.id = id;
        hasil.perintah = String((const char *)(baris["perintah"] | ""));
        hasil.ok = true;
      }
    }
  } else {
    Serial.printf("[SUPABASE] GET device_commands (ph_cal) gagal, HTTP %d\n",
                  kodeHttp);
  }
  http.end();
  return hasil;
}

bool laporkanHasilKalibrasiPh(long id, bool sukses, const char *pesan) {
  if (!jaringanSiap())
    return false;

  HttpKlien http;
  String url = String(SUPABASE_URL) + "/rest/v1/device_commands?id=eq." +
               String(id);
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String pesanEscaped = String(pesan);
  pesanEscaped.replace("\"", "'");

  String payload = "{";
  payload += "\"status\":\"" + String(sukses ? "sukses" : "gagal") + "\",";
  payload += "\"pesan\":\"" + pesanEscaped + "\"";
  payload += "}";

  int kodeHttp = http.PATCH(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 204);
}

bool laporkanProgresKalibrasiPh(long id, const char *pesan) {
  if (!internetSiap())
    return false;

  HttpKlien http;
  String url = String(SUPABASE_URL) + "/rest/v1/device_commands?id=eq." +
               String(id) + "&status=eq.pending";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String pesanEscaped = String(pesan);
  pesanEscaped.replace("\"", "'");
  int kodeHttp = http.PATCH("{\"pesan\":\"" + pesanEscaped + "\"}");
  http.end();
  return (kodeHttp == 200 || kodeHttp == 204);
}

bool kirimSampleKalibrasi(const char *titik, const char *aksi, int indexSample,
                          double ph, double suhu, double mv) {
  if (!internetSiap())
    return false;

  HttpKlien http;
  String url = String(SUPABASE_URL) + "/rest/v1/calibration";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String payload = "{";
  payload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"sensor\":\"ph\",";
  payload += "\"titik\":\"" + String(titik) + "\",";
  payload += "\"aksi\":\"" + String(aksi) + "\",";
  payload += "\"sample_index\":" + String(indexSample) + ",";
  payload += "\"nilai\":" + String(ph, 3) + ",";
  payload += "\"suhu\":" + String(suhu, 2) + ",";
  payload += (isnan(mv) ? "\"mv\":null" : ("\"mv\":" + String(mv, 2)));
  payload += "}";

  int kodeHttp = http.POST(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 201);
}

// URL baris alat di tabel devices (status + jadwal). Baris harus sudah ada
// (disemai di migrasi 0005): PATCH tidak membuat baris baru.
static String urlBarisAlat() {
  return String(SUPABASE_URL) + "/rest/v1/devices?project=eq.hydrosense&name=eq." +
         String(DEVICE_ID);
}

// Simpan status asli pompa ke baris alat di devices supaya dashboard bisa
// menampilkan kondisi sebenarnya, termasuk penolakan interlock.
bool kirimStatusPompaKeSupabase(bool pompaNyala, const char *catatan) {
  if (!internetSiap())
    return false;

  HttpKlien http;
  String url = urlBarisAlat();
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String payload = "{";
  payload += String("\"pump_on\":") + (pompaNyala ? "true" : "false") + ",";
  payload += "\"pump_note\":\"" + String(catatan) + "\",";
  payload += "\"ip_address\":\"" + WiFi.localIP().toString() + "\",";
  payload += "\"mac_address\":\"" + WiFi.macAddress() + "\",";
  payload += "\"pump_updated_at\":\"now\"";
  payload += "}";

  int kodeHttp = http.PATCH(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 201 || kodeHttp == 204);
}

extern float tssRawTerakhir; // main.cpp
// Denyut alat: mode, fase, dan sisa detik ke awal siklus berikutnya. Hanya
// menyentuh kolom ini + heartbeat_at (kolom pompa tidak ikut tertimpa), jadi
// dashboard bisa menandai alat online & menampilkan countdown di mode apa pun.
// sisaDetik < 0 -> tidak ada countdown (null).
bool kirimHeartbeatKeSupabase(const char *mode, const char *fase,
                              long sisaDetik, bool pompaNyala, bool valveBuka,
                              bool floatHigh) {
  if (!internetSiap())
    return false;

  HttpKlien http;
  String url = urlBarisAlat();
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String payload = "{";
  // IP/MAC ikut denyut supaya terisi di semua mode (sebelumnya hanya mode manual).
  payload += "\"ip_address\":\"" + WiFi.localIP().toString() + "\",";
  payload += "\"mac_address\":\"" + WiFi.macAddress() + "\",";
  payload += "\"mode\":\"" + String(mode) + "\",";
  payload += "\"fase\":\"" + String(fase) + "\",";
  payload += sisaDetik >= 0 ? "\"next_cycle_s\":" + String(sisaDetik) + ","
                            : String("\"next_cycle_s\":null,");
  // Kondisi nyata aktuator & float switch -> panel tangki di dashboard.
  payload += String("\"pompa_nyala\":") + (pompaNyala ? "true" : "false") + ",";
  payload += String("\"valve_buka\":") + (valveBuka ? "true" : "false") + ",";
  payload += String("\"float_high\":") + (floatHigh ? "true" : "false") + ",";
  // Raw TSS terakhir (trimmed mean sebelum kalibrasi) -- hanya untuk wizard
  // kalibrasi di dashboard; TIDAK masuk tabel data utama.
  payload += "\"tss_raw\":" + String(tssRawTerakhir, 2) + ",";
  // heartbeat_at, BUKAN pump_updated_at: pump_updated_at dipakai dashboard untuk
  // mendeteksi status pompa basi setelah toggle diklik.
  payload += "\"heartbeat_at\":\"now\"";
  payload += "}";

  int kodeHttp = http.PATCH(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 201 || kodeHttp == 204);
}

// Posisi alat dari GNSS SIM7600. Dikirim TERPISAH dari heartbeat: kalau kolom latitude/longitude
// belum ada di devices (migrasi 0013 belum dijalankan), hanya pengiriman ini yang gagal.
bool kirimPosisiKeSupabase(float lat, float lon, float alt) {
  if (!internetSiap())
    return false;
  HttpKlien http;
  http.begin(urlBarisAlat());
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  String payload = "{\"latitude\":" + String(lat, 6) + ",\"longitude\":" + String(lon, 6) +
                   ",\"altitude_m\":" + String(alt, 0) + ",\"location_at\":\"now\"}";
  int kodeHttp = http.PATCH(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 201 || kodeHttp == 204);
}

bool kirimSampleKalibrasiTSS(const char *standard, const char *aksi,
                             int indexSample, double tss, double suhu,
                             double ntu, double rawValue) {
  if (!internetSiap())
    return false;

  HttpKlien http;
  String url = String(SUPABASE_URL) + "/rest/v1/calibration";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String payload = "{";
  payload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"sensor\":\"tss\",";
  payload += "\"titik\":\"" + String(standard) + "\",";
  payload += "\"aksi\":\"" + String(aksi) + "\",";
  payload += "\"sample_index\":" + String(indexSample) + ",";
  payload += "\"nilai\":" + String(tss, 2) + ",";
  payload += "\"suhu\":" + String(suhu, 2) + ",";
  payload +=
      (isnan(ntu) ? "\"ntu\":null" : ("\"ntu\":" + String(ntu, 2))) + ",";
  payload += (isnan(rawValue) ? "\"raw_value\":null"
                              : ("\"raw_value\":" + String(rawValue, 2)));
  payload += "}";

  int kodeHttp = http.POST(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 201);
}
