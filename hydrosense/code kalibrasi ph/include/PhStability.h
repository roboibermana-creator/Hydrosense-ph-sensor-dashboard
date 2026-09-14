#pragma once
// =====================================================================
//  PhStability.h  -  Deteksi stabilitas & diagnostik kalibrasi pH
//  Drop-in untuk proyek DFRobot Gravity pH V2 + ESP32
//
//  Menggantikan metrik "range = max - min seluruh sesi" yang monoton
//  naik dan tidak adil, dengan dua metrik pada JENDELA BERGERAK:
//    1. Standar deviasi  -> derau (noise)
//    2. Drift regresi    -> apakah pembacaan masih merayap (mV/menit)
//
//  Cara pakai singkat:
//    PhStability stb;
//    stb.reset();
//    loop kalibrasi:
//        double v = readVoltage();
//        stb.add(v);
//        StabilityStats s = stb.evaluate();
//        if (s.valid && s.stable) { ambil s.mean sebagai hasil; break; }
// =====================================================================

#include <Arduino.h>
#include <math.h>
#include "Config.h"   // ambang stabilitas (satuan pH) & PH_BOARD_GAIN dari sini

// ---------------------------------------------------------------------
// Parameter yang bisa disetel
// ---------------------------------------------------------------------
#define STB_MAX_SAMPLES      240      // ring buffer (240 x 2s = 8 menit)
#define STB_WINDOW_MS        120000UL // jendela evaluasi: 2 menit terakhir
#define STB_MIN_SAMPLES      15       // minimal sampel dalam jendela

#ifndef STB_UNIT
#define STB_UNIT "mV"
#endif
#ifndef STB_SD_GOOD_MV
#define STB_SD_GOOD_MV       2.0      // sd <= 2.0 mV  -> bagus
#endif
#ifndef STB_SD_OK_MV
#define STB_SD_OK_MV         5.0      // sd <= 5.0 mV  -> masih diterima
#endif
#ifndef STB_DRIFT_GOOD_MV
#define STB_DRIFT_GOOD_MV    0.5      // |drift| <= 0.5 mV/menit -> bagus
#endif
#ifndef STB_DRIFT_OK_MV
#define STB_DRIFT_OK_MV      1.5      // |drift| <= 1.5 mV/menit -> masih diterima
#endif

// Gain papan pengkondisi sinyal DFRobot V2 (sinyal elektroda dikuatkan ~3x)
#ifndef PH_BOARD_GAIN
#define PH_BOARD_GAIN        3.0
#endif

// Batas kesehatan elektroda (rujukan USGS TWRI 9-A6.4)
#define PH_SLOPE_MIN_PCT     95.0
#define PH_SLOPE_MAX_PCT     102.0


// =====================================================================
//  Hasil evaluasi stabilitas
// =====================================================================
struct StabilityStats {
  bool   valid;        // apakah sampel dalam jendela sudah cukup
  int    n;            // jumlah sampel dalam jendela
  double mean;         // rata-rata jendela (mV)  <-- INI yang dipakai sbg hasil
  double sd;           // standar deviasi (mV)
  double drift;        // mV per menit (positif = naik)
  double range;        // max-min DALAM JENDELA saja (bukan seluruh sesi)
  bool   stable;       // lolos kriteria "bagus"
  bool   acceptable;   // lolos kriteria "masih diterima"
};


// =====================================================================
//  Ring buffer + evaluator
// =====================================================================
class PhStability {
  struct Sample { unsigned long t; double v; };

  Sample _buf[STB_MAX_SAMPLES];
  int    _count = 0;
  int    _head  = 0;

public:
  void reset() {
    _count = 0;
    _head  = 0;
  }

  void add(double mv) {
    _buf[_head].t = millis();
    _buf[_head].v = mv;
    _head = (_head + 1) % STB_MAX_SAMPLES;
    if (_count < STB_MAX_SAMPLES) _count++;
  }

  int size() const { return _count; }

