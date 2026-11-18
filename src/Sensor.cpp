#include "Sensor.h"
#include "Config.h"
#include "Network.h"
#include "PhStability.h"
#include <ModbusMaster.h>
#include <time.h>

Preferences prefs;

CalPoint calPoints[3] = {{false, BUFFER_PH4_VALUE, 0.0, NAN, 0},
                         {false, BUFFER_PH7_VALUE, 0.0, NAN, 0},
                         {false, BUFFER_PH10_VALUE, 0.0, NAN, 0}};
CalPoint verifPoints[3] = {{false, BUFFER_PH4_VALUE, 0.0, NAN, 0},
                           {false, BUFFER_PH7_VALUE, 0.0, NAN, 0},
                           {false, BUFFER_PH10_VALUE, 0.0, NAN, 0}};
Verification verif = {0, 0, 0, -1, 0, 0, false};
bool calibrated = false;
unsigned long lastCalibrated = 0;
double lastTempC = NAN;
bool sensorOnline = false;

// ---------------------------------------------------------------------
// Parameter waktu kalibrasi. Ditaruh di sini supaya Config.h tetap ringkas.
// ---------------------------------------------------------------------
#ifndef CAL_MAX_SAMPLES
#define CAL_MAX_SAMPLES                                                        \
  30 // maksimal 30 sampel berguna (~60 detik) setelah warm-up
#endif
#ifndef CAL_SAMPLE_MS
#define CAL_SAMPLE_MS 2000UL // jeda antar sampel saat kalibrasi
#endif

// Koreksi linear di sisi ESP32 dari titik buffer MANA SAJA yang sudah diukur
// (4.01, 6.86, 9.18 -> indeks 0/1/2), minimal 1 titik:
//   pH_benar = m * pH_sensor + b + swOff
//   1 titik       : m = 1, b = pH_ref - r            (hanya offset)
//   2 atau 3 titik: regresi least squares pH_ref ~ r (slope + offset)
// r = pembacaan MENTAH sensor di buffer. swOff = trim offset manual (pH) dari
// dashboard, ditambahkan di atasnya. Aktif untuk titik yang sensor-nya menolak
// tulis kalibrasi (kalau sensor menerima, sensor sendiri yang mengoreksi dan
// koreksi software dimatikan supaya tidak dobel).
static double swRaw[3] = {NAN, NAN, NAN};
static bool softOn = false;
static double softM = 1.0, softB = 0.0;
static double swOff = 0.0; // trim offset manual (pH)
static bool phBacaMentah = false; // true saat mengumpulkan titik kalibrasi

static PhStability stbLive; // jendela stabilitas operasi normal
static PhStability stbCal;  // jendela stabilitas saat kalibrasi/verifikasi

static unsigned long epochNow() {
  time_t t = time(nullptr);
  return (t >= 100000) ? (unsigned long)t : 0;
}

// =====================================================================
//  Modbus RS485 (MAX485 di Serial2). DE/RE HIGH hanya saat kirim.
// =====================================================================
static ModbusMaster node;

static void preTransmission() { digitalWrite(RS485_RE_DE_PIN, HIGH); }
static void postTransmission() { digitalWrite(RS485_RE_DE_PIN, LOW); }

// Tahap [1]-[3]: satu transaksi membaca blok register terendah..tertinggi
// (0x0001..0x0007), lalu penskalaan dan gerbang validitas.
bool readModbusSensor(double &ph, double &tempC, uint16_t *rawPh,
                      int16_t *rawTemp, double *mv) {
  const uint16_t first =
      min((uint16_t)PH_REGISTER_ADDRESS,
          min((uint16_t)TEMP_REGISTER_ADDRESS, (uint16_t)MV_REGISTER_ADDRESS));
  const uint16_t last =
      max((uint16_t)PH_REGISTER_ADDRESS,
          max((uint16_t)TEMP_REGISTER_ADDRESS, (uint16_t)MV_REGISTER_ADDRESS));

  uint8_t result = node.readHoldingRegisters(first, last - first + 1);
  if (result != node.ku8MBSuccess) { // termasuk CRC salah / timeout
    sensorOnline = false;
    return false;
  }

  uint16_t rp = node.getResponseBuffer(PH_REGISTER_ADDRESS - first);
  int16_t rt = (int16_t)node.getResponseBuffer(TEMP_REGISTER_ADDRESS -
                                               first); // cast WAJIB
  int16_t rm = (int16_t)node.getResponseBuffer(MV_REGISTER_ADDRESS - first);

  double p = rp / PH_SCALE;
  double t = rt / TEMP_SCALE;

  // Gerbang validitas (bagian 5.1)
  if (p < PH_VALID_MIN || p > PH_VALID_MAX || t < TEMP_VALID_MIN ||
      t > TEMP_VALID_MAX) {
    sensorOnline = true; // bus hidup, datanya yang tidak masuk akal
    return false;
  }

#if PH_KOMPENSASI_SUHU
  // Berlaku juga untuk titik kalibrasi (domain "setara 25 C"), jadi kalibrasi
  // dan pengukuran tidak tergantung suhu buffer/air.
  p = 7.0 + (p - 7.0) * (273.15 + PH_SUHU_REFERENSI_C) / (273.15 + t);
#endif
  if (!phBacaMentah) {
    if (softOn)
      p = softM * p + softB;
    p += swOff;
  }
  ph = p;
  tempC = t;
  lastTempC = t;
  sensorOnline = true;
  if (rawPh)
    *rawPh = rp;
  if (rawTemp)
    *rawTemp = rt;
  if (mv)
    *mv = rm / MV_SCALE;
  return true;
}

