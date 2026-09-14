#include "Sensor.h"
#include "Config.h"
#include "Network.h"
#include "PhStability.h"
#include <ModbusMaster.h>
#include <time.h>

Preferences prefs;

CalPoint calPoints[3] = {
  { false, BUFFER_PH4_VALUE,  0.0, NAN, 0 },
  { false, BUFFER_PH7_VALUE,  0.0, NAN, 0 },
  { false, BUFFER_PH10_VALUE, 0.0, NAN, 0 }
};
CalPoint verifPoints[3] = {
  { false, BUFFER_PH4_VALUE,  0.0, NAN, 0 },
  { false, BUFFER_PH7_VALUE,  0.0, NAN, 0 },
  { false, BUFFER_PH10_VALUE, 0.0, NAN, 0 }
};
Verification  verif = { 0, 0, 0, -1, 0, 0, false };
bool          calibrated    = false;
unsigned long lastCalibrated = 0;
double        lastTempC     = NAN;
bool          sensorOnline  = false;

// ---------------------------------------------------------------------
// Parameter waktu kalibrasi. Ditaruh di sini supaya Config.h tetap ringkas.
// ---------------------------------------------------------------------
#ifndef CAL_MIN_MS
  #define CAL_MIN_MS   240000UL   // minimal 4 menit sebelum boleh selesai
#endif
#ifndef CAL_MAX_MS
  #define CAL_MAX_MS   600000UL   // pengaman: paksa berhenti di 10 menit
#endif
#ifndef CAL_SAMPLE_MS
  #define CAL_SAMPLE_MS  2000UL   // jeda antar sampel saat kalibrasi
#endif

static PhStability stbLive;   // jendela stabilitas operasi normal
static PhStability stbCal;    // jendela stabilitas saat kalibrasi/verifikasi

static unsigned long epochNow() {
  time_t t = time(nullptr);
  return (t >= 100000) ? (unsigned long)t : 0;
}

// =====================================================================
//  Modbus RS485 (MAX485 di Serial2). DE/RE HIGH hanya saat kirim.
// =====================================================================
static ModbusMaster node;

static void preTransmission()  { digitalWrite(RS485_RE_DE_PIN, HIGH); }
static void postTransmission() { digitalWrite(RS485_RE_DE_PIN, LOW);  }

// Tahap [1]-[3]: satu transaksi membaca blok register terendah..tertinggi
// (0x0001..0x0007), lalu penskalaan dan gerbang validitas.
bool readModbusSensor(double &ph, double &tempC, uint16_t *rawPh, int16_t *rawTemp, double *mv) {
  const uint16_t first = min((uint16_t)PH_REGISTER_ADDRESS,
                             min((uint16_t)TEMP_REGISTER_ADDRESS, (uint16_t)MV_REGISTER_ADDRESS));
  const uint16_t last  = max((uint16_t)PH_REGISTER_ADDRESS,
                             max((uint16_t)TEMP_REGISTER_ADDRESS, (uint16_t)MV_REGISTER_ADDRESS));

  uint8_t result = node.readHoldingRegisters(first, last - first + 1);
  if (result != node.ku8MBSuccess) {          // termasuk CRC salah / timeout
    sensorOnline = false;
    return false;
  }

  uint16_t rp = node.getResponseBuffer(PH_REGISTER_ADDRESS   - first);
  int16_t  rt = (int16_t)node.getResponseBuffer(TEMP_REGISTER_ADDRESS - first);  // cast WAJIB
  int16_t  rm = (int16_t)node.getResponseBuffer(MV_REGISTER_ADDRESS   - first);

  double p = rp / PH_SCALE;
  double t = rt / TEMP_SCALE;

  // Gerbang validitas (bagian 5.1)
  if (p < PH_VALID_MIN || p > PH_VALID_MAX || t < TEMP_VALID_MIN || t > TEMP_VALID_MAX) {
    sensorOnline = true;                      // bus hidup, datanya yang tidak masuk akal
    return false;
  }

  ph = p; tempC = t; lastTempC = t; sensorOnline = true;
  if (rawPh)   *rawPh   = rp;
  if (rawTemp) *rawTemp = rt;
  if (mv)      *mv      = rm / MV_SCALE;
  return true;
}