  // -------------------------------------------------------------------
  // Evaluasi jendela STB_WINDOW_MS terakhir.
  // Drift dihitung lewat regresi least-squares tegangan terhadap waktu.
  // -------------------------------------------------------------------
  StabilityStats evaluate(unsigned long windowMs = STB_WINDOW_MS) const {
    StabilityStats s = { false, 0, 0, 0, 0, 0, false, false };

    if (_count == 0) return s;

    unsigned long now    = millis();
    unsigned long cutoff = (now > windowMs) ? (now - windowMs) : 0;

    // --- kumpulkan sampel di dalam jendela --------------------------
    double sumT = 0, sumV = 0, sumTV = 0, sumTT = 0;
    double minV =  1e12, maxV = -1e12;
    int    n = 0;

    for (int i = 0; i < _count; i++) {
      int idx = (_head - 1 - i + 2 * STB_MAX_SAMPLES) % STB_MAX_SAMPLES;
      if (_buf[idx].t < cutoff) break;      // buffer terurut, boleh berhenti

      double tMin = (double)_buf[idx].t / 60000.0;   // waktu dalam MENIT
      double v    = _buf[idx].v;

      sumT  += tMin;
      sumV  += v;
      sumTV += tMin * v;
      sumTT += tMin * tMin;

      if (v < minV) minV = v;
      if (v > maxV) maxV = v;
      n++;
    }

    s.n = n;
    if (n < STB_MIN_SAMPLES) return s;      // valid tetap false

    // --- rata-rata & standar deviasi --------------------------------
    double mean = sumV / n;

    double sumSq = 0;
    int k = 0;
    for (int i = 0; i < _count && k < n; i++) {
      int idx = (_head - 1 - i + 2 * STB_MAX_SAMPLES) % STB_MAX_SAMPLES;
      if (_buf[idx].t < cutoff) break;
      double d = _buf[idx].v - mean;
      sumSq += d * d;
      k++;
    }
    double sd = (n > 1) ? sqrt(sumSq / (n - 1)) : 0.0;

    // --- drift: slope regresi V terhadap waktu (mV/menit) -----------
    double denom = (n * sumTT) - (sumT * sumT);
    double drift = (fabs(denom) > 1e-12)
                 ? ((n * sumTV) - (sumT * sumV)) / denom
                 : 0.0;

    s.valid = true;
    s.mean  = mean;
    s.sd    = sd;
    s.drift = drift;
    s.range = maxV - minV;

    s.stable     = (sd <= STB_SD_GOOD_MV) && (fabs(drift) <= STB_DRIFT_GOOD_MV);
    s.acceptable = (sd <= STB_SD_OK_MV)   && (fabs(drift) <= STB_DRIFT_OK_MV);

    return s;
  }

  // -------------------------------------------------------------------
  // Cetak ringkasan ke Serial, lengkap dengan terjemahan ke satuan pH.
  // mvPerPH: |slope| kalibrasi saat ini (mV/pH). Beri 0 kalau belum ada.
  // -------------------------------------------------------------------
  void printStatus(const StabilityStats &s, double mvPerPH = 0.0) const {
    if (!s.valid) {
      Serial.print(F("  [menunggu data... "));
      Serial.print(s.n);
      Serial.print(F("/"));
      Serial.print(STB_MIN_SAMPLES);
      Serial.println(F(" sampel]"));
      return;
    }

    Serial.print(F("  n="));      Serial.print(s.n);
    Serial.print(F("  mean="));   Serial.print(s.mean, 3);
    Serial.print(F(" " STB_UNIT "  sd="));  Serial.print(s.sd, 3);
    Serial.print(F(" " STB_UNIT "  drift=")); Serial.print(s.drift, 3);
    Serial.print(F(" " STB_UNIT "/mnt"));

    if (mvPerPH > 1.0) {
      Serial.print(F("  (sd="));
      Serial.print(s.sd / mvPerPH, 3);
      Serial.print(F(" pH)"));
    }

    if      (s.stable)     Serial.println(F("  -> STABIL"));
    else if (s.acceptable) Serial.println(F("  -> cukup stabil"));
    else                   Serial.println(F("  -> belum stabil"));
  }
};


// =====================================================================
//  DIAGNOSTIK KESEHATAN ELEKTRODA
// =====================================================================

// Slope Nernst teoritis pada suhu tertentu (mV/pH).
// 25 C -> 59.16 ; 30 C -> 60.15
inline double nernstSlopeMv(double tempC) {
  return 0.19841 * (tempC + 273.15);
}

// Efisiensi slope elektroda dalam persen.
// slopeBoardMv = |calSlope| hasil regresi Anda, satuan mV/pH di sisi PAPAN.
inline double phSlopeEfficiencyPct(double slopeBoardMv, double tempC = 25.0) {
  double electrodeSlope = fabs(slopeBoardMv) / PH_BOARD_GAIN;
  return (electrodeSlope / nernstSlopeMv(tempC)) * 100.0;
}