static double medianOf(double *v, int n) {
  for (int i = 1; i < n; i++) { // insertion sort, n kecil
    double x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = x;
  }
  return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static const char *classifyPh(double ph) {
  if (ph < BAKU_MUTU_PH_MIN)
    return "acidic";
  if (ph > BAKU_MUTU_PH_MAX)
    return "alkaline";
  return "normal";
}

// Tahap [4]-[6]: median N sampel, gerbang stabilitas, pembulatan.
PhReading readSensor() {
  PhReading r = {false, NAN, NAN, NAN, 0,       0,         0,
                 0,     NAN, NAN, "error", "unknown", false};
  double phs[MEDIAN_SAMPLES], temps[MEDIAN_SAMPLES], mvs[MEDIAN_SAMPLES];

  for (int i = 0; i < MEDIAN_SAMPLES; i++) {
    double p, t, m;
    uint16_t rp;
    int16_t rt;
    if (readModbusSensor(p, t, &rp, &rt, &m)) {
      mvs[r.sampleCount] = m;
      phs[r.sampleCount] = p;
      temps[r.sampleCount] = t;
      r.sampleCount++;
      r.rawPh = rp;
      r.rawTemp = rt;
    } else {
      r.modbusErrors++;
    }
    if (i < MEDIAN_SAMPLES - 1)
      delay(SAMPLE_INTERVAL_MS);
  }
  if (r.sampleCount == 0)
    return r; // quality = error, tanpa angka pH

  double ph = medianOf(phs, r.sampleCount);
  double tc = medianOf(temps, r.sampleCount);
  r.mv = medianOf(mvs, r.sampleCount);

  stbLive.add(ph);
  StabilityStats s = stbLive.evaluate();
  r.sd = s.valid ? s.sd : NAN;
  r.drift = s.valid ? s.drift : NAN;

  if (!s.valid)
    r.quality = "fair"; // jendela belum penuh (~30 s pertama)
  else if (s.stable)
    r.quality = "good";
  else if (s.acceptable)
    r.quality = "fair";
  else
    r.quality = "unstable";

  r.valid = true;
  r.ph = round(ph * 100.0) / 100.0;  // resolusi 0,01
  r.tempC = round(tc * 10.0) / 10.0; // resolusi 0,1
  r.phStatus = classifyPh(r.ph);
  bool usable =
      (strcmp(r.quality, "good") == 0) || (strcmp(r.quality, "fair") == 0);
  r.compliant =
      usable && (r.ph >= BAKU_MUTU_PH_MIN) && (r.ph <= BAKU_MUTU_PH_MAX);
  return r;
}

// =====================================================================
//  Kalibrasi & verifikasi
// =====================================================================
static bool waitWithAbortCheck(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'X' || c == 'x')
        return true;
    }
    delay(50);
  }
  return false;
}