static double medianOf(double *v, int n) {
  for (int i = 1; i < n; i++) {               // insertion sort, n kecil
    double x = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
  return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static const char *classifyPh(double ph) {
  if (ph < BAKU_MUTU_PH_MIN) return "acidic";
  if (ph > BAKU_MUTU_PH_MAX) return "alkaline";
  return "normal";
}

// Tahap [4]-[6]: median N sampel, gerbang stabilitas, pembulatan.
PhReading readSensor() {
  PhReading r = { false, NAN, NAN, 0, 0, 0, 0, NAN, NAN, "error", "unknown", false };
  double phs[MEDIAN_SAMPLES], temps[MEDIAN_SAMPLES];

  for (int i = 0; i < MEDIAN_SAMPLES; i++) {
    double p, t; uint16_t rp; int16_t rt;
    if (readModbusSensor(p, t, &rp, &rt)) {
      phs[r.sampleCount] = p; temps[r.sampleCount] = t; r.sampleCount++;
      r.rawPh = rp; r.rawTemp = rt;
    } else {
      r.modbusErrors++;
    }
    if (i < MEDIAN_SAMPLES - 1) delay(SAMPLE_INTERVAL_MS);
  }
  if (r.sampleCount == 0) return r;           // quality = error, tanpa angka pH

  double ph = medianOf(phs, r.sampleCount);
  double tc = medianOf(temps, r.sampleCount);

  stbLive.add(ph);
  StabilityStats s = stbLive.evaluate();
  r.sd = s.valid ? s.sd : NAN;
  r.drift = s.valid ? s.drift : NAN;

  if (!s.valid)          r.quality = "fair";       // jendela belum penuh (~30 s pertama)
  else if (s.stable)     r.quality = "good";
  else if (s.acceptable) r.quality = "fair";
  else                   r.quality = "unstable";

  r.valid     = true;
  r.ph        = round(ph * 100.0) / 100.0;    // resolusi 0,01
  r.tempC     = round(tc * 10.0) / 10.0;      // resolusi 0,1
  r.phStatus  = classifyPh(r.ph);
  bool usable = (strcmp(r.quality, "good") == 0) || (strcmp(r.quality, "fair") == 0);
  r.compliant = usable && (r.ph >= BAKU_MUTU_PH_MIN) && (r.ph <= BAKU_MUTU_PH_MAX);
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
      if (c == 'X' || c == 'x') return true;
    }
    delay(50);
  }
  return false;
}

