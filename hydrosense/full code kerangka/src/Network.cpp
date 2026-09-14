#include "Network.h"
#include <ArduinoJson.h>
#include <math.h>
#include "Level_Sensor.h"

// ========================= KONFIGURASI SUPABASE =========================
const char* SUPABASE_URL      = "https://hgtwrcjjjycdlhuwsgcx.supabase.co";
const char* SUPABASE_API_KEY  = "sb_publishable_S2nOQnap6hUtYWusISR5Ig_WpOrdrGB";
// "Hydrosense table" menyimpan data OLAHAN (ph, tss, water_temperature, water_level),
// bukan register mentah/diagnostik. Dashboard web membaca langsung dari tabel ini.
const char* TABLE_READINGS = "Hydrosense table";
const char* TABLE_MODE     = "system_control";

const int UKURAN_BUFFER = 20;
DataSample buffer[20];
int bufferHead = 0;

void connectWiFi() {
  Serial.print("Menghubungkan ke WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool wifiTersambung() { 
  return WiFi.status() == WL_CONNECTED; 
}

bool supabaseSiap() {
  if (!wifiTersambung()) return false;
  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_MODE + "?select=id&limit=1";
  http.begin(url);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  int kodeHttp = http.GET();
  http.end();
  return (kodeHttp == 200);
}

bool kirimDataKeAPI(HasilPH hasilPh, float tss) {
  if (!wifiTersambung() || !hasilPh.valid) return false;

  float levelPercent = 0.0, distanceCm = 0.0, currentMa = 0.0;
  readTankSensor(levelPercent, distanceCm, currentMa);

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_READINGS;
  http.begin(url);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  // Hanya kolom data OLAHAN yang dipakai dashboard — bukan raw register/diagnostik
  String payload = "{";
  payload += "\"ph\":" + String(hasilPh.ph, 2) + ",";
  payload += "\"tss\":" + String(tss, 2) + ",";
  payload += "\"water_temperature\":" + String(hasilPh.suhu, 1) + ",";
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
  Serial.printf("[SUPABASE] Lewati kirim data - pH tidak valid (modbus_errors=%d)\n", modbusErrorCount);
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
  buffer[bufferHead].phValid = true;
  buffer[bufferHead].terisi = true;
  bufferHead = (bufferHead + 1) % UKURAN_BUFFER;
}

void cobaKirimUlangBuffer() {
  if (!wifiTersambung()) return;
  for (int i = 0; i < UKURAN_BUFFER; i++) {
    if (buffer[i].terisi) {
      HasilPH hp;
      hp.valid = true;
      hp.ph = buffer[i].ph; hp.suhu = buffer[i].suhu;
      hp.kualitas = buffer[i].kualitas; hp.compliant = buffer[i].compliant;
      hp.rawPh = buffer[i].rawPh; hp.rawTemp = buffer[i].rawTemp;
      hp.sd = buffer[i].sd; hp.drift = buffer[i].drift;
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
  hasil.ok       = false;
  hasil.mode     = "otomatis";
  hasil.valveCmd = "tutup";
  hasil.pumpCmd  = "berhenti";

  if (!wifiTersambung()) return hasil;

  // order by id.desc (kolom identity, selalu naik) -> jangan pakai updated_at
  // karena kolom itu bisa NULL kalau tidak di-set eksplisit saat INSERT dari
  // web, sehingga urutan "terbaru" jadi tidak reliable.
  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + TABLE_MODE +
               "?select=id,mode,valve_cmd,pump_cmd&order=id.desc&limit=1";
  http.begin(url);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);

  int kodeHttp = http.GET();
  if (kodeHttp == 200) {
    String respon = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, respon);
    if (!err && doc.is<JsonArray>() && doc.size() > 0) {
      JsonObject baris = doc[0];
      hasil.mode     = String((const char*)(baris["mode"]     | "otomatis"));
      hasil.valveCmd = String((const char*)(baris["valve_cmd"] | "tutup"));
      hasil.pumpCmd  = String((const char*)(baris["pump_cmd"]  | "berhenti"));
      hasil.ok = true;
    } else {
      Serial.printf("[SUPABASE] Gagal parse kontrol: %s\n", respon.c_str());
    }
  } else {
    Serial.printf("[SUPABASE] GET system_control gagal, HTTP %d\n", kodeHttp);
  }
  http.end();
  return hasil;
}

bool bacaModeOperasiDariSupabase() {
  KontrolManual k = bacaKontrolDariSupabase();
  return k.mode == "otomatis";
}

bool kirimSampleKalibrasi(const char* titik, const char* aksi, int indexSample,
                           double ph, double suhu, double mv) {
  if (!wifiTersambung()) return false;

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/calibration_samples";
  http.begin(url);
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

bool kirimSampleKalibrasiTSS(const char* standard, const char* aksi, int indexSample,
                              double tss, double suhu, double ntu, double rawValue) {
  if (!wifiTersambung()) return false;

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/tss_calibration_samples";
  http.begin(url);
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
  payload += (isnan(ntu) ? "\"ntu\":null" : ("\"ntu\":" + String(ntu, 2))) + ",";
  payload += (isnan(rawValue) ? "\"raw_value\":null" : ("\"raw_value\":" + String(rawValue, 2)));
  payload += "}";

  int kodeHttp = http.POST(payload);
  http.end();
  return (kodeHttp == 200 || kodeHttp == 201);
}