// Gerbang stabilitas sebelum kalibrasi/verifikasi (bagian 5.2).
// Hasil = rata-rata jendela stabil (pH) dan rata-rata mV sesudah warm-up.
static bool collectStableReading(double &resultPh, double &resultMv,
                                 bool &wasStable, const char *label,
                                 const char *titikDb, const char *aksiDb) {
  unsigned long startTime = millis();
  int totalSamples = 0, mvCount = 0, sampelBerguna = 0;
  double mvSum = 0;

  stbCal.reset();

  Serial.println();
  Serial.println(F("Menunggu pembacaan stabil..."));
  Serial.printf("Warm-up %lu detik dibuang.\n", CAL_WARMUP_MS / 1000UL);
  Serial.printf(
      "Selesai otomatis bila sd <= %.2f pH DAN drift <= %.2f pH/menit.\n",
      STB_SD_GOOD_MV, STB_DRIFT_GOOD_MV);
  Serial.printf("Maksimal %d sampel (~%lu detik) setelah warm-up.\n",
                CAL_MAX_SAMPLES,
                (unsigned long)CAL_MAX_SAMPLES * CAL_SAMPLE_MS / 1000UL);
  Serial.println(F("'X' di serial atau 'cancel' dari web untuk batal."));
  Serial.println();

  while (true) {
    double ph, tc, mv;
    bool ok = readModbusSensor(ph, tc, nullptr, nullptr, &mv);
    totalSamples++;

    unsigned long elapsed = millis() - startTime;
    bool inWarmup = (elapsed < CAL_WARMUP_MS);

    if (ok && !inWarmup) {
      stbCal.add(ph);
      mvSum += mv;
      mvCount++;
      sampelBerguna++;
      kirimSampleKalibrasi(titikDb, aksiDb, sampelBerguna, ph, tc, mv);
    }

    StabilityStats s = stbCal.evaluate();

    // Lapor progres ke dashboard (hanya untuk perintah dari web), tiap >= 6 dtk.
    static unsigned long progresTerakhir = 0;
    if (phCalIdPerintahWeb >= 0 && millis() - progresTerakhir >= 6000UL) {
      progresTerakhir = millis();
      char buf[120];
      if (!ok)
        snprintf(buf, sizeof(buf), "[%lus] membaca sensor gagal...",
                 elapsed / 1000UL);
      else if (inWarmup)
        snprintf(buf, sizeof(buf), "Warm-up %lu/%lu dtk | pH %.2f (dibuang)",
                 elapsed / 1000UL, CAL_WARMUP_MS / 1000UL, ph);
      else
        snprintf(buf, sizeof(buf),
                 "Sampel %d/%d | pH %.2f | sd %.3f | drift %.3f pH/mnt (butuh "
                 "<= %.2f)",
                 sampelBerguna, CAL_MAX_SAMPLES, ph, s.valid ? s.sd : 0.0,
                 s.valid ? s.drift : 0.0, STB_DRIFT_OK_MV);
      laporkanProgresKalibrasiPh(phCalIdPerintahWeb, buf);
    }

    if (!ok)
      Serial.printf("[%lus] #%d  pH=GAGAL", elapsed / 1000UL, totalSamples);
    else
      Serial.printf("[%lus] #%d  pH=%.2f  T=%.1f C  mV=%.1f", elapsed / 1000UL,
                    totalSamples, ph, tc, mv);
    if (inWarmup)
      Serial.println(F("   [WARM-UP, dibuang]"));
    else {
      Serial.println();
      stbCal.printStatus(s);
    }

    bool cukupSampel = (sampelBerguna >= CAL_MAX_SAMPLES);

    if (!inWarmup && ((s.valid && s.stable) || cukupSampel)) {
      const char *finalMsg;
      Serial.println(F("\n--------------------------------"));
      if (!s.valid) {
        Serial.println(F(
            "GAGAL: sampel valid terlalu sedikit. Cek kabel A/B / catu daya."));
        wasStable = false;
        finalMsg = "sample_kurang";
      } else if (s.stable) {
        Serial.println(F("STATUS: STABIL."));
        wasStable = true;
        finalMsg = "stabil";
      } else if (s.acceptable) {
        Serial.println(
            F("STATUS: cukup stabil (belum ideal, masih bisa dipakai)."));
        wasStable = true;
        finalMsg = "cukup_stabil";
      } else {
        Serial.println(F("STATUS: BELUM STABIL sampai batas sampel."));
        wasStable = false;
        finalMsg = "belum_stabil";
      }
      resultPh = s.mean;
      resultMv = mvCount ? mvSum / mvCount : NAN;
      Serial.printf("Hasil      = %.3f pH\n", s.mean);
      Serial.printf("sd / drift = %.3f pH / %.3f pH per menit\n", s.sd,
                    s.drift);
      Serial.println(F("--------------------------------"));
      return s.valid;
    }

    if (waitWithAbortCheck(CAL_SAMPLE_MS)) {
      Serial.println(F("\n>> DIBATALKAN USER. Tidak ada yang ditulis.\n"));
      return false;
    }
  }
}

