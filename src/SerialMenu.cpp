// Menu & perintah Serial Monitor (kalibrasi pH, diagnosa TSS, tes relay).
// Dijalankan oleh taskSerial; pemanggil sudah memegang mutex Modbus.
#include "App.h"
#include "Kolam.h"
#include "Rain.h"
#include "SdQueue.h"
#include <math.h>

// Baca satu angka dari Serial Monitor (diakhiri Enter). Sisa Enter dari perintah
// huruf sebelumnya dibuang dulu. false bila waktu habis atau input kosong.
static bool bacaAngkaSerial(const char *prompt, float &out) {
  Serial.print(prompt);
  unsigned long batas = millis() + 60000UL;
  String s;
  while (millis() < batas) {
    if (!Serial.available()) {
      delay(20);
      continue;
    }
    char c = (char)Serial.read();
    // Enter bisa berupa \r, \n, atau \r\n tergantung Serial Monitor.
    if (c == '\r' || c == '\n') {
      if (s.length() == 0)
        continue; // sisa Enter dari perintah huruf
      break;
    }
    s += c;
  }
  s.trim();
  if (s.length() == 0) {
    Serial.println(F("(tidak ada angka diterima / waktu habis)"));
    return false;
  }
  s.replace(',', '.');
  out = s.toFloat();
  Serial.printf("diterima: '%s' = %.4f\n", s.c_str(), (double)out);
  return true;
}

void tampilkanMenuSerial() {
  Serial.println("\n=== Hydrosense Inlet System ===");
  Serial.println("COMMAND KALIBRASI PH SENSOR:");
  Serial.println(" 4 = Kalibrasi titik 1 (pH 4.01)");
  Serial.println(" 1 = Kalibrasi titik 2 (pH 9.18)");
  Serial.println(" A = Verifikasi pH 4.01");
  Serial.println(" 7 = Verifikasi pH 6.86");
  Serial.println(" C = Verifikasi pH 9.18");
  Serial.println(" S = Tampilkan Status Sensor & Sistem");
  Serial.println(" T = Baca Status & Data TSS Sensor");
  Serial.println(" H = Health Report");
  Serial.println(" D = Dump Modbus Registers");
  Serial.println(" X = Diagnosa bus RS485 (cetak byte mentah ID 1-3, 3 baud)");
  Serial.println(" Q = Status antrean SD card (data offline)");
  Serial.println(
      " N = Info Status WiFi (SSID/IP/RSSI) - bisa dicek kapan saja");
  Serial.println(" L = Scan WiFi sekitar (cek SSID target terlihat/tidak)");
  Serial.println(" P = Tes relay pompa (toggle ON/OFF, tanpa WiFi/Supabase)");
  Serial.println(" K = Kirim manual pH+TSS ke Supabase SEKARANG (paksa, tanpa "
                 "nunggu siklus otomatis/level air)");
  Serial.println(" U = Status GSM (modem SIM7600: AT, SIM, sinyal, operator, tes Supabase)");
  Serial.println(" Y = Status rain gauge (ketukan, mm/ketukan, hari ini)");
  Serial.println(" Z = Nolkan total hujan hari ini");
  Serial.println(" J = Set tinggi air maksimum & batas minimal kolam (cm)");
  Serial.println(" M = Set mm per ketukan (ketik angka lalu Enter)");
  Serial.println(" B / E = Kalibrasi rain gauge dengan air takaran (B mulai, E selesai)");
  Serial.println(
      " F = Reset factor TSS (dinonaktifkan sampai register RD terverifikasi)");
  Serial.println(
      " G = Uji factor TSS (dinonaktifkan sampai register RD terverifikasi)");
  Serial.println(" V = Uji two-point TSS (dinonaktifkan sampai register RD "
                 "terverifikasi)");
  Serial.println();
}

// Diagnosa bus RS485 mentah: kirim request FC03 (baca 1 register) ke beberapa
// ID dan baud rate, lalu cetak SEMUA byte yang kembali. Membedakan "sensor
// diam total" (wiring/arah/catu) dari "sensor menjawab tapi salah parameter".
static uint16_t crc16Modbus(const uint8_t *d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  }
  return crc;
}