// Gerbang stabilitas sebelum kalibrasi/verifikasi (bagian 5.2).
// Hasil = rata-rata jendela stabil (pH) dan rata-rata mV sesudah warm-up.
static bool collectStableReading(double &resultPh, double &resultMv, bool &wasStable, const char *label) {
  unsigned long startTime = millis();
  int totalSamples = 0, mvCount = 0;
  double mvSum = 0;

  stbCal.reset();

  Serial.println();
  Serial.println(F("Menunggu pembacaan stabil..."));
  Serial.printf("Warm-up %lu detik dibuang.\n", CAL_WARMUP_MS / 1000UL);
  Serial.printf("Selesai otomatis bila sd <= %.2f pH DAN drift <= %.2f pH/menit.\n",
                STB_SD_GOOD_MV, STB_DRIFT_GOOD_MV);
  Serial.printf("Minimal %lu menit, maksimal %lu menit.\n", CAL_MIN_MS / 60000UL, CAL_MAX_MS / 60000UL);
  Serial.println(F("'X' di serial atau 'cancel' dari web untuk batal."));
  Serial.println();

  unsigned long lastPush = 0;

  while (true) {
    double ph, tc, mv;
    bool ok = readModbusSensor(ph, tc, nullptr, nullptr, &mv);
    totalSamples++;

    unsigned long elapsed = millis() - startTime;
    bool inWarmup = (elapsed < CAL_WARMUP_MS);

    if (ok && !inWarmup) { stbCal.add(ph); mvSum += mv; mvCount++; }

    StabilityStats s = stbCal.evaluate();

    if (!ok) Serial.printf("[%lus] #%d  pH=GAGAL", elapsed / 1000UL, totalSamples);
    else     Serial.printf("[%lus] #%d  pH=%.2f  T=%.1f C  mV=%.1f", elapsed / 1000UL, totalSamples, ph, tc, mv);
    if (inWarmup) Serial.println(F("   [WARM-UP, dibuang]"));
    else        { Serial.println(); stbCal.printStatus(s); }

    bool minTimePassed = (elapsed >= CAL_MIN_MS);
    bool timeout       = (elapsed >= CAL_MAX_MS);

    if ((minTimePassed && s.valid && s.stable) || timeout) {
      const char *finalMsg;
      Serial.println(F("\n--------------------------------"));
      if (!s.valid) {
        Serial.println(F("GAGAL: sampel valid terlalu sedikit. Cek kabel A/B / catu daya."));
        wasStable = false; finalMsg = "sample_kurang";
      } else if (s.stable) {
        Serial.println(F("STATUS: STABIL."));
        wasStable = true;  finalMsg = "stabil";
      } else if (s.acceptable) {
        Serial.println(F("STATUS: cukup stabil (belum ideal, masih bisa dipakai)."));
        wasStable = true;  finalMsg = "cukup_stabil";
      } else {
        Serial.println(F("STATUS: BELUM STABIL sampai batas waktu."));
        Serial.println(F("  1. Probe kurang terhidrasi -> rendam KCl 3M 8-12 jam"));
        Serial.println(F("  2. Junction referensi tersumbat -> bersihkan"));
        Serial.println(F("  3. Buffer terkontaminasi -> pakai yang baru"));
        wasStable = false; finalMsg = "belum_stabil";
      }
      resultPh = s.mean;
      resultMv = mvCount ? mvSum / mvCount : NAN;
      Serial.printf("Hasil      = %.3f pH\n", s.mean);
      Serial.printf("sd / drift = %.3f pH / %.3f pH per menit\n", s.sd, s.drift);
      Serial.println(F("--------------------------------"));
      firebasePushCalStatus(false, label, elapsed / 1000UL, s.n, s.mean, s.sd, finalMsg);
      return s.valid;
    }

    // Jaringan dipisah dari sampling, tiap ~20 detik.
    if (millis() - lastPush >= 20000UL) {
      lastPush = millis();
      firebasePushCalStatus(true, label, elapsed / 1000UL, s.n, s.mean, s.sd,
                            inWarmup ? "warmup" : (s.stable ? "stabil" : "menunggu"));
      if (firebaseGetCommand() == "cancel") {
        firebaseClearCommand();
        Serial.println(F("\n>> DIBATALKAN (dari WEB)\n"));
        firebasePushCalStatus(false, label, elapsed / 1000UL, s.n, s.mean, s.sd, "dibatalkan");
        return false;
      }
    }

    if (waitWithAbortCheck(CAL_SAMPLE_MS)) {
      Serial.println(F("\n>> DIBATALKAN USER. Tidak ada yang ditulis.\n"));
      firebasePushCalStatus(false, label, elapsed / 1000UL, s.n, s.mean, s.sd, "dibatalkan");
      return false;
    }
  }
}

// Tulis titik kalibrasi ke sensor: reg 0x0120 = langkah, 0x0121 = round(pH x 100).
static bool writeCalibrationToSensor(uint16_t step, double phBuffer) {
  node.clearTransmitBuffer();
  node.setTransmitBuffer(0, step);
  node.setTransmitBuffer(1, (uint16_t)lround(phBuffer * 100.0));
  uint8_t result = node.writeMultipleRegisters(CAL_REGISTER_CMD, 2);
  if (result != node.ku8MBSuccess) {
    Serial.printf("Tulis kalibrasi GAGAL, kode 0x%02X\n", result);
    return false;
  }
  return true;
}

static void savePoint(const char *prefix, const CalPoint &p) {
  String k(prefix);
  prefs.putBool  ((k + "v").c_str(), p.valid);
  prefs.putDouble((k + "r").c_str(), p.reading);
  prefs.putDouble((k + "m").c_str(), p.mv);
  prefs.putULong ((k + "t").c_str(), p.when);
}

static void loadPoint(const char *prefix, CalPoint &p) {
  String k(prefix);
  p.valid   = prefs.getBool  ((k + "v").c_str(), false);
  p.reading = prefs.getDouble((k + "r").c_str(), 0.0);
  p.mv      = prefs.getDouble((k + "m").c_str(), NAN);
  p.when    = prefs.getULong ((k + "t").c_str(), 0);
}

static void clearVerification() {
  for (int i = 0; i < 3; i++) { verifPoints[i].valid = false; verifPoints[i].reading = 0; }
  const char *keys[3] = { "vf4", "vf7", "vf10" };
  for (int i = 0; i < 3; i++) savePoint(keys[i], verifPoints[i]);
  computeVerification();
}