// Tulis titik kalibrasi ke sensor: reg 0x0120 = langkah, 0x0121 = round(pH x
// 100).
static void hitungKoreksiSoft() {
  softOn = false;
  softM = 1.0;
  softB = 0.0;
  if (!prefs.getBool("sw_on", false))
    return;
  double sx = 0, sy = 0, sxy = 0, sxx = 0;
  int n = 0;
  for (int i = 0; i < 3; i++) {
    if (isnan(swRaw[i]))
      continue;
    const double x = swRaw[i], y = calPoints[i].phValue;
    sx += x;
    sy += y;
    sxy += x * y;
    sxx += x * x;
    n++;
  }
  if (n == 0)
    return;
  if (n == 1) { // satu titik -> hanya geser (offset), slope dianggap 1
    softB = sy - sx;
    softOn = true;
    return;
  }
  const double den = n * sxx - sx * sx;
  if (fabs(den) < 0.25)
    return; // titik terlalu berdekatan -> koreksi tidak masuk akal
  const double m = (n * sxy - sx * sy) / den;
  if (m < 0.7 || m > 1.3)
    return; // kemiringan di luar wajar -> probe/buffer bermasalah
  softM = m;
  softB = (sy - m * sx) / n;
  softOn = true;
}

static int jumlahTitikValid() {
  int n = 0;
  for (int i = 0; i < 3; i++)
    if (calPoints[i].valid)
      n++;
  return n;
}

// Trim offset manual (pH), dipanggil dari dashboard/serial. 0 = hapus.
void setOffsetPhManual(double nilai) {
  swOff = nilai;
  prefs.putDouble("sw_off", swOff);
}
double getOffsetPhManual() { return swOff; }

static bool writeCalibrationToSensor(uint16_t step, double phBuffer) {
  node.clearTransmitBuffer();
  node.setTransmitBuffer(0, step);
  node.setTransmitBuffer(1, (uint16_t)lround(phBuffer * 100.0));
  uint8_t result = node.writeMultipleRegisters(CAL_REGISTER_CMD, 2);
  if (result != node.ku8MBSuccess) {
    Serial.printf("Tulis kalibrasi GAGAL, kode 0x%02X\n", result);
    char buf[96];
    snprintf(buf, sizeof(buf),
             "Gagal menulis kalibrasi ke register sensor (kode Modbus 0x%02X, "
             "alamat 0x%04X).",
             result, (unsigned)CAL_REGISTER_CMD);
    phCalLastMsg = buf;
    return false;
  }
  return true;
}

static void savePoint(const char *prefix, const CalPoint &p) {
  String k(prefix);
  prefs.putBool((k + "v").c_str(), p.valid);
  prefs.putDouble((k + "r").c_str(), p.reading);
  prefs.putDouble((k + "m").c_str(), p.mv);
  prefs.putULong((k + "t").c_str(), p.when);
}

static void loadPoint(const char *prefix, CalPoint &p) {
  String k(prefix);
  p.valid = prefs.getBool((k + "v").c_str(), false);
  p.reading = prefs.getDouble((k + "r").c_str(), 0.0);
  p.mv = prefs.getDouble((k + "m").c_str(), NAN);
  p.when = prefs.getULong((k + "t").c_str(), 0);
}

static void clearVerification() {
  for (int i = 0; i < 3; i++) {
    verifPoints[i].valid = false;
    verifPoints[i].reading = 0;
  }
  const char *keys[3] = {"vf4", "vf7", "vf10"};
  for (int i = 0; i < 3; i++)
    savePoint(keys[i], verifPoints[i]);
  computeVerification();
}

// Hasil proses kalibrasi/verifikasi pH terakhir -- dibaca WebSync.cpp untuk
// dilaporkan ke dashboard (perintah dari web), selain dicetak ke Serial.
bool phCalLastOk = false;
long phCalIdPerintahWeb = -1;
String phCalLastMsg;

static void calibratePoint(int idx, uint16_t step, const char *label,
                           const char *prefsKey) {
  Serial.printf("\n=== KALIBRASI TITIK %u: %s ===\n", step, label);
  Serial.println(F("1. Bilas probe dengan aquades, keringkan lembut."));
  Serial.printf("2. Celupkan ke buffer %s, aduk pelan.\n", label);
  delay(3000);

  // step == 0 -> titik tengah (6.86): tidak ada langkah tulis ke sensor yang
  // dikenal, selalu lewat koreksi software.
  const char *titikDb = (idx == 0) ? "ph4" : (idx == 1) ? "ph7" : "ph10";
  double ph = 0, mv = NAN;
  bool wasStable = false;
  phCalLastOk = false;
  phCalLastMsg = "Gagal membaca sensor pH (Modbus tidak menjawab).";
  phBacaMentah = true; // titik kalibrasi harus pembacaan mentah sensor
  const bool bacaOk =
      collectStableReading(ph, mv, wasStable, label, titikDb, "kalibrasi");
  phBacaMentah = false;
  if (!bacaOk)
    return;
  if (!wasStable) {
    phCalLastMsg = String("Pembacaan ") + label +
                   " belum stabil -> kalibrasi TIDAK ditulis ke sensor.";
    Serial.println(
        F("Pembacaan belum stabil -> kalibrasi TIDAK ditulis ke sensor."));
    return;
  }

  phCalLastMsg = "Gagal menulis kalibrasi ke register sensor.";
  static const char *kunciSw[3] = {"sw_r1", "sw_r2", "sw_r3"};
  bool tulisKeSensorOk = false;
  String kode = "titik tengah, tidak ditulis ke sensor";
  if (step != 0) {
    tulisKeSensorOk = writeCalibrationToSensor(step, calPoints[idx].phValue);
    if (!tulisKeSensorOk)
      kode = phCalLastMsg; // berisi kode Modbus dari fungsi tulis
  }
  if (tulisKeSensorOk) {
    // Sensor menerima: sensor yang mengoreksi, koreksi software dimatikan.
    for (int i = 0; i < 3; i++) {
      swRaw[i] = NAN;
      prefs.remove(kunciSw[i]);
    }
    prefs.putBool("sw_on", false);
    hitungKoreksiSoft();
    phCalLastMsg = String(label) + " tertulis ke sensor (pembacaan " +
                   String(ph, 2) + " pH).";
  } else {
    // Sensor menolak / titik tengah: simpan titik mentah, pakai koreksi
    // linear di ESP32 dari titik yang sudah ada.
    swRaw[idx] = ph;
    prefs.putDouble(kunciSw[idx], ph);
    prefs.putBool("sw_on", true);
    hitungKoreksiSoft();
    phCalLastMsg = String(label) + " tersimpan sebagai koreksi software (" +
                   kode + ").";
    if (softOn)
      phCalLastMsg += String(" Koreksi aktif: pH = ") + String(softM, 4) +
                      " x pH_sensor " + (softB >= 0 ? "+ " : "- ") +
                      String(fabs(softB), 3);
    else
      phCalLastMsg += " Butuh titik lain (kemiringan belum wajar/titik "
                      "terlalu dekat) agar koreksi aktif.";
  }
  phCalLastOk = true;

  calPoints[idx].reading =
      ph; // pembacaan SEBELUM koefisien baru berlaku (dokumentasi)
  calPoints[idx].mv = mv;
  calPoints[idx].when = epochNow();
  calPoints[idx].valid = true;
  savePoint(prefsKey, calPoints[idx]);

  Serial.printf("\n%s\n", phCalLastMsg.c_str());

  calibrated = jumlahTitikValid() >= 2;
  if (calibrated) {
    lastCalibrated = epochNow();
    prefs.putULong("calts", lastCalibrated);
    clearVerification(); // verifikasi lama tidak berlaku lagi
    Serial.println(F("Minimal 2 titik selesai. Lanjut verifikasi: 'A' (4.01), "
                     "'7' (6.86), 'C' (9.18)."));
  }
}

static void verifyPoint(int idx, const char *label, const char *prefsKey) {
  Serial.printf("\n=== VERIFIKASI %s ===\n", label);
  Serial.printf("Celupkan probe (sudah dibilas) ke buffer %s.\n", label);
  delay(3000);

  const char *titikDb = (idx == 0) ? "ph4" : (idx == 1) ? "ph7" : "ph10";
  double ph = 0, mv = NAN;
  bool wasStable = false;
  phCalLastOk = false;
  phCalLastMsg = "Gagal membaca sensor pH (Modbus tidak menjawab).";
  if (!collectStableReading(ph, mv, wasStable, label, titikDb, "verifikasi"))
    return;

  verifPoints[idx].reading = ph;
  verifPoints[idx].mv = mv;
  verifPoints[idx].when = epochNow();
  verifPoints[idx].valid = true;
  savePoint(prefsKey, verifPoints[idx]);

  Serial.printf("\n%s: sensor %.3f vs referensi %.2f  (error %+.3f)\n", label,
                ph, verifPoints[idx].phValue, ph - verifPoints[idx].phValue);
  phCalLastOk = true;
  phCalLastMsg = String("Verifikasi ") + label + ": sensor " + String(ph, 3) +
                 " vs ref " + String(verifPoints[idx].phValue, 2) + " (error " +
                 String(ph - verifPoints[idx].phValue, 3) + ")";
  computeVerification();
  showHealthReport();
}