static void diagnosaRs485() {
  static const uint32_t baudUji[] = {9600, 4800, 19200};
  Serial.println(F("\n=== DIAGNOSA RS485 (kirim FC03 reg 0x0001, ID 1..3) ==="));
  Serial.printf("Pin: TX=GPIO%d RX=GPIO%d DE/RE=GPIO%d\n", RS485_TX_PIN,
                RS485_RX_PIN, RS485_RE_DE_PIN);
  bool adaJawaban = false;
  for (uint32_t baud : baudUji) {
    Serial2.updateBaudRate(baud);
    for (uint8_t id = 1; id <= 3; id++) {
      uint8_t req[8] = {id, 0x03, 0x00, 0x01, 0x00, 0x01, 0, 0};
      uint16_t crc = crc16Modbus(req, 6);
      req[6] = crc & 0xFF;
      req[7] = crc >> 8;
      while (Serial2.available())
        Serial2.read(); // buang sisa byte lama
      digitalWrite(RS485_RE_DE_PIN, HIGH);
      Serial2.write(req, 8);
      Serial2.flush();
      digitalWrite(RS485_RE_DE_PIN, LOW);
      uint8_t buf[32];
      size_t n = 0;
      unsigned long mulai = millis();
      while (millis() - mulai < 400 && n < sizeof(buf)) {
        if (Serial2.available())
          buf[n++] = Serial2.read();
      }
      Serial.printf("baud %5lu ID %u -> ", (unsigned long)baud, id);
      if (n == 0) {
        Serial.println(F("tidak ada byte kembali"));
      } else {
        adaJawaban = true;
        for (size_t i = 0; i < n; i++)
          Serial.printf("%02X ", buf[i]);
        Serial.println();
      }
    }
  }
  Serial2.updateBaudRate(RS485_BAUD); // kembalikan baud normal
  Serial.println(adaJawaban
                     ? F("Ada jawaban -> catat baud/ID yang menjawab.")
                     : F("TIDAK ADA jawaban sama sekali -> cek TX/RX, A/B, "
                         "DE/RE, catu & GND sensor."));
}