static void calibratePoint(int idx, uint16_t step, const char *label, const char *prefsKey) {
  Serial.printf("\n=== KALIBRASI TITIK %u: %s ===\n", step, label);
  Serial.println(F("1. Bilas probe dengan aquades, keringkan lembut."));
  Serial.printf ("2. Celupkan ke buffer %s, aduk pelan.\n", label);
  delay(3000);

  double ph = 0, mv = NAN; bool wasStable = false;
  if (!collectStableReading(ph, mv, wasStable, label)) return;
  if (!wasStable) {
    Serial.println(F("Pembacaan belum stabil -> kalibrasi TIDAK ditulis ke sensor."));
    return;
  }

  if (!writeCalibrationToSensor(step, calPoints[idx].phValue)) return;

  calPoints[idx].reading = ph;          // pembacaan SEBELUM koefisien baru berlaku (dokumentasi)
  calPoints[idx].mv      = mv;
  calPoints[idx].when    = epochNow();
  calPoints[idx].valid   = true;
  savePoint(prefsKey, calPoints[idx]);

  Serial.printf("\n%s TERTULIS ke sensor (0x%04X=%u, 0x%04X=%ld). Pembacaan saat itu %.2f pH.\n",
                label, CAL_REGISTER_CMD, step, CAL_REGISTER_VALUE,
                lround(calPoints[idx].phValue * 100.0), ph);

  calibrated = calPoints[0].valid && calPoints[2].valid;
  if (calibrated) {
    lastCalibrated = epochNow();
    prefs.putULong("calts", lastCalibrated);
    clearVerification();                // verifikasi lama tidak berlaku lagi
    Serial.println(F("Kedua titik selesai. Lanjut verifikasi: 'A' (4.01), '7' (7.01), 'C' (9.01)."));
  }
  firebasePushCalPoints();
}

static void verifyPoint(int idx, const char *label, const char *prefsKey) {
  Serial.printf("\n=== VERIFIKASI %s ===\n", label);
  Serial.printf("Celupkan probe (sudah dibilas) ke buffer %s.\n", label);
  delay(3000);

  double ph = 0, mv = NAN; bool wasStable = false;
  if (!collectStableReading(ph, mv, wasStable, label)) return;

  verifPoints[idx].reading = ph;
  verifPoints[idx].mv      = mv;
  verifPoints[idx].when    = epochNow();
  verifPoints[idx].valid   = true;
  savePoint(prefsKey, verifPoints[idx]);

  Serial.printf("\n%s: sensor %.3f vs referensi %.2f  (error %+.3f)\n",
                label, ph, verifPoints[idx].phValue, ph - verifPoints[idx].phValue);
  computeVerification();
  showHealthReport();
  firebasePushCalPoints();
}

void calibratePH4()  { calibratePoint(0, 1, "pH 4.01", "cal4"); }
void calibratePH10() { calibratePoint(2, 2, "pH 9.01", "cal10"); }
void verifyPH4()     { verifyPoint(0, "pH 4.01", "vf4"); }
void verifyPH7()     { verifyPoint(1, "pH 7.01", "vf7"); }
void verifyPH10()    { verifyPoint(2, "pH 9.01", "vf10"); }
void calibratePH7()  { verifyPH7(); }   // 7.01 hanya untuk verifikasi

// Regresi least squares pH_ref = m * pH_sensor + b, R^2, RMSE, error maks (bagian 4.2).
void computeVerification() {
  double sx = 0, sy = 0, sxy = 0, sxx = 0, syy = 0;
  int n = 0;
  for (int i = 0; i < 3; i++) {
    if (!verifPoints[i].valid) continue;
    double x = verifPoints[i].reading, y = verifPoints[i].phValue;
    sx += x; sy += y; sxy += x * y; sxx += x * x; syy += y * y; n++;
  }
  verif = { n, 0, 0, -1, 0, 0, false };
  if (n < 2) return;

  double den = n * sxx - sx * sx;
  if (fabs(den) < 1e-12) return;
  verif.m = (n * sxy - sx * sy) / den;
  verif.b = (sy - verif.m * sx) / n;

  double denR2 = den * (n * syy - sy * sy);
  verif.r2 = (n >= 3 && denR2 > 0) ? (n * sxy - sx * sy) * (n * sxy - sx * sy) / denR2 : -1;

  double se = 0;
  for (int i = 0; i < 3; i++) {
    if (!verifPoints[i].valid) continue;
    double x = verifPoints[i].reading, y = verifPoints[i].phValue;
    double e = y - (verif.m * x + verif.b);
    se += e * e;
    verif.maxErr = max(verif.maxErr, fabs(x - y));
  }
  verif.rmse = sqrt(se / n);

  verif.pass = (n >= 3)
            && fabs(verif.m - 1.0) <= VERIF_SLOPE_TOL
            && fabs(verif.b)       <= VERIF_OFFSET_TOL
            && verif.r2            >= VERIF_R2_MIN
            && verif.maxErr        <= VERIF_MAX_ERROR;
}