// ---------------------------------------------------------------------
// Perintah dipanggil dari loop()
// ---------------------------------------------------------------------
void calibratePH4() { calibratePoint(0, 1, "pH 4.01", "cal4"); }
void calibratePH10() { calibratePoint(2, 2, "pH 9.18", "cal10"); }
void verifyPH4() { verifyPoint(0, "pH 4.01", "vf4"); }
void verifyPH7() { verifyPoint(1, "pH 6.86", "vf7"); }
void verifyPH10() { verifyPoint(2, "pH 9.18", "vf10"); }
// Titik tengah 6.86: ikut koreksi software (tidak ada langkah tulis sensor).
void calibratePH7() { calibratePoint(1, 0, "pH 6.86", "cal7"); }

// Regresi least squares pH_ref = m * pH_sensor + b, R^2, RMSE, error maks
// (bagian 4.2).
void computeVerification() {
  double sx = 0, sy = 0, sxy = 0, sxx = 0, syy = 0;
  int n = 0;
  for (int i = 0; i < 3; i++) {
    if (!verifPoints[i].valid)
      continue;
    double x = verifPoints[i].reading, y = verifPoints[i].phValue;
    sx += x;
    sy += y;
    sxy += x * y;
    sxx += x * x;
    syy += y * y;
    n++;
  }
  verif = {n, 0, 0, -1, 0, 0, false};
  if (n < 2)
    return;

  double den = n * sxx - sx * sx;
  if (fabs(den) < 1e-12)
    return;
  verif.m = (n * sxy - sx * sy) / den;
  verif.b = (sy - verif.m * sx) / n;

  double denR2 = den * (n * syy - sy * sy);
  verif.r2 = (n >= 3 && denR2 > 0)
                 ? (n * sxy - sx * sy) * (n * sxy - sx * sy) / denR2
                 : -1;

  double se = 0;
  for (int i = 0; i < 3; i++) {
    if (!verifPoints[i].valid)
      continue;
    double x = verifPoints[i].reading, y = verifPoints[i].phValue;
    double e = y - (verif.m * x + verif.b);
    se += e * e;
    verif.maxErr = max(verif.maxErr, fabs(x - y));
  }
  verif.rmse = sqrt(se / n);

  verif.pass = (n >= 3) && fabs(verif.m - 1.0) <= VERIF_SLOPE_TOL &&
               fabs(verif.b) <= VERIF_OFFSET_TOL && verif.r2 >= VERIF_R2_MIN &&
               verif.maxErr <= VERIF_MAX_ERROR;
}

// Offset sistematis -> register deviasi 0x0050 (bagian 4.5). Ditulis SEKALI ke
// sensor.
void writeDeviation() {
  if (verif.n < 2) {
    Serial.println(
        F("Butuh minimal 2 titik verifikasi sebelum menulis deviasi."));
    return;
  }
  double sum = 0;
  for (int i = 0; i < 3; i++)
    if (verifPoints[i].valid)
      sum += verifPoints[i].phValue - verifPoints[i].reading;
  double delta = sum / verif.n;
  int16_t reg = (int16_t)lround(delta * 100.0);

  Serial.printf("Delta rata-rata = %+.3f pH -> register 0x%04X = %d (0x%04X)\n",
                delta, DEVIATION_REGISTER, reg, (uint16_t)reg);
  uint8_t result = node.writeSingleRegister(DEVIATION_REGISTER, (uint16_t)reg);
  if (result != node.ku8MBSuccess) {
    Serial.printf("Tulis deviasi GAGAL, kode 0x%02X\n", result);
    return;
  }
  Serial.println(
      F("Deviasi tertulis. Ulangi verifikasi (A/7/C) untuk memastikan."));
  clearVerification();
}