void prosesPerintahSerial(char command) {
  const bool perintahKalibrasiHwTss = command == 'F' || command == 'f' ||
                                      command == 'G' || command == 'g' ||
                                      command == 'V' || command == 'v';
  if (perintahKalibrasiHwTss && !ENABLE_TSS_HW_REGISTER_CALIBRATION) {
    Serial.println(F("[TSS] Perintah F/G/V dinonaktifkan sementara: "
                     "register kalibrasi RD-SSBWT-01 belum terverifikasi."));
  } else if (command == '4')
    calibratePH4();
  else if (command == '1')
    calibratePH10();
  else if (command == '3')
    calibratePH7();
  else if (command == 'A' || command == 'a')
    verifyPH4();
  else if (command == '7')
    verifyPH7();
  else if (command == 'C' || command == 'c')
    verifyPH10();
  else if (command == 'O' || command == 'o')
    writeDeviation();
  else if (command == 'R' || command == 'r')
    resetCalibration();
  else if (command == 'S' || command == 's')
    showStatus();
  else if (command == 'T' || command == 't')
    tssSensor.readStatus();
  else if (command == 'H' || command == 'h')
    showHealthReport();
  else if (command == 'D' || command == 'd')
    dumpModbusRegisters();
  else if (command == 'X' || command == 'x')
    diagnosaRs485();
  else if (command == 'Q' || command == 'q')
    sdQueueStatus();
  else if (command == 'N' || command == 'n')
    tampilkanStatusWiFi();
  else if (command == 'L' || command == 'l')
    scanWiFiSekitar();
  else if (command == 'P' || command == 'p') {
    // Tes relay pompa langsung dari Serial (toggle), tanpa Supabase/WiFi --
    // untuk memisahkan masalah hardware relay dari masalah jalur perintah.
    if (pumpState)
      pumpOFF();
    else
      pumpON();
  } else if (command == 'K' || command == 'k')
    kirimDataManualSekarang();
  else if (command == 'U' || command == 'u')
    gsmCetakStatus();
  else if (command == 'Y' || command == 'y')
    rainCetakStatus();
  else if (command == 'Z' || command == 'z') {
    rainNolkanHariIni();
    Serial.println(F("[RAIN] Total hari ini dinolkan."));
    rainCetakStatus();
  } else if (command == 'J' || command == 'j') {
    Serial.printf("[KOLAM] Sekarang: tinggi maksimum %.1f cm, batas minimal %.1f cm\n",
                  (double)kolamMaxAirCm(), (double)kolamMinAirCm());
    float cm;
    // Maksimum dulu supaya batas minimal baru divalidasi terhadap maksimum yang baru.
    if (bacaAngkaSerial("[KOLAM] Tinggi MAKSIMUM baru (cm, jarak sensor-dasar; kosong = lewati): ", cm)) {
      if (kolamSetMaxAirCm(cm))
        Serial.printf("[KOLAM] Disimpan: tinggi maksimum %.1f cm\n", (double)cm);
      else
        Serial.printf("[KOLAM] Ditolak: harus 10 - %.0f cm dan >= batas minimal.\n",
                      (double)RADAR_RANGE_CM);
    }
    if (bacaAngkaSerial("[KOLAM] Batas MINIMAL baru (cm, mis. 20; kosong = lewati): ", cm)) {
      if (kolamSetMinAirCm(cm))
        Serial.printf("[KOLAM] Disimpan: siklus otomatis jalan bila air >= %.1f cm\n",
                      (double)cm);
      else
        Serial.printf("[KOLAM] Ditolak: harus 0 - %.1f cm.\n", (double)kolamMaxAirCm());
    }
  } else if (command == 'M' || command == 'm') {
    float mm;
    if (bacaAngkaSerial("[RAIN] Ketik mm per ketukan lalu Enter (mis. 0.2): ", mm)) {
      if (rainSetMmPerTip(mm))
        Serial.printf("[RAIN] Disimpan: %.3f mm/ketukan\n", (double)mm);
      else
        Serial.println(F("[RAIN] Ditolak: nilai harus 0.01 - 5 mm."));
    }
  } else if (command == 'B' || command == 'b') {
    rainKalibrasiMulai();
    Serial.println(F("[RAIN] Kalibrasi dimulai. Tuang air takaran PELAN-PELAN ke "
                     "corong (5-10 menit), lalu kirim 'E'."));
  } else if (command == 'E' || command == 'e') {
    if (!rainKalibrasiJalan()) {
      Serial.println(F("[RAIN] Belum dimulai. Kirim 'B' dulu."));
    } else {
      uint32_t n = rainKetukanKalibrasi();
      float vMl;
      Serial.printf("[RAIN] Ketukan sejak 'B': %u\n", (unsigned)n);
      if (n == 0) {
        Serial.println(F("[RAIN] Tidak ada ketukan, kalibrasi dibatalkan."));
        rainKalibrasiSelesai();
      } else if (bacaAngkaSerial("[RAIN] Volume air yang dituang (mL): ", vMl) &&
                 vMl > 0) {
        // h_tip = V / (A * N); 1 mL = 1000 mm^3; A = luas bukaan corong (Config.h)
        const float luas = RAIN_LUAS_CORONG_MM2;
        float h = (vMl * 1000.0f) / (luas * (float)n);
        Serial.printf("[RAIN] Hasil: %.4f mm/ketukan (V=%.1f mL, A=%.0f mm2, N=%u)\n",
                      (double)h, (double)vMl, (double)luas, (unsigned)n);
        if (rainSetMmPerTip(h))
          Serial.println(F("[RAIN] Nilai baru DISIMPAN dan langsung dipakai."));
        else
          Serial.println(F("[RAIN] Di luar 0.01-5 mm, TIDAK disimpan (cek input)."));
        rainKalibrasiSelesai();
      } else {
        Serial.println(F("[RAIN] Input tidak valid, kalibrasi dibatalkan."));
        rainKalibrasiSelesai();
      }
    }
  }
  else if (command == 'F' || command == 'f') {
    Serial.println(F("\n>>> RESET FACTOR & MODE KALIBRASI TSS (paksa via "
                     "Serial, tanpa WiFi)..."));
    bool ok1 = tssSensor.setModeKalibrasi(TSS_CAL_MODE_FACTOR);
    delay(150);
    bool ok2 = tssSensor.tulisFactor(1.0f);
    if (ok1 && ok2) {
      Serial.println(F("Berhasil: mode diset ke factor(1), factor "
                       "direset ke 1.0"));
    } else {
      Serial.println(F("GAGAL sebagian/seluruhnya -- cek kabel RS485 "
                       "sensor TSS."));
    }
    delay(200);
    tssSensor.readStatus(); // tampilkan ulang status buat verifikasi
  } else if (command == 'G' || command == 'g') {
    // Uji pembuktian: apakah tulisan ke register 6 (Factor) BENAR-BENAR
    // mempengaruhi TSS Value (register 2), atau cuma readback-nya saja yang
    // ngaco. Tulis factor=1.0, baca TSS, lalu tulis factor=5.0, baca TSS
    // lagi -- kalau proporsional (~5x lebih besar), factor beneran nempel.
    Serial.println(F("\n>>> UJI PEMBUKTIAN FACTOR (bandingkan TSS @1.0 vs "
                     "@5.0)..."));
    tssSensor.tulisFactor(1.0f);
    // Sensor butuh ~4 dtk (response time, register 12) untuk menerapkan
    // factor baru, jadi tunggu 8 dtk lalu rata-ratakan 3 bacaan.
    auto bacaStabil = []() {
      delay(8000);
      float jumlah = 0;
      int n = 0;
      for (int i = 0; i < 3; i++) {
        float v = tssSensor.readTSS();
        if (v >= 0) {
          jumlah += v;
          n++;
        }
        delay(1000);
      }
      return n ? jumlah / n : -1.0f;
    };
    Serial.printf("Register factor yang diuji : alamat %u (0x%04X)\n",
                  (unsigned)REG_CAL_FACTOR, (unsigned)REG_CAL_FACTOR);
    float tssPada1 = bacaStabil();
    Serial.printf("TSS saat Factor ditulis 1.0 : %.4f\n", tssPada1);
    tssSensor.tulisFactor(5.0f);
    float tssPada5 = bacaStabil();
    Serial.printf("TSS saat Factor ditulis 5.0 : %.4f\n", tssPada5);
    float rasio = (tssPada1 != 0) ? (tssPada5 / tssPada1) : 0;
    Serial.printf("Rasio (harusnya mendekati 5.0 kalau Factor nempel) : "
                  "%.2f\n",
                  rasio);
    if (rasio > 4.0 && rasio < 6.0) {
      Serial.println(F(">> Factor NEMPEL & mempengaruhi TSS -- readback "
                       "reg6 aja yang salah alamat/ngaco."));
    } else {
      Serial.println(F(">> Factor TIDAK mempengaruhi TSS sama sekali -- "
                       "register faktor yang diuji bukan Factor yang sebenarnya."));
    }
    // Kembalikan ke 1.0 supaya tidak ketinggalan di 5.0
    tssSensor.tulisFactor(1.0f);
    Serial.println(F("(Factor dikembalikan ke 1.0)"));
  } else if (command == 'V' || command == 'v') {
    // Uji pembuktian TWO-POINT: sama seperti uji Factor, tapi untuk
    // register 20/22/27/28 -- TIDAK perlu pindah media fisik. Kita bikin
    // titik kalibrasi buatan di sekitar raw SEKARANG (S1=raw-10, S2=raw+10,
    // target sembarang P1=0, P2=100), lalu HITUNG prediksi output kalau
    // mekanismenya beneran jalan, dan bandingkan dengan hasil baca ulang.
    Serial.println(F("\n>>> UJI PEMBUKTIAN TWO-POINT (diam saja, tidak "
                     "perlu pindah media)..."));
    float rawSekarang = tssSensor.readTSS();
    Serial.printf("Raw saat ini (X)            : %.4f\n", rawSekarang);
    float s1 = rawSekarang - 10.0f;
    float s2 = rawSekarang + 10.0f;
    float p1 = 0.0f;
    float p2 = 100.0f;
    float slopePrediksi = (p2 - p1) / (s2 - s1);
    float offsetPrediksi = p1 - (slopePrediksi * s1);
    float prediksiOutput = (slopePrediksi * rawSekarang) + offsetPrediksi;
    Serial.printf("Titik buatan: P1=%.1f S1=%.4f | P2=%.1f S2=%.4f\n", p1, s1,
                  p2, s2);
    Serial.printf("Prediksi output KALAU mekanisme jalan : %.4f\n",
                  prediksiOutput);
    bool ok = true;
    ok &= tssSensor.setModeKalibrasi(TSS_CAL_MODE_TWO_POINT);
    delay(150);
    ok &= tssSensor.setTitikKalibrasi(1);
    delay(150);
    ok &= tssSensor.tulisTargetKalibrasi(p1);
    delay(150);
    ok &= tssSensor.tulisAktualKalibrasi(s1);
    delay(150);
    ok &= tssSensor.setTitikKalibrasi(2);
    delay(150);
    ok &= tssSensor.tulisTargetKalibrasi(p2);
    delay(150);
    ok &= tssSensor.tulisAktualKalibrasi(s2);
    delay(150);
    ok &= tssSensor.keluarModeKalibrasi();
    delay(300);
    float hasilAktual = tssSensor.readTSS();
    Serial.printf("Semua langkah tulis          : %s\n",
                  ok ? "SUKSES (ACK)" : "ADA YANG GAGAL");
    Serial.printf("TSS setelah two-point        : %.4f (prediksi %.4f)\n",
                  hasilAktual, prediksiOutput);
    if (fabs(hasilAktual - prediksiOutput) < 5.0f) {
      Serial.println(
          F(">> MEKANISME TWO-POINT JALAN & SESUAI PREDIKSI! Register "
            "20/22/27/28 valid, aman dipakai kalibrasi sungguhan."));
    } else if (fabs(hasilAktual - rawSekarang) < 2.0f) {
      Serial.println(
          F(">> TIDAK ADA EFEK SAMA SEKALI -- sama seperti kasus Factor, "
            "register two-point kemungkinan JUGA tidak cocok di unit ini."));
    } else {
      Serial.println(
          F(">> Ada efek tapi TIDAK sesuai prediksi -- kemungkinan urutan/"
            "rumus regresi sensor beda dari asumsi kita, perlu investigasi "
            "lanjutan."));
    }
    // Buang kalibrasi palsu ini, kembali ke factor=1 (uncorrected)
    tssSensor.setModeKalibrasi(TSS_CAL_MODE_FACTOR);
    delay(150);
    tssSensor.tulisFactor(1.0f);
    Serial.println(F("(Kalibrasi palsu dibuang, dikembalikan ke mode factor)"));
  }
}

// Command 'K' -- baca pH+TSS SEKARANG dan langsung kirim ke Supabase, TANPA
// nunggu siklus otomatis (isi chamber -> stabilisasi -> baca -> kirim) yang
// butuh level air tangki utama cukup tinggi dulu. Berguna buat testing/debug
// supaya dashboard kelihatan ada data walau tangki fisik belum terisi air.
// TSS tetap dikirim walau pH sensor tidak terpasang/invalid (lihat
// kirimDataKeAPI() -- ph & water_temperature dikirim null kalau pH invalid).

void kirimDataManualSekarang() {
  Serial.println(F("\n>>> Kirim manual pH+TSS ke Supabase (paksa, skip siklus "
                   "otomatis)..."));
  HasilPH hasil = bacaProsesPH();
  float tssRaw = 0.0f;
  float tss = bacaRataRataTSS(5, &tssRaw);
  Serial.printf("pH  : %s (valid=%d)\n",
                hasil.valid ? String(hasil.ph, 2).c_str() : "n/a", hasil.valid);
  Serial.printf("TSS : %.2f mg/L (raw=%.2f)\n", tss, tssRaw);
  bool terkirim = kirimDataKeAPI(hasil, tss);
  if (terkirim) {
    Serial.println(F("Terkirim ke Supabase."));
  } else {
    Serial.println(F("Gagal kirim (WiFi belum siap?). Data TIDAK dibuffer -- "
                     "coba lagi manual."));
  }
}
