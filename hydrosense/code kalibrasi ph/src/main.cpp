#include <Arduino.h>
#include "Config.h"
#include "Sensor.h"
#include "Network.h"

// =====================================================================
//  Hydrosense - sensor pH Modbus RS485
//  Firmware TIDAK menghitung/mengoreksi pH. Yang dilakukan di sini:
//  penskalaan, gerbang validitas & stabilitas, median N, pembulatan,
//  evaluasi baku mutu, lalu kirim nilai bersih + metadata mutu ke web.
//  Kalibrasi dua titik ditulis ke register sensor (rumus_kalibrasi_ph.md).
// =====================================================================

void setup() {
  // Tunggu 4 detik SEBELUM memulai apapun.
  // Ini memberi waktu bagi Serial Monitor VSCode untuk benar-benar terhubung
  // setelah proses upload/restart, sehingga Anda tidak kehilangan teks awal.
  delay(4000);
  
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== Hydrosense pH Modbus RS485 ===");
  Serial.println("Memulai inisialisasi...");

  setupSensor();
  setupWiFi();
  setupTime();

  if (wifiReady) {
    firebasePushCalPoints();
  }
  Serial.printf("Serial2 RX=%d TX=%d DE/RE=%d, Slave ID %d, pH@0x%04X suhu@0x%04X\n",
                RS485_RX_PIN, RS485_TX_PIN, RS485_RE_DE_PIN, SENSOR_SLAVE_ID,
                PH_REGISTER_ADDRESS, TEMP_REGISTER_ADDRESS);
  Serial.printf("Median %d sampel @%d ms, baku mutu pH %.1f-%.1f\n",
                MEDIAN_SAMPLES, SAMPLE_INTERVAL_MS, BAKU_MUTU_PH_MIN, BAKU_MUTU_PH_MAX);

  if (calibrated) {
    Serial.printf("Kalibrasi 2 titik sudah ditulis ke sensor (epoch %lu).\n", lastCalibrated);
    Serial.printf("Verifikasi: %d/3 titik, %s.\n", verif.n, verif.pass ? "LULUS" : "belum lulus");
  } else {
    Serial.println("Kalibrasi 2 titik BELUM ditulis dari firmware ini (sensor pakai koefisien internalnya).");
  }

  Serial.println("\nCOMMAND:");
  Serial.println(" 4 = Kalibrasi titik 1, buffer pH 4.01  (tulis ke sensor)");
  Serial.println(" 1 = Kalibrasi titik 2, buffer pH 9.01  (tulis ke sensor)");
  Serial.println(" A = Verifikasi buffer pH 4.01           (baca saja)");
  Serial.println(" 7 = Verifikasi buffer pH 7.01           (baca saja)");
  Serial.println(" C = Verifikasi buffer pH 9.01           (baca saja)");
  Serial.println(" O = Tulis offset sistematis ke register deviasi 0x0050");
  Serial.println(" X = Batal proses yang sedang berjalan");
  Serial.println(" S = Status");
  Serial.println(" H = Laporan kalibrasi & verifikasi (slope, R2, RMSE)");
  Serial.println(" D = Dump register Modbus 0x0000-0x000F");
  Serial.println(" R = Hapus catatan kalibrasi di ESP32");
  Serial.println();
}

void loop() {
  // 1. Cek perintah serial
  if (Serial.available()) {
    char command = Serial.read();
    if      (command == '4')                    calibratePH4();
    else if (command == '1')                    calibratePH10();
    else if (command == 'A' || command == 'a')  verifyPH4();
    else if (command == '7')                    verifyPH7();
    else if (command == 'C' || command == 'c')  verifyPH10();
    else if (command == 'O' || command == 'o')  writeDeviation();
    else if (command == 'R' || command == 'r')  resetCalibration();
    else if (command == 'S' || command == 's')  showStatus();
    else if (command == 'H' || command == 'h')  showHealthReport();
    else if (command == 'D' || command == 'd')  dumpModbusRegisters();
  }

  // 2. Cek perintah dari cloud
  checkFirebaseCommand();

  // 3. Siklus pembacaan: median N sampel -> gerbang -> kirim
  static unsigned long lastRead = 0;
  if (millis() - lastRead >= READ_PERIOD_MS) {
    lastRead = millis();
    PhReading r = readSensor();

    if (!r.valid) {
      Serial.printf("quality=error | %d/%d sampel gagal | sensor %s\n",
                    r.modbusErrors, MEDIAN_SAMPLES, sensorOnline ? "menjawab tapi data tak valid" : "tidak merespons");
    } else {
      Serial.printf("pH %.2f (%s) | %.1f C | quality=%s sd=%.3f drift=%.3f | n=%d err=%d | baku mutu: %s\n",
                    r.ph, r.phStatus, r.tempC, r.quality,
                    isnan(r.sd) ? 0.0 : r.sd, isnan(r.drift) ? 0.0 : r.drift,
                    r.sampleCount, r.modbusErrors, r.compliant ? "TAAT" : "TIDAK/UNSTABLE");
    }

    firebaseLogReading(r);   // saat error tetap dikirim, tanpa angka pH
  }
}