// Laporan untuk skripsi: efisiensi slope (bagian 3.3) + regresi verifikasi
// (4.3-4.4).
void showHealthReport() {
  Serial.println(F("\n================================"));
  Serial.println(F("   LAPORAN KALIBRASI & VERIFIKASI"));
  Serial.println(F("================================"));

  int lo = -1, hi = -1;
  for (int i = 0; i < 3; i++)
    if (calPoints[i].valid && !isnan(calPoints[i].mv)) {
      if (lo < 0)
        lo = i;
      hi = i;
    }
  if (lo >= 0 && hi > lo) {
    double S = (calPoints[lo].mv - calPoints[hi].mv) /
               (calPoints[hi].phValue - calPoints[lo].phValue);
    double E0 = calPoints[lo].mv + S * calPoints[lo].phValue;
    double tc = isnan(lastTempC) ? 25.0 : lastTempC;
    double eff = phSlopeEfficiencyPct(S, tc);
    Serial.printf(
        "E(%.2f)=%.1f mV  E(%.2f)=%.1f mV  (register 0x%04X, dugaan)\n",
        calPoints[lo].phValue, calPoints[lo].mv, calPoints[hi].phValue,
        calPoints[hi].mv, MV_REGISTER_ADDRESS);
    Serial.printf("Slope S        : %.2f mV/pH   (Nernst %.1f C = %.2f)\n", S,
                  tc, nernstSlopeMv(tc));
    Serial.printf("Zero point E0  : %.1f mV\n", E0);
    Serial.printf("Efisiensi      : %.1f %%  -> %s\n", eff,
                  eff < PH_SLOPE_MIN_PCT   ? "BURUK, elektroda perlu diganti"
                  : eff > PH_SLOPE_MAX_PCT ? "ANEH, cek buffer"
                                           : "SEHAT");
  } else {
    Serial.println(
        F("Efisiensi slope: belum ada 2 titik kalibrasi berbeda dengan data mV."));
  }

  if (softOn)
    Serial.printf("Koreksi software AKTIF: pH = %.4f x pH_sensor %+.3f\n", softM,
                  softB);
  else
    Serial.println(F("Koreksi software: tidak aktif."));

  Serial.println(F("\nVerifikasi (pH_ref = m * pH_sensor + b):"));
  const char *labels[3] = {"pH 4.01", "pH 6.86", "pH 9.18"};
  for (int i = 0; i < 3; i++) {
    Serial.printf("  %-8s : ", labels[i]);
    if (!verifPoints[i].valid) {
      Serial.println(F("BELUM"));
      continue;
    }
    double x = verifPoints[i].reading, y = verifPoints[i].phValue;
    double pred = verif.n >= 2 ? verif.m * x + verif.b : NAN;
    Serial.printf("sensor %.3f  prediksi %.3f  residual %+.3f  error %.3f\n", x,
                  pred, y - pred, fabs(x - y));
  }
  if (verif.n >= 2) {
    Serial.printf("  m=%.4f  b=%+.4f  R2=%s  RMSE=%.3f  err.maks=%.3f\n",
                  verif.m, verif.b,
                  verif.r2 < 0 ? "n/a" : String(verif.r2, 5).c_str(),
                  verif.rmse, verif.maxErr);
    Serial.printf(
        "  |m-1|<=%.2f  |b|<=%.2f  R2>=%.2f  err<=%.2f  -> %s\n",
        VERIF_SLOPE_TOL, VERIF_OFFSET_TOL, VERIF_R2_MIN, VERIF_MAX_ERROR,
        verif.pass ? "LULUS" : (verif.n < 3 ? "belum 3 titik" : "TIDAK LULUS"));
    if (verif.n >= 3 && !verif.pass && fabs(verif.m - 1.0) <= VERIF_SLOPE_TOL &&
        fabs(verif.b) > VERIF_OFFSET_TOL)
      Serial.println(F("  Offset sistematis: tulis ke register deviasi dengan "
                       "'O' (bukan koreksi di firmware)."));
    if (verif.n >= 3 && fabs(verif.m - 1.0) > VERIF_SLOPE_TOL)
      Serial.println(F(
          "  Slope menyimpang: ulangi kalibrasi 2 titik dengan buffer segar."));
  }
  Serial.println();
}