// R^2 untuk fit V = m*pH + c. Perlu >= 3 titik agar bermakna.
// Kembalikan -1 kalau titik < 3.
inline double phCalibrationR2(const double *pH, const double *mv, int n) {
  if (n < 3) return -1.0;

  double sumX = 0, sumY = 0, sumXY = 0, sumXX = 0, sumYY = 0;
  for (int i = 0; i < n; i++) {
    sumX  += pH[i];
    sumY  += mv[i];
    sumXY += pH[i] * mv[i];
    sumXX += pH[i] * pH[i];
    sumYY += mv[i] * mv[i];
  }

  double num = (n * sumXY) - (sumX * sumY);
  double den = ((n * sumXX) - (sumX * sumX)) * ((n * sumYY) - (sumY * sumY));
  if (den <= 0) return -1.0;

  return (num * num) / den;
}

// Prediksi tegangan pada suatu nilai pH, dari persamaan V = m*pH + c.
// Berguna untuk menguji titik ke-3 SEBELUM mencelupkan probe:
//   double target = phPredictVoltage(9.18, calSlope, calIntercept);
inline double phPredictVoltage(double phValue, double slope, double intercept) {
  return slope * phValue + intercept;
}

// Cetak laporan kesehatan lengkap.
inline void phPrintHealthReport(double slope, double intercept,
                                const double *pH, const double *mv, int n,
                                double tempC = 25.0) {
  Serial.println();
  Serial.println(F("================================"));
  Serial.println(F("   LAPORAN KESEHATAN ELEKTRODA"));
  Serial.println(F("================================"));

  Serial.print(F("Suhu acuan     : "));
  Serial.print(tempC, 1);
  Serial.println(F(" C"));

  Serial.print(F("Nernst teoritis: "));
  Serial.print(nernstSlopeMv(tempC), 2);
  Serial.println(F(" mV/pH"));

  Serial.print(F("Slope papan    : "));
  Serial.print(fabs(slope), 2);
  Serial.println(F(" mV/pH"));

  double elec = fabs(slope) / PH_BOARD_GAIN;
  Serial.print(F("Slope elektroda: "));
  Serial.print(elec, 2);
  Serial.println(F(" mV/pH"));

  double eff = phSlopeEfficiencyPct(slope, tempC);
  Serial.print(F("Efisiensi      : "));
  Serial.print(eff, 1);
  Serial.print(F(" %  -> "));

  if (eff < PH_SLOPE_MIN_PCT) {
    Serial.println(F("BURUK, elektroda perlu dibersihkan/diganti"));
  } else if (eff > PH_SLOPE_MAX_PCT) {
    Serial.println(F("ANEH, cek buffer / skala ADC"));
  } else {
    Serial.println(F("SEHAT"));
  }

  // Zero point: pH saat tegangan = tegangan pH7 nominal.
  // Di sini kita laporkan pH terhitung pada titik tengah rentang kalibrasi.
  if (fabs(slope) > 1e-6) {
    double phAt1500 = (1500.0 - intercept) / slope;
    Serial.print(F("pH @1500 mV    : "));
    Serial.print(phAt1500, 2);
    Serial.println(F("  (informatif, dipengaruhi offset ADC)"));
  }

  double r2 = phCalibrationR2(pH, mv, n);
  if (r2 >= 0) {
    Serial.print(F("R^2 ("));
    Serial.print(n);
    Serial.print(F(" titik) : "));
    Serial.print(r2, 5);
    Serial.println(r2 >= 0.999 ? F("  -> sangat baik")
                 : (r2 >= 0.99 ? F("  -> baik")
                               : F("  -> linearitas buruk")));
  } else {
    Serial.println(F("R^2            : perlu 3 titik buffer"));
  }

  Serial.println(F("--------------------------------"));
  Serial.println(F("Prediksi tegangan (untuk verifikasi):"));
  const double checkPh[3] = { 4.01, 7.01, 9.01 };
  for (int i = 0; i < 3; i++) {
    Serial.print(F("  pH "));
    Serial.print(checkPh[i], 2);
    Serial.print(F("  ->  "));
    Serial.print(phPredictVoltage(checkPh[i], slope, intercept), 1);
    Serial.println(F(" mV"));
  }
  Serial.println(F("================================"));
  Serial.println();
}