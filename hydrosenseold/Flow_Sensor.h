#ifndef FLOW_SENSOR_H
#define FLOW_SENSOR_H

#include <Arduino.h>
#include "config.h"

// ================== KONFIGURASI KHUSUS PIPA ==================
// Kalau mau lebih rapi, bagian ini boleh kamu pindah ke config.h

// Diameter pipa 16 inch = 0.4064 m
#ifndef PIPE_DIAMETER_M
  #define PIPE_DIAMETER_M 0.453f
#endif

// Koefisien Manning untuk pipa halus (PVC/baja halus), kira-kira:
#ifndef PIPE_MANNING_N
  #define PIPE_MANNING_N 0.013f   // bisa kamu sesuaikan kalau ada data
#endif

// Kemiringan hidrolis (slope), misal beda tinggi 1 cm per 1 m = 0.01
#ifndef PIPE_SLOPE
  #define PIPE_SLOPE 0.01f        // HARUS kamu sesuaikan dengan kondisi nyata
#endif

// Kalibrasi level (hasil uji sensor → ketinggian air di pipa dalam meter)
// SEMENTARA pakai nilai lama dulu, nanti kamu kalibrasi ulang dan ganti di sini
#ifndef LEVEL_M_SLOPE
  #define LEVEL_M_SLOPE  0.00001f   // ganti setelah kalibrasi ulang
#endif

#ifndef LEVEL_M_OFFSET
  #define LEVEL_M_OFFSET -0.1f  // ganti setelah kalibrasi ulang
#endif
// =============================================================

class Flow_Sensor {
  private:
    float lastLevel_m;     // level air terakhir (meter, dari dasar pipa)
    float lastFlow_m3s;    // debit terakhir (m3/s)

  public:
    Flow_Sensor()
      : lastLevel_m(-1.0f),
        lastFlow_m3s(-1.0f) {}

    void begin() {
      pinMode(FLOW_LEVEL_PIN, INPUT);
      lastLevel_m  = -1.0f;
      lastFlow_m3s = -1.0f;

      Serial.println("Flow_Sensor initialized (pipa lingkaran + Manning).");
    }

    // === Baca ketinggian air (meter) pakai EMA + kalibrasi linear ===
    float readLevelMeter() {
      int adcValue = analogRead(FLOW_LEVEL_PIN);

      // Exponential Moving Average (sama seperti program kalibrasimu)
      static float ema = 0.0f;
      const float alpha = 0.05f;   // makin kecil makin halus

      ema = (alpha * adcValue) + (1.0f - alpha) * ema;

      // Kalibrasi: ADC (ema) → tinggi air (meter)
      // SEMENTARA pakai slope & offset lama, nanti tolong dikalibrasi ulang
      float level_m = (LEVEL_M_SLOPE * ema) + LEVEL_M_OFFSET;

      // Debug seperti program kalibrasi
      Serial.print("Raw : ");
      Serial.print(adcValue);
      Serial.print(" || Filtered : ");
      Serial.print(ema);
      Serial.print(" || Level (m) : ");
      Serial.println(level_m);

      // Jangan biarkan negatif, dan batasi ke tinggi pipa
      if (level_m < 0.0f) {
        level_m = 0.0f;
      }

      // Tinggi maksimum dari dasar pipa = diameter pipa
      level_m = constrain(level_m, 0.0f, PIPE_DIAMETER_M);

      lastLevel_m = level_m;
      return level_m;   // meter
    }

    float getLastLevel() const {
      return lastLevel_m;
    }

    /*
      Hitung debit di pipa lingkaran pakai pendekatan open channel + Manning:

      - level_m : tinggi air di dalam pipa dari dasar (meter)
      - D       : diameter pipa (meter)
      - R       : jari-jari pipa (meter)
      - y       : tinggi air (dibatasi 0..D)

      Rumus penampang pipa terisi sebagian:
        θ = 2 * acos((R - y)/R)
        A = 0.5 * R^2 * (θ - sin(θ))      // luas basah (m2)
        P = R * θ                         // keliling basah (m)
        Rh = A / P                        // jari-jari hidrolis (m)

      Manning:
        V = (1/n) * Rh^(2/3) * S^(1/2)   // m/s
        Q = A * V                        // m3/s
    */
    float calculateFlowRate(float level_m) {
      if (level_m <= 0.0f) {
        lastFlow_m3s = 0.0f;
        return lastFlow_m3s;
      }

      const float D = PIPE_DIAMETER_M;
      const float R = D * 0.5f;

      // Batasi level ke maksimum diameter (kalau sensor baca lebih)
      float y = level_m;
      if (y > D) {
        y = D;
      }

      // Hindari nilai yang bikin acos() error
      float ratio = (R - y) / R;
      if (ratio < -1.0f) ratio = -1.0f;
      if (ratio >  1.0f) ratio =  1.0f;

      float theta = 2.0f * acos(ratio);           // radian
      float area  = 0.5f * R * R * (theta - sin(theta)); // m2
      float perim = R * theta;                    // m

      // Kalau perimeternya nol (harusnya nggak), amanin saja
      if (perim <= 0.0f) {
        lastFlow_m3s = 0.0f;
        return lastFlow_m3s;
      }

      float Rh = area / perim;                    // jari-jari hidrolis

      const float n = PIPE_MANNING_N;
      const float S = PIPE_SLOPE;

      // Kecepatan aliran (m/s) menurut Manning
      float velocity = (1.0f / n) * pow(Rh, 2.0f / 3.0f) * sqrt(S);

      // Debit (m3/s)
      float Q = area * velocity;

      lastFlow_m3s = Q;
      return Q;
    }

    float getLastFlow_m3s() const {
      return lastFlow_m3s;
    }
};

#endif