void resetCalibration() {
  prefs.clear();
  swRaw[0] = swRaw[1] = swRaw[2] = NAN;
  softOn = false;
  softM = 1.0;
  softB = 0.0;
  swOff = 0.0;
  for (int i = 0; i < 3; i++) {
    calPoints[i].valid = false;
    calPoints[i].reading = 0;
    calPoints[i].mv = NAN;
    calPoints[i].when = 0;
    verifPoints[i].valid = false;
    verifPoints[i].reading = 0;
    verifPoints[i].mv = NAN;
    verifPoints[i].when = 0;
  }
  calibrated = false;
  lastCalibrated = 0;
  computeVerification();
  Serial.println(F("\n=== CATATAN KALIBRASI DI ESP32 DIHAPUS ==="));
  Serial.println(F("Koefisien di dalam sensor TIDAK berubah (tidak ada "
                   "register reset yang diketahui)."));
}

void showStatus() {
  Serial.println(F("\n=== STATUS SENSOR ==="));
  Serial.printf("Titik 4.01    : %s\n",
                calPoints[0].valid ? String(calPoints[0].reading, 3).c_str()
                                   : "BELUM");
  Serial.printf("Titik 6.86    : %s\n",
                calPoints[1].valid ? String(calPoints[1].reading, 3).c_str()
                                   : "BELUM");
  Serial.printf("Titik 9.18    : %s\n",
                calPoints[2].valid ? String(calPoints[2].reading, 3).c_str()
                                   : "BELUM");
  Serial.printf("Kalibrasi     : %s (%d titik)\n",
                calibrated ? "SELESAI" : "BELUM", jumlahTitikValid());
  Serial.printf("Koreksi soft  : %s, offset manual %+.2f pH\n",
                softOn ? "AKTIF" : "tidak aktif", swOff);
  Serial.printf("Verifikasi    : %d/3 titik, %s\n", verif.n,
                verif.pass ? "LULUS" : "belum lulus");
  Serial.printf("Suhu terakhir : %.1f C\n", lastTempC);
  Serial.printf("Sensor        : %s\n",
                sensorOnline ? "ONLINE" : "TIDAK MERESPONS");
}

// Diagnostik: cetak holding register. Rentang rendah 0x0000..0x007F lalu
// 0x0100..0x013F (area kalibrasi), blok 8 register; blok yang ditolak sensor
// (alamat tidak ada) hanya dicetak satu baris. Dipakai mencari register mV.
void dumpModbusRegisters() {
  Serial.println(F("\n=== DUMP REGISTER MODBUS (holding) ==="));
  Serial.println(F("addr   raw(dec)  raw(hex)  signed   /10     /100"));
  const uint16_t awal[2] = {0x0000, 0x0100};
  const uint16_t akhir[2] = {0x0080, 0x0140};
  for (int rentang = 0; rentang < 2; rentang++) {
    for (uint16_t base = awal[rentang]; base < akhir[rentang]; base += 8) {
      uint8_t result = node.readHoldingRegisters(base, 8);
      if (result != node.ku8MBSuccess) {
        Serial.printf("0x%04X..0x%04X : gagal, kode 0x%02X\n", base, base + 7,
                      result);
        delay(30);
        continue;
      }
      for (uint8_t i = 0; i < 8; i++) {
        uint16_t r = node.getResponseBuffer(i);
        int16_t sg = (int16_t)r;
        Serial.printf("0x%04X  %6u    0x%04X   %6d  %7.1f  %7.2f\n", base + i,
                      r, r, sg, sg / 10.0, sg / 100.0);
      }
      delay(50);
    }
  }
  Serial.println();
}

void setupSensor() {
  pinMode(RS485_RE_DE_PIN, OUTPUT);
  digitalWrite(RS485_RE_DE_PIN, LOW); // default: mode terima
  Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  node.begin(SENSOR_SLAVE_ID, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  prefs.begin("ph-cal", false);
  loadPoint("cal4", calPoints[0]);
  loadPoint("cal7", calPoints[1]);
  loadPoint("cal10", calPoints[2]);
  loadPoint("vf4", verifPoints[0]);
  loadPoint("vf7", verifPoints[1]);
  loadPoint("vf10", verifPoints[2]);
  swRaw[0] = prefs.getDouble("sw_r1", NAN);
  swRaw[1] = prefs.getDouble("sw_r2", NAN);
  swRaw[2] = prefs.getDouble("sw_r3", NAN);
  swOff = prefs.getDouble("sw_off", 0.0);
  hitungKoreksiSoft();
  lastCalibrated = prefs.getULong("calts", 0);
  calibrated = jumlahTitikValid() >= 2;
  computeVerification();
}