// Offset sistematis -> register deviasi 0x0050 (bagian 4.5). Ditulis SEKALI ke sensor.
void writeDeviation() {
  if (verif.n < 2) {
    Serial.println(F("Butuh minimal 2 titik verifikasi sebelum menulis deviasi."));
    return;
  }
  double sum = 0;
  for (int i = 0; i < 3; i++)
    if (verifPoints[i].valid) sum += verifPoints[i].phValue - verifPoints[i].reading;
  double delta = sum / verif.n;
  int16_t reg  = (int16_t)lround(delta * 100.0);

  Serial.printf("Delta rata-rata = %+.3f pH -> register 0x%04X = %d (0x%04X)\n",
                delta, DEVIATION_REGISTER, reg, (uint16_t)reg);
  uint8_t result = node.writeSingleRegister(DEVIATION_REGISTER, (uint16_t)reg);
  if (result != node.ku8MBSuccess) {
    Serial.printf("Tulis deviasi GAGAL, kode 0x%02X\n", result);
    return;
  }
  Serial.println(F("Deviasi tertulis. Ulangi verifikasi (A/7/C) untuk memastikan."));
  clearVerification();
  firebasePushCalPoints();
}

// Laporan untuk skripsi: efisiensi slope (bagian 3.3) + regresi verifikasi (4.3-4.4).
void showHealthReport() {
  Serial.println(F("\n================================"));
  Serial.println(F("   LAPORAN KALIBRASI & VERIFIKASI"));
  Serial.println(F("================================"));

  if (calPoints[0].valid && calPoints[2].valid &&
      !isnan(calPoints[0].mv) && !isnan(calPoints[2].mv)) {
    double S  = (calPoints[0].mv - calPoints[2].mv) / (calPoints[2].phValue - calPoints[0].phValue);
    double E0 = calPoints[0].mv + S * calPoints[0].phValue;
    double tc = isnan(lastTempC) ? 25.0 : lastTempC;
    double eff = phSlopeEfficiencyPct(S, tc);
    Serial.printf("E(4.01)=%.1f mV  E(9.18)=%.1f mV  (register 0x%04X, dugaan)\n",
                  calPoints[0].mv, calPoints[2].mv, MV_REGISTER_ADDRESS);
    Serial.printf("Slope S        : %.2f mV/pH   (Nernst %.1f C = %.2f)\n", S, tc, nernstSlopeMv(tc));
    Serial.printf("Zero point E0  : %.1f mV\n", E0);
    Serial.printf("Efisiensi      : %.1f %%  -> %s\n", eff,
                  eff < PH_SLOPE_MIN_PCT ? "BURUK, elektroda perlu diganti" :
                  eff > PH_SLOPE_MAX_PCT ? "ANEH, cek buffer" : "SEHAT");
  } else {
    Serial.println(F("Efisiensi slope: belum ada 2 titik kalibrasi dengan data mV."));
  }

  Serial.println(F("\nVerifikasi (pH_ref = m * pH_sensor + b):"));
  const char *labels[3] = { "pH 4.01", "pH 7.01", "pH 9.01" };
  for (int i = 0; i < 3; i++) {
    Serial.printf("  %-8s : ", labels[i]);
    if (!verifPoints[i].valid) { Serial.println(F("BELUM")); continue; }
    double x = verifPoints[i].reading, y = verifPoints[i].phValue;
    double pred = verif.n >= 2 ? verif.m * x + verif.b : NAN;
    Serial.printf("sensor %.3f  prediksi %.3f  residual %+.3f  error %.3f\n",
                  x, pred, y - pred, fabs(x - y));
  }
  if (verif.n >= 2) {
    Serial.printf("  m=%.4f  b=%+.4f  R2=%s  RMSE=%.3f  err.maks=%.3f\n",
                  verif.m, verif.b, verif.r2 < 0 ? "n/a" : String(verif.r2, 5).c_str(),
                  verif.rmse, verif.maxErr);
    Serial.printf("  |m-1|<=%.2f  |b|<=%.2f  R2>=%.2f  err<=%.2f  -> %s\n",
                  VERIF_SLOPE_TOL, VERIF_OFFSET_TOL, VERIF_R2_MIN, VERIF_MAX_ERROR,
                  verif.pass ? "LULUS" : (verif.n < 3 ? "belum 3 titik" : "TIDAK LULUS"));
    if (verif.n >= 3 && !verif.pass && fabs(verif.m - 1.0) <= VERIF_SLOPE_TOL && fabs(verif.b) > VERIF_OFFSET_TOL)
      Serial.println(F("  Offset sistematis: tulis ke register deviasi dengan 'O' (bukan koreksi di firmware)."));
    if (verif.n >= 3 && fabs(verif.m - 1.0) > VERIF_SLOPE_TOL)
      Serial.println(F("  Slope menyimpang: ulangi kalibrasi 2 titik dengan buffer segar."));
  }
  Serial.println();
}

