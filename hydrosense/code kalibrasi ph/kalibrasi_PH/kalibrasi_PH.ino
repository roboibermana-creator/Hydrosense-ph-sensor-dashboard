#include <Arduino.h>
#include <Preferences.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

#define PH_PIN 34

Preferences prefs;

// =====================================================
// KREDENSIAL WIFI & FIREBASE
// =====================================================

#define WIFI_SSID     "SPRM-CORP"
#define WIFI_PASSWORD "e$kr1mN@N@s3000"

#define DATABASE_URL "https://ph-sensor-monitor-11e27-default-rtdb.asia-southeast1.firebasedatabase.app"


// =====================================================
// TITIK KALIBRASI
// =====================================================

#define BUFFER_PH4_VALUE  4.01
#define BUFFER_PH7_VALUE  6.86
#define BUFFER_PH10_VALUE 9.18

struct CalPoint {
  bool   valid;
  double phValue;
  double voltage;
};

CalPoint calPoints[3] = {
  { false, BUFFER_PH4_VALUE,  0.0 },
  { false, BUFFER_PH7_VALUE,  0.0 },
  { false, BUFFER_PH10_VALUE, 0.0 }
};

double calSlope     = 0.0;
double calIntercept = 0.0;
int    calNumPoints = 0;

bool calibrated = false;


// =====================================================
// PARAMETER PEMBACAAN CEPAT
// =====================================================

#define SAMPLE_COUNT 50
#define SAMPLE_DELAY 20


// =====================================================
// PARAMETER PENGUMPULAN DATA KALIBRASI
// =====================================================

#define CAL_DURATION_MS 900000UL          // 15 menit tetap
#define CAL_STABLE_RANGE_MV 5.0
#define CAL_SAMPLE_INTERVAL_MS 5000UL

#define CAL_WARMUP_MS 120000UL            // 2 menit warm-up dibuang
#define CAL_OUTLIER_THRESHOLD_MV 15.0
#define CAL_MEDIAN_WINDOW 15


// =====================================================
// PARAMETER LOGGING KE FIREBASE
// =====================================================

#define FIREBASE_LOG_INTERVAL_MS 60000UL
#define FIREBASE_COMMAND_CHECK_MS 3000UL

bool wifiReady = false;


// =====================================================
// EMA UNTUK PEMBACAAN REAL-TIME (LOOP)
// =====================================================

double emaVoltage = 0.0;
const double EMA_ALPHA = 0.2;


// =====================================================
// HELPER FIREBASE REST API (PUT / POST / GET)
// =====================================================

String firebaseUrl(const String &path)
{
  return String(DATABASE_URL) + path + ".json";
}

