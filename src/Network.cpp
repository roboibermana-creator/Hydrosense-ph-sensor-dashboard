#include "Network.h"
#include "Level_Sensor.h"
#include <ArduinoJson.h>
#include <math.h>

// ========================= KONFIGURASI SUPABASE =========================
const char *SUPABASE_URL = "https://hgtwrcjjjycdlhuwsgcx.supabase.co";
const char *SUPABASE_API_KEY = "sb_publishable_S2nOQnap6hUtYWusISR5Ig_WpOrdrGB";
// "Hydrosense table" menyimpan data OLAHAN (ph, tss, water_temperature,
// water_level), bukan register mentah/diagnostik. Dashboard web membaca
// langsung dari tabel ini.
const char *TABLE_READINGS = "Hydrosense table";
const char *TABLE_MODE = "system_control";
const char *TABLE_TSS_CAL_CONFIG = "tss_calibration_config";
const char *TABLE_TSS_HW_CAL_CMD = "tss_hw_calibration_cmd";

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

void connectWiFi() {
  Serial.print("Menghubungkan ke WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool wifiTersambung() { return WiFi.status() == WL_CONNECTED; }

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

bool supabaseSiap() {
  if (!wifiTersambung())
    return false;
  HTTPClient http;
  String url =
      String(SUPABASE_URL) + "/rest/v1/" + TABLE_MODE + "?select=id&limit=1";
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

bool kirimDataKeAPI(HasilPH hasilPh, float tss, float tssRaw) {
  // TSS TIDAK BOLEH ikut ketahan cuma karena pH sensor belum/tidak terpasang --
  // dulu baris ini `return false` kalau !hasilPh.valid, jadi TSS yang sudah
  // valid pun ikut tidak pernah terkirim ke dashboard. Sekarang TSS+level
  // selalu dikirim; ph & water_temperature dikirim null kalau pH invalid,
  // supaya tidak mengotori tabel dengan angka pH/suhu yang belum tentu benar.
  if (!wifiTersambung())
    return false;

  float levelPercent = 0.0, distanceCm = 0.0, currentMa = 0.0;
  readTankSensor(levelPercent, distanceCm, currentMa);

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_READINGS;
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  // Hanya kolom data OLAHAN yang dipakai dashboard — bukan raw
  // register/diagnostik
  String payload = "{";
  payload +=
      "\"ph\":" + (hasilPh.valid ? String(hasilPh.ph, 2) : String("null")) +
      ",";
  payload += "\"tss\":" + String(tss, 2) + ",";
  payload += "\"tss_raw\":" + String(tssRaw, 2) + ",";
  payload += "\"water_temperature\":" +
             (hasilPh.valid ? String(hasilPh.suhu, 1) : String("null")) + ",";
  payload += "\"water_level\":" + String(levelPercent, 1);
  payload += "}";

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

void simpanKeBuffer(HasilPH hasilPh, float tss, float tssRaw) {
  buffer[bufferHead].ph = hasilPh.ph;
  buffer[bufferHead].suhu = hasilPh.suhu;
  buffer[bufferHead].tss = tss;
  buffer[bufferHead].tssRaw = tssRaw;
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
  if (!wifiTersambung())
    return;
  for (int i = 0; i < UKURAN_BUFFER; i++) {
    if (buffer[i].terisi) {
      HasilPH hp;
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

      if (kirimDataKeAPI(hp, buffer[i].tss, buffer[i].tssRaw)) {
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
  hasil.wiperCmd = "diam";

  if (!wifiTersambung())
    return hasil;

  // order by id.desc (kolom identity, selalu naik) -> jangan pakai updated_at
  // karena kolom itu bisa NULL kalau tidak di-set eksplisit saat INSERT dari
  // web, sehingga urutan "terbaru" jadi tidak reliable.
  HTTPClient http;
  String url =
      String(SUPABASE_URL) + "/rest/v1/" + TABLE_MODE +
      "?select=id,mode,valve_cmd,pump_cmd,wiper_cmd&order=id.desc&limit=1";
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
      hasil.mode = String((const char *)(baris["mode"] | "otomatis"));
      hasil.valveCmd = String((const char *)(baris["valve_cmd"] | "tutup"));
      hasil.pumpCmd = String((const char *)(baris["pump_cmd"] | "berhenti"));
      hasil.wiperCmd = String((const char *)(baris["wiper_cmd"] | "diam"));
      hasil.ok = true;
    } else {
      Serial.printf("[SUPABASE] Gagal parse kontrol: %s\n", respon.c_str());
    }
  } else {
    Serial.printf(
        "[SUPABASE] GET system_control gagal, HTTP %d, free heap %u, "
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

  if (!wifiTersambung())
    return hasil;

  // Baris terbaru = faktor kalibrasi aktif saat ini, baik hasil hitung
  // regresi otomatis maupun input manual dari dashboard -- keduanya ditulis
  // ke tabel yang sama, jadi tinggal ambil id.desc limit 1.
  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_TSS_CAL_CONFIG +
              "?select=id,slope,cal_offset&order=id.desc&limit=1";
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
    Serial.printf("[SUPABASE] GET tss_calibration_config gagal, HTTP %d\n",
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

  if (!wifiTersambung())
    return hasil;

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_TSS_HW_CAL_CMD +
              "?select=id,perintah,nilai_target,nilai_aktual&status=eq."
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
        hasil.nilaiTarget = baris["nilai_target"] | 0.0f;
        if (!baris["nilai_aktual"].isNull()) {
          hasil.adaNilaiAktual = true;
          hasil.nilaiAktual = baris["nilai_aktual"] | 0.0f;
        }
        hasil.ok = true;
      }
    }
  } else {
    Serial.printf(
        "[SUPABASE] GET tss_hw_calibration_cmd gagal, HTTP %d\n", kodeHttp);
  }
  http.end();
  return hasil;
}

bool laporkanHasilKalibrasiTssHw(long id, bool sukses, const char *pesan,
                                int16_t reg13, int16_t reg14) {
  if (!wifiTersambung())
    return false;

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_TSS_HW_CAL_CMD +
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
  payload += "\"reg13\":" + String(reg13) + ",";
  payload += "\"reg14\":" + String(reg14);
  payload += "}";

  int kodeHttp = http.PATCH(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 204);
}

bool kirimSampleKalibrasi(const char *titik, const char *aksi, int indexSample,
                          double ph, double suhu, double mv) {
  if (!wifiTersambung())
    return false;

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/calibration_samples";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String payload = "{";
  payload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"titik\":\"" + String(titik) + "\",";
  payload += "\"aksi\":\"" + String(aksi) + "\",";
  payload += "\"sample_index\":" + String(indexSample) + ",";
  payload += "\"ph\":" + String(ph, 3) + ",";
  payload += "\"suhu\":" + String(suhu, 2) + ",";
  payload += (isnan(mv) ? "\"mv\":null" : ("\"mv\":" + String(mv, 2)));
  payload += "}";

  int kodeHttp = http.POST(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 201);
}

bool kirimSampleKalibrasiTSS(const char *standard, const char *aksi,
                             int indexSample, double tss, double suhu,
                             double ntu, double rawValue) {
  if (!wifiTersambung())
    return false;

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/tss_calibration_samples";
  http.begin(url);
  konfigurasiTimeoutHttp(http);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String payload = "{";
  payload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"standard\":\"" + String(standard) + "\",";
  payload += "\"aksi\":\"" + String(aksi) + "\",";
  payload += "\"sample_index\":" + String(indexSample) + ",";
  payload += "\"tss\":" + String(tss, 2) + ",";
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