void resetCalibration() {
  prefs.clear();
  for (int i = 0; i < 3; i++) {
    calPoints[i].valid = false;  calPoints[i].reading = 0;  calPoints[i].mv = NAN;  calPoints[i].when = 0;
    verifPoints[i].valid = false; verifPoints[i].reading = 0; verifPoints[i].mv = NAN; verifPoints[i].when = 0;
  }
  calibrated = false; lastCalibrated = 0;
  computeVerification();
  firebasePushCalPoints();
  Serial.println(F("\n=== CATATAN KALIBRASI DI ESP32 DIHAPUS ==="));
  Serial.println(F("Koefisien di dalam sensor TIDAK berubah (tidak ada register reset yang diketahui)."));
}

void showStatus() {
  Serial.println(F("\n=== STATUS SENSOR ==="));
  Serial.printf("Titik 1 (4.01): %s\n", calPoints[0].valid ? String(calPoints[0].reading, 3).c_str() : "BELUM");
  Serial.printf("Titik 2 (9.18): %s\n", calPoints[2].valid ? String(calPoints[2].reading, 3).c_str() : "BELUM");
  Serial.printf("Kalibrasi     : %s\n", calibrated ? "DITULIS ke sensor" : "BELUM");
  Serial.printf("Verifikasi    : %d/3 titik, %s\n", verif.n, verif.pass ? "LULUS" : "belum lulus");
  Serial.printf("Suhu terakhir : %.1f C\n", lastTempC);
  Serial.printf("Sensor        : %s\n", sensorOnline ? "ONLINE" : "TIDAK MERESPONS");
  Serial.printf("WiFi          : %s\n", wifiReady ? "TERHUBUNG" : "OFFLINE");
}

// Diagnostik: cetak holding register 0x0000..0x000F.
void dumpModbusRegisters() {
  Serial.println(F("\n=== DUMP REGISTER MODBUS (holding) ==="));
  Serial.println(F("addr   raw(dec)  raw(hex)  signed   /10     /100"));
  for (uint16_t base = 0x0000; base < 0x0010; base += 8) {
    uint8_t result = node.readHoldingRegisters(base, 8);
    if (result != node.ku8MBSuccess) {
      Serial.printf("0x%04X..0x%04X : gagal, kode 0x%02X\n", base, base + 7, result);
      continue;
    }
    for (uint8_t i = 0; i < 8; i++) {
      uint16_t r = node.getResponseBuffer(i);
      int16_t  s = (int16_t)r;
      Serial.printf("0x%04X  %6u    0x%04X   %6d  %7.1f  %7.2f\n", base + i, r, r, s, s / 10.0, s / 100.0);
    }
    delay(50);
  }
  Serial.println();
}

void setupSensor() {
  pinMode(RS485_RE_DE_PIN, OUTPUT);
  digitalWrite(RS485_RE_DE_PIN, LOW);                       // default: mode terima
  Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  node.begin(SENSOR_SLAVE_ID, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  prefs.begin("ph-cal", false);
  loadPoint("cal4",  calPoints[0]);
  loadPoint("cal10", calPoints[2]);
  loadPoint("vf4",   verifPoints[0]);
  loadPoint("vf7",   verifPoints[1]);
  loadPoint("vf10",  verifPoints[2]);
  lastCalibrated = prefs.getULong("calts", 0);
  calibrated = calPoints[0].valid && calPoints[2].valid;
  computeVerification();
}