bool firebasePut(const String &path, const String &jsonBody)
{
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

bool firebasePush(const String &path, const String &jsonBody)
{
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

String firebaseGetString(const String &path)
{
  if (WiFi.status() != WL_CONNECTED) return "";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, firebaseUrl(path));

  int code = http.GET();
  String result = "";

  if (code == 200)
  {
    result = http.getString();
    result.trim();
    result.replace("\"", "");
    if (result == "null") result = "";
  }

  http.end();

  return result;
}


// =====================================================
// KONEKSI WIFI & SINKRONISASI WAKTU (NTP)
// =====================================================

void setupWiFi()
{
  Serial.print("Menghubungkan ke WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000)
  {
    delay(300);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiReady = true;
    Serial.print("WiFi terhubung, IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    wifiReady = false;
    Serial.println("WiFi GAGAL terhubung. Lanjut mode OFFLINE (serial only).");
  }
}

void setupTime()
{
  if (!wifiReady) return;

  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com"); // WIB (+7)

  Serial.print("Sinkronisasi waktu NTP");

  time_t now = time(nullptr);
  unsigned long start = millis();

  while (now < 100000 && millis() - start < 10000)
  {
    delay(300);
    Serial.print(".");
    now = time(nullptr);
  }

  Serial.println();
  Serial.println(now >= 100000 ? "Waktu tersinkronisasi." : "Sinkronisasi waktu gagal, timestamp mungkin tidak akurat.");
}


// =====================================================
// PUSH DATA KE FIREBASE
// =====================================================

void firebaseLogReading(double voltage, double ph, bool hasPh)
{
  if (!wifiReady) return;

  String json = "{\"voltage\":" + String(voltage, 2);

  if (hasPh)
  {
    json += ",\"ph\":" + String(ph, 2);
  }

  json += ",\"timestamp\":" + String((unsigned long)time(nullptr));
  json += "}";

  firebasePut("/ph_sensor/latest", json);

  static unsigned long lastHistoryPush = 0;

  if (millis() - lastHistoryPush >= FIREBASE_LOG_INTERVAL_MS)
  {
    lastHistoryPush = millis();
    firebasePush("/ph_sensor/readings", json);
  }
}

void firebasePushCalStatus(bool running, const char *label, unsigned long elapsedSec,
                            int sampleNum, double avg, double range, const char *message)
{
  if (!wifiReady) return;

  String json = "{";
  json += "\"running\":";
  json += (running ? "true" : "false");
  json += ",\"label\":\"" + String(label) + "\"";
  json += ",\"elapsedSec\":" + String(elapsedSec);
  json += ",\"sample\":" + String(sampleNum);
  json += ",\"avgVoltage\":" + String(avg, 2);
  json += ",\"range\":" + String(range, 2);
  json += ",\"message\":\"" + String(message) + "\"";
  json += "}";

  firebasePut("/ph_sensor/calibration/status", json);
}

void firebasePushCalPoints()
{
  if (!wifiReady) return;

  const char *keys[3] = { "ph4", "ph7", "ph10" };

  String json = "{";

  for (int i = 0; i < 3; i++)
  {
    json += "\"" + String(keys[i]) + "\":{";
    json += "\"valid\":" + String(calPoints[i].valid ? "true" : "false") + ",";
    json += "\"phValue\":" + String(calPoints[i].phValue, 2) + ",";
    json += "\"voltage\":" + String(calPoints[i].voltage, 2);
    json += "}";

    if (i < 2) json += ",";
  }

  json += "}";

  firebasePut("/ph_sensor/calibration/points", json);

  String eq = "{";
  eq += "\"slope\":" + String(calSlope, 6) + ",";
  eq += "\"intercept\":" + String(calIntercept, 6) + ",";
  eq += "\"numPoints\":" + String(calNumPoints) + ",";
  eq += "\"calibrated\":" + String(calibrated ? "true" : "false");
  eq += "}";

  firebasePut("/ph_sensor/calibration/equation", eq);
}

String firebaseGetCommand()
{
  if (!wifiReady) return "";
  return firebaseGetString("/ph_sensor/calibration/command");
}

void firebaseClearCommand()
{
  if (!wifiReady) return;
  firebasePut("/ph_sensor/calibration/command", "\"\"");
}


// =====================================================
// BACA SATU BATCH TEGANGAN
// =====================================================

double readVoltage()
{
  uint32_t total = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    total += analogRead(PH_PIN);
    delay(SAMPLE_DELAY);
  }

  double adcAverage = total / (double)SAMPLE_COUNT;
  double voltage = (adcAverage / 4095.0) * 3300.0;

  return voltage;
}


// =====================================================
// PEMBACAAN LOOP DENGAN EMA
// =====================================================

double readVoltageSmoothed()
{
  double v = readVoltage();

  if (emaVoltage == 0.0)
  {
    emaVoltage = v;
  }
  else
  {
    emaVoltage = EMA_ALPHA * v + (1 - EMA_ALPHA) * emaVoltage;
  }

  return emaVoltage;
}


// =====================================================
// HITUNG ULANG PERSAMAAN KALIBRASI (LEAST-SQUARES FIT)
// =====================================================

void computeCalibration()
{
  double sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
  int n = 0;

  for (int i = 0; i < 3; i++)
  {
    if (calPoints[i].valid)
    {
      double x = calPoints[i].phValue;
      double y = calPoints[i].voltage;

      sumX  += x;
      sumY  += y;
      sumXY += x * y;
      sumXX += x * x;
      n++;
    }
  }

  calNumPoints = n;

  if (n < 2)
  {
    calibrated = false;
    return;
  }

  double denom = (n * sumXX) - (sumX * sumX);

  if (abs(denom) < 1e-9)
  {
    calibrated = false;
    return;
  }

  calSlope     = ((n * sumXY) - (sumX * sumY)) / denom;
  calIntercept = (sumY - (calSlope * sumX)) / n;

  calibrated = true;
}


// =====================================================
// HITUNG pH DARI TEGANGAN
// =====================================================

double calculatePH(double voltage)
{
  if (!calibrated)
    return -1;

  if (abs(calSlope) < 1e-6)
    return -1;

  double ph = (voltage - calIntercept) / calSlope;

  return ph;
}


// =====================================================
// TUNGGU DENGAN CEK BATAL ('X' dari serial ATAU remote)
// =====================================================

bool waitWithAbortCheck(unsigned long ms)
{
  unsigned long start = millis();

  while (millis() - start < ms)
  {
    if (Serial.available())
    {
      char c = Serial.read();

      if (c == 'X' || c == 'x')
      {
        return true;
      }
    }

    delay(50);
  }

  return false;
}


// =====================================================
// HITUNG MEDIAN DARI WINDOW SAMPLE TERAKHIR
// =====================================================

double computeMedian(double *buffer, int n)
{
  double sorted[CAL_MEDIAN_WINDOW];
  for (int i = 0; i < n; i++) sorted[i] = buffer[i];

  for (int i = 1; i < n; i++)
  {
    double key = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > key)
    {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = key;
  }

  if (n % 2 == 1)
    return sorted[n / 2];
  else
    return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
}


// =====================================================
// KUMPULKAN DATA SELAMA 15 MENIT (WARM-UP + OUTLIER REJECT)
// =====================================================

bool collectStableReading(double &resultVoltage, bool &wasStable, const char *label)
{
  unsigned long startTime = millis();

  double sum = 0;
  int count = 0;
  int totalSamples = 0;
  double minV = 99999;
  double maxV = -99999;

  double medianBuf[CAL_MEDIAN_WINDOW];
  int medianCount = 0;
  int medianIndex = 0;

  Serial.println();
  Serial.println("Mengumpulkan data kalibrasi...");
  Serial.print("Durasi TETAP ");
  Serial.print(CAL_DURATION_MS / 60000UL);
  Serial.println(" menit (tidak berhenti lebih awal, demi akurasi).");
  Serial.print("Warm-up ");
  Serial.print(CAL_WARMUP_MS / 1000UL);
  Serial.println(" detik pertama dibuang. Outlier > 15 mV dari median otomatis dibuang.");
  Serial.println("Tekan 'X' di serial ATAU kirim command 'cancel' dari web untuk BATAL.");
  Serial.println();

  while (true)
  {
    double v = readVoltage();
    totalSamples++;

    unsigned long elapsed = millis() - startTime;

    bool inWarmup = (elapsed < CAL_WARMUP_MS);
    bool isOutlier = false;

    if (!inWarmup)
    {
      if (medianCount < CAL_MEDIAN_WINDOW)
      {
        medianBuf[medianCount] = v;
        medianCount++;
      }
      else
      {
        medianBuf[medianIndex] = v;
        medianIndex = (medianIndex + 1) % CAL_MEDIAN_WINDOW;
      }

      if (medianCount >= 5)
      {
        double med = computeMedian(medianBuf, medianCount);
        if (abs(v - med) > CAL_OUTLIER_THRESHOLD_MV)
        {
          isOutlier = true;
        }
      }
    }

    bool used = !inWarmup && !isOutlier;

    if (used)
    {
      sum += v;
      count++;

      if (v < minV) minV = v;
      if (v > maxV) maxV = v;
    }

    double range = (count > 0) ? (maxV - minV) : 0.0;
    double average = (count > 0) ? (sum / count) : 0.0;

    Serial.print("[");
    Serial.print(elapsed / 1000UL);
    Serial.print("s] Sample #");
    Serial.print(totalSamples);
    Serial.print("  V=");
    Serial.print(v, 2);
    Serial.print(" mV  Avg=");
    Serial.print(average, 2);
    Serial.print(" mV  Range=");
    Serial.print(range, 2);
    Serial.print(" mV");

    const char *statusMsg = "ok";

    if (inWarmup)       { Serial.print("  [WARM-UP, dibuang]"); statusMsg = "warmup"; }
    else if (isOutlier) { Serial.print("  [OUTLIER, dibuang]"); statusMsg = "outlier"; }

    Serial.println();

    firebasePushCalStatus(true, label, elapsed / 1000UL, count, average, range, statusMsg);

    if (elapsed >= CAL_DURATION_MS)
    {
      Serial.println();
      Serial.println("STATUS: DURASI 15 MENIT SELESAI.");
      Serial.print("Sample terpakai = ");
      Serial.print(count);
      Serial.print(" / ");
      Serial.print(totalSamples);
      Serial.println(" total sample diambil");
      Serial.print("Range akhir   = ");
      Serial.print(range, 2);
      Serial.println(" mV");
      Serial.print("Rata-rata final = ");
      Serial.print(average, 2);
      Serial.println(" mV");

      const char *finalMsg;

      if (count < 10)
      {
        Serial.println("PERINGATAN: sample valid terlalu sedikit,");
        Serial.println("cek probe/wiring, mungkin banyak noise.");
        wasStable = false;
        finalMsg = "sample_kurang";
      }
      else if (range <= CAL_STABLE_RANGE_MV)
      {
        Serial.println("Data tergolong STABIL (range <= 5 mV).");
        wasStable = true;
        finalMsg = "stabil";
      }
      else
      {
        Serial.println("PERHATIAN: range masih > 5 mV.");
        Serial.println("Saran: cek probe/larutan, kemungkinan probe");
        Serial.println("perlu direndam ulang atau sudah waktunya diganti.");
        wasStable = false;
        finalMsg = "belum_stabil";
      }

      Serial.println("--------------------------------");

      firebasePushCalStatus(false, label, elapsed / 1000UL, count, average, range, finalMsg);

      resultVoltage = average;
      return true;
    }

    String remoteCmd = firebaseGetCommand();
    if (remoteCmd == "cancel")
    {
      firebaseClearCommand();

      Serial.println();
      Serial.println("================================");
      Serial.println("   KALIBRASI DIBATALKAN (dari WEB)");
      Serial.println("================================");
      Serial.println();

      firebasePushCalStatus(false, label, elapsed / 1000UL, count, average, range, "dibatalkan");

      resultVoltage = average;
      return false;
    }

    if (waitWithAbortCheck(CAL_SAMPLE_INTERVAL_MS))
    {
      Serial.println();
      Serial.println("================================");
      Serial.println("   KALIBRASI DIBATALKAN USER");
      Serial.println("================================");
      Serial.println("Tidak ada data yang disimpan.");
      Serial.println();

      firebasePushCalStatus(false, label, elapsed / 1000UL, count, average, range, "dibatalkan");

      resultVoltage = average;
      return false;
    }
  }
}


// =====================================================
// KALIBRASI GENERIK UNTUK 1 TITIK BUFFER
// =====================================================

void calibratePoint(int pointIndex, const char *label, const char *prefsKey)
{
  Serial.println();
  Serial.println("================================");
  Serial.print("       KALIBRASI ");
  Serial.println(label);
  Serial.println("================================");

  Serial.println();
  Serial.println("1. Bilas probe dengan distilled water.");
  Serial.println("2. Keringkan sisa air secara perlahan.");
  Serial.print("3. Masukkan probe ke buffer ");
  Serial.println(label);
  Serial.println("4. Aduk perlahan.");
  Serial.println();
  Serial.println("JANGAN tekan tombol apa pun (kecuali mau batal).");
  Serial.println("Program akan mengumpulkan data selama 15 menit penuh.");
  Serial.println();

  delay(3000);

  double voltage = 0.0;
  bool wasStable = false;

  bool completed = collectStableReading(voltage, wasStable, label);

  if (!completed)
  {
    return;
  }

  calPoints[pointIndex].voltage = voltage;
  calPoints[pointIndex].valid   = true;

  prefs.putDouble(prefsKey, voltage);

  Serial.println();
  Serial.println("================================");
  Serial.print("      ");
  Serial.print(label);
  Serial.println(" TERSIMPAN");
  Serial.println("================================");

  Serial.print("Voltage = ");
  Serial.print(voltage, 2);
  Serial.println(" mV");

  Serial.print("Status kestabilan: ");
  Serial.println(wasStable ? "STABIL (<=5 mV)" : "BELUM STABIL (range > 5 mV)");

  computeCalibration();

  firebasePushCalPoints();

  Serial.println();

  if (calibrated)
  {
    Serial.println("================================");
    Serial.print("   PERSAMAAN KALIBRASI DIPERBARUI (");
    Serial.print(calNumPoints);
    Serial.println(" titik)");
    Serial.println("================================");

    Serial.print("V = ");
    Serial.print(calSlope, 4);
    Serial.print(" * pH + ");
    Serial.println(calIntercept, 4);

    Serial.println();
  }
  else
  {
    Serial.println("Minimal 2 titik kalibrasi dibutuhkan sebelum bisa menghitung pH.");
    Serial.println();
  }
}

void calibratePH4()
{
  calibratePoint(0, "pH 4.01", "ph4v");
}

void calibratePH7()
{
  calibratePoint(1, "pH 6.86", "ph7v");
}

void calibratePH10()
{
  calibratePoint(2, "pH 9.18", "ph10v");
}


// =====================================================
// RESET KALIBRASI
// =====================================================

void resetCalibration()
{
  prefs.clear();

  for (int i = 0; i < 3; i++)
  {
    calPoints[i].valid = false;
    calPoints[i].voltage = 0.0;
  }

  calSlope = 0.0;
  calIntercept = 0.0;
  calNumPoints = 0;
  calibrated = false;

  firebasePushCalPoints();

  Serial.println();
  Serial.println("================================");
  Serial.println("     KALIBRASI DIHAPUS");
  Serial.println("================================");
  Serial.println();
}


// =====================================================
// STATUS KALIBRASI
// =====================================================

void showStatus()
{
  Serial.println();
  Serial.println("================================");
  Serial.println("          STATUS SENSOR");
  Serial.println("================================");

  const char *labels[3] = { "pH 4.01 ", "pH 6.86 ", "pH 9.18 " };

  for (int i = 0; i < 3; i++)
  {
    Serial.print(labels[i]);
    Serial.print(": ");

    if (calPoints[i].valid)
    {
      Serial.print(calPoints[i].voltage, 2);
      Serial.println(" mV");
    }
    else
    {
      Serial.println("BELUM ADA");
    }
  }

  Serial.println();
  Serial.print("Jumlah titik terpakai : ");
  Serial.println(calNumPoints);

  if (calibrated)
  {
    Serial.print("Persamaan   : V = ");
    Serial.print(calSlope, 4);
    Serial.print(" * pH + ");
    Serial.println(calIntercept, 4);
  }

  Serial.print("Calibration  : ");
  Serial.println(calibrated ? "OK" : "BELUM");

  Serial.print("WiFi/Firebase : ");
  Serial.println(wifiReady ? "TERHUBUNG" : "OFFLINE");

  Serial.println("================================");
  Serial.println();
}


// =====================================================
// CEK COMMAND DARI WEB (kalibrasi remote)
// =====================================================

void checkFirebaseCommand()
{
  if (!wifiReady) return;

  static unsigned long lastCheck = 0;

  if (millis() - lastCheck < FIREBASE_COMMAND_CHECK_MS) return;
  lastCheck = millis();

  String cmd = firebaseGetCommand();

  if (cmd == "4")
  {
    firebaseClearCommand();
    calibratePH4();
  }
  else if (cmd == "7")
  {
    firebaseClearCommand();
    calibratePH7();
  }
  else if (cmd == "10")
  {
    firebaseClearCommand();
    calibratePH10();
  }
  else if (cmd == "reset")
  {
    firebaseClearCommand();
    resetCalibration();
  }
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1500);

  analogReadResolution(12);
  analogSetPinAttenuation(PH_PIN, ADC_11db);

  prefs.begin("ph-cal", false);

  const char *prefsKeys[3] = { "ph4v", "ph7v", "ph10v" };

  for (int i = 0; i < 3; i++)
  {
    double v = prefs.getDouble(prefsKeys[i], -1.0);

    if (v > 0)
    {
      calPoints[i].voltage = v;
      calPoints[i].valid = true;
    }
  }

  computeCalibration();

  setupWiFi();
  setupTime();

  if (wifiReady)
  {
    firebasePushCalPoints();
  }

  Serial.println();
  Serial.println("========================================");
  Serial.println(" DFRobot Gravity pH V2.0");
  Serial.println(" ESP32-S3");
  Serial.println(" Multi-Point Calibration + Firebase Logging");
  Serial.println("========================================");

  Serial.println();

  if (calibrated)
  {
    Serial.print("Data kalibrasi ditemukan (");
    Serial.print(calNumPoints);
    Serial.println(" titik).");

    Serial.print("Persamaan: V = ");
    Serial.print(calSlope, 4);
    Serial.print(" * pH + ");
    Serial.println(calIntercept, 4);
  }
  else
  {
    Serial.println("Sensor BELUM dikalibrasi (minimal 2 titik dibutuhkan).");
  }

  Serial.println();
  Serial.println("COMMAND (serial atau dari web):");
  Serial.println(" 4 = Kalibrasi buffer pH 4.01");
  Serial.println(" 7 = Kalibrasi buffer pH 6.86");
  Serial.println(" 1 = Kalibrasi buffer pH 9.18 (opsional, titik ke-3)");
  Serial.println(" X = Batal / keluar dari proses kalibrasi yang berjalan");
  Serial.println(" R = Reset semua kalibrasi");
  Serial.println(" S = Status");
  Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
  if (Serial.available())
  {
    char command = Serial.read();

    if (command == '4')
    {
      calibratePH4();
    }
    else if (command == '7')
    {
      calibratePH7();
    }
    else if (command == '1')
    {
      calibratePH10();
    }
    else if (command == 'R' || command == 'r')
    {
      resetCalibration();
    }
    else if (command == 'S' || command == 's')
    {
      showStatus();
    }
  }

  checkFirebaseCommand();

  static unsigned long lastRead = 0;

  if (millis() - lastRead >= 2000)
  {
    lastRead = millis();

    double voltage = readVoltageSmoothed();

    Serial.print("Voltage : ");
    Serial.print(voltage, 2);
    Serial.print(" mV");

    double ph = -1;
    bool hasPh = false;

    if (calibrated)
    {
      ph = calculatePH(voltage);
      hasPh = (ph >= 0);

      Serial.print("    pH : ");

      if (hasPh)
      {
        Serial.println(ph, 2);
      }
      else
      {
        Serial.println("ERROR");
      }
    }
    else
    {
      Serial.println("    pH : BELUM KALIBRASI");
    }

    firebaseLogReading(voltage, ph, hasPh);
  }
}