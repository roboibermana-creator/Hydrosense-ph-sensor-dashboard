#include "Network.h"
#include "Config.h"
#include "Sensor.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

bool wifiReady = false;

String firebaseUrl(const String &path) {
  return String(DATABASE_URL) + path + ".json";
}

bool firebasePut(const String &path, const String &jsonBody) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, firebaseUrl(path));
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(jsonBody);
  http.end();
  return (code == 200);
}

bool firebasePush(const String &path, const String &jsonBody) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, firebaseUrl(path));
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(jsonBody);
  http.end();
  return (code == 200);
}

String firebaseGetString(const String &path) {
  if (WiFi.status() != WL_CONNECTED) return "";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, firebaseUrl(path));
  int code = http.GET();
  String result = "";
  if (code == 200) {
    result = http.getString();
    result.trim();
    result.replace("\"", "");
    if (result == "null") result = "";
  }
  http.end();
  return result;
}

void setupWiFi() {
  Serial.print("Menghubungkan ke WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    Serial.print("WiFi terhubung, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiReady = false;
    Serial.println("WiFi GAGAL terhubung. Lanjut mode OFFLINE.");
  }
}

void setupTime() {
  if (!wifiReady) return;
  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
  Serial.print("Sinkronisasi waktu NTP");
  time_t now = time(nullptr);
  unsigned long start = millis();
  while (now < 100000 && millis() - start < 5000) {
    delay(300);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();
  Serial.println(now >= 100000 ? "Waktu tersinkronisasi." : "Sinkronisasi gagal.");
}

static String jsonNum(double v, int digits) { return isnan(v) ? String("null") : String(v, digits); }

// Tahap [7]: kirim nilai yang sudah siap tampil + metadata mutu.
// Kunci "ph", "temp", "timestamp" dipertahankan karena dibaca dashboard.
// Saat quality = error, TIDAK ada angka pH/suhu yang dikirim.
void firebaseLogReading(const PhReading &r) {
  if (!wifiReady) return;
  String json = "{";
  json += "\"device_id\":\"" DEVICE_ID "\"";
  json += ",\"timestamp\":" + String((unsigned long)time(nullptr));
  json += ",\"quality\":\"" + String(r.quality) + "\"";
  if (r.valid) {
    json += ",\"ph\":" + String(r.ph, 2);
    json += ",\"temp\":" + String(r.tempC, 1);
    json += ",\"ph_status\":\"" + String(r.phStatus) + "\"";
    json += ",\"baku_mutu\":{\"ph_min\":" + String(BAKU_MUTU_PH_MIN, 1)
          + ",\"ph_max\":" + String(BAKU_MUTU_PH_MAX, 1)
          + ",\"compliant\":" + String(r.compliant ? "true" : "false") + "}";
  }
  json += ",\"diagnostics\":{";
  json += "\"raw_register_ph\":" + String(r.rawPh);
  json += ",\"raw_register_temp\":" + String(r.rawTemp);
  json += ",\"sample_count\":" + String(r.sampleCount);
  json += ",\"sd_ph\":" + jsonNum(r.sd, 3);
  json += ",\"drift_ph_per_min\":" + jsonNum(r.drift, 3);
  json += ",\"modbus_errors\":" + String(r.modbusErrors) + "}";
  json += ",\"calibration\":{";
  json += "\"last_calibrated\":" + String(lastCalibrated);
  json += ",\"r_squared\":" + (verif.r2 < 0 ? String("null") : String(verif.r2, 5));
  json += ",\"max_error_ph\":" + (verif.n ? String(verif.maxErr, 3) : String("null"));
  json += ",\"verified\":" + String(verif.pass ? "true" : "false") + "}";
  json += "}";

  firebasePut("/ph_sensor/latest", json);
  static unsigned long lastHistoryPush = 0;
  if (millis() - lastHistoryPush >= FIREBASE_LOG_INTERVAL_MS) {
    lastHistoryPush = millis();
    firebasePush("/ph_sensor/readings", json);
  }
}

void firebasePushCalStatus(bool running, const char *label, unsigned long elapsedSec, int sampleNum, double avg, double range, const char *message) {
  if (!wifiReady) return;
  String json = "{";
  json += "\"running\":" + String(running ? "true" : "false");
  json += ",\"label\":\"" + String(label) + "\"";
  json += ",\"elapsedSec\":" + String(elapsedSec);
  json += ",\"sample\":" + String(sampleNum);
  json += ",\"avgPh\":" + String(avg, 3);     // rata-rata jendela stabil (pH)
  json += ",\"sdPh\":" + String(range, 3);     // sd jendela (pH)
  json += ",\"message\":\"" + String(message) + "\"}";
  firebasePut("/ph_sensor/calibration/status", json);
}

static String pointJson(const CalPoint &p) {
  String j = "{\"valid\":" + String(p.valid ? "true" : "false");
  j += ",\"phValue\":" + String(p.phValue, 2);
  j += ",\"reading\":" + String(p.reading, 3);
  j += ",\"mv\":" + jsonNum(p.mv, 1);
  j += ",\"when\":" + String(p.when) + "}";
  return j;
}

// Riwayat kalibrasi dipisah dari readings (bagian 7). Struktur
// calibration/points {ph4, ph7, ph10}.valid dipertahankan untuk dashboard:
// ph4/ph10 = titik kalibrasi yang sudah ditulis ke sensor, ph7 = verifikasi 6.86.
void firebasePushCalPoints() {
  if (!wifiReady) return;
  String json = "{";
  json += "\"ph4\":"  + pointJson(calPoints[0]);
  json += ",\"ph7\":"  + pointJson(verifPoints[1]);
  json += ",\"ph10\":" + pointJson(calPoints[2]);
  json += "}";
  firebasePut("/ph_sensor/calibration/points", json);

  String v = "{";
  v += "\"calibrated\":" + String(calibrated ? "true" : "false");
  v += ",\"last_calibrated\":" + String(lastCalibrated);
  v += ",\"verification\":{\"n\":" + String(verif.n);
  v += ",\"m\":" + String(verif.m, 4) + ",\"b\":" + String(verif.b, 4);
  v += ",\"r2\":" + (verif.r2 < 0 ? String("null") : String(verif.r2, 5));
  v += ",\"rmse\":" + String(verif.rmse, 3) + ",\"max_error\":" + String(verif.maxErr, 3);
  v += ",\"pass\":" + String(verif.pass ? "true" : "false");
  v += ",\"points\":{\"ph4\":" + pointJson(verifPoints[0]) + ",\"ph7\":" + pointJson(verifPoints[1])
     + ",\"ph10\":" + pointJson(verifPoints[2]) + "}}}";
  firebasePut("/ph_sensor/calibration/equation", v);
}

String firebaseGetCommand() {
  if (!wifiReady) return "";
  return firebaseGetString("/ph_sensor/calibration/command");
}

void firebaseClearCommand() {
  if (!wifiReady) return;
  firebasePut("/ph_sensor/calibration/command", "\"\"");
}

void checkFirebaseCommand() {
  if (!wifiReady) return;
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < FIREBASE_COMMAND_CHECK_MS) return;
  lastCheck = millis();

  String cmd = firebaseGetCommand();
  if      (cmd == "4")     { firebaseClearCommand(); calibratePH4(); }    // tulis titik 1
  else if (cmd == "10")    { firebaseClearCommand(); calibratePH10(); }   // tulis titik 2
  else if (cmd == "7")     { firebaseClearCommand(); verifyPH7(); }       // verifikasi 6.86
  else if (cmd == "v4")    { firebaseClearCommand(); verifyPH4(); }
  else if (cmd == "v10")   { firebaseClearCommand(); verifyPH10(); }
  else if (cmd == "reset") { firebaseClearCommand(); resetCalibration(); }
}