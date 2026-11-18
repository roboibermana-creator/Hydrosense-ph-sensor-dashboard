// Sinkronisasi dari dashboard web: override mode manual, faktor kalibrasi
// TSS, dan wizard kalibrasi hardware TSS. Dipanggil dari taskKontrol.
#include "App.h"

static unsigned long timerCekOverrideTerakhir = 0;
static const unsigned long INTERVAL_CEK_OVERRIDE_MS = 1000;
static const unsigned long INTERVAL_CEK_KALIBRASI_TSS_MS =
    300000UL; // 5 menit -- faktor kalibrasi jarang berubah
static unsigned long timerCekKalibrasiHwTssTerakhir = 0;
static const unsigned long INTERVAL_CEK_KALIBRASI_HW_TSS_MS =
    GSM_HEMAT ? 8000UL : 2000UL; // wizard interaktif -- operator nunggu respons tiap klik
static long idTerakhirDiprosesKalibrasiHwTss = -1;
unsigned long timerCekKalibrasiTssTerakhir = 0;

// Siklus otomatis punya beberapa state tunggu yang BISA MACET LAMA/tanpa
// batas waktu kalau sensor fisiknya belum terpasang (mis.
// STATE_AUTO_TUNGGU_KOSONG nunggu float switch LOW, padahal pin float switch
// yang belum dikabel akan selalu terbaca HIGH lewat INPUT_PULLUP -> macet
// permanen). Selama macet di state-state ini, mode dari Supabase TIDAK pernah
// dicek ulang, jadi command manual dari web (toggle valve/pompa) kelihatan
// "delay lama" padahal sebenarnya tidak pernah terbaca sampai siklus otomatis
// kebetulan lepas.

static bool sedangDiTengahSiklusOtomatisPanjang() {
  switch (state) {
  case STATE_TUNGGU_AIR:
  case STATE_TUNGGU_FLOAT_SWITCH:
  case STATE_AUTO_ISI_CHAMBER:
  case STATE_AUTO_STABILISASI:
  case STATE_AUTO_BACA_KUALITAS:
  case STATE_AUTO_VALIDASI:
  case STATE_AUTO_TUNGGU_KOSONG:
    return true;
  default:
    return false;
  }
}
// Dipanggil tiap loop(): kalau lagi macet di siklus otomatis DAN Supabase
// bilang mode sudah "manual", langsung potong siklus otomatis saat itu juga
// (pompa dimatikan demi aman) dan pindah ke mode manual -- supaya command
// dari web tidak nunggu siklus otomatis selesai/macet dulu.

void cekOverrideManualDariWeb() {
  if (!sedangDiTengahSiklusOtomatisPanjang())
    return;
  if (millis() - timerCekOverrideTerakhir < INTERVAL_CEK_OVERRIDE_MS)
    return;
  timerCekOverrideTerakhir = millis();
  // Hanya potong siklus kalau fetch BERHASIL dan mode memang bukan otomatis;
  // gangguan jaringan sesaat tidak boleh membuang siklus yang sedang jalan.
  KontrolManual k = bacaKontrolDariSupabase();
  if (k.ok && k.mode != "otomatis") {
    Serial.println(F("\n[OVERRIDE] Mode MANUAL terdeteksi dari web -> memotong "
                     "siklus otomatis."));
    pompaOff();
    modeOtomatis = false;
    state = STATE_M_BACA_PERINTAH;
  }
}
// Dipanggil tiap loop() (throttled): tarik faktor kalibrasi TSS (slope,
// cal_offset) terbaru dari Supabase dan terapkan ke tssSensor, supaya operator
// bisa kalibrasi ulang probe TSS dari dashboard web tanpa reflash firmware.

void cekKalibrasiTSSDariWeb() {
  // timer == 0 berarti "belum pernah diambil / dipaksa" (ModeInisialisasi mengisinya 0 begitu online):
  // ambil SEKARANG. Dulu hanya dicek millis() - timer, yang selama 5 menit pertama setelah boot
  // selalu < interval, sehingga faktor kalibrasi dari web baru dipakai ~5 menit setelah nyala.
  if (!internetSiap() ||
      (timerCekKalibrasiTssTerakhir != 0 &&
       millis() - timerCekKalibrasiTssTerakhir < INTERVAL_CEK_KALIBRASI_TSS_MS))
    return;
  timerCekKalibrasiTssTerakhir = millis();
  KalibrasiTSS k = bacaKalibrasiTSSDariSupabase();
  if (!k.ok) // gagal: coba lagi sekitar 10 detik, bukan menunggu 5 menit
    timerCekKalibrasiTssTerakhir = millis() - (INTERVAL_CEK_KALIBRASI_TSS_MS - 10000UL);
  if (k.ok) {
    float slopeLama, offsetLama;
    tssSensor.getCalibration(slopeLama, offsetLama);
    if (slopeLama != k.slope || offsetLama != k.offset) {
      tssSensor.setCalibration(k.slope, k.offset);
      Serial.printf("[KALIBRASI TSS] Faktor diperbarui dari web -> slope=%.6f "
                    "offset=%.6f\n",
                    k.slope, k.offset);
    }
  }
}
// ---------------- Alat Modbus dari web (tabel modbus_cmd) ----------------
// Baca/tulis register sensor RS485 atas perintah dashboard (pengganti Modbus
// Poll). Bus dipegang lewat mutex Modbus. Perintah 'tulis' hanya sampai ke
// tabel kalau lolos PIN di database (fungsi modbus_tulis).
#include <ModbusMaster.h>

static ModbusMaster nodeModbusTool;
static void toolPreTx() { digitalWrite(RS485_RE_DE_PIN, HIGH); }
static void toolPostTx() { digitalWrite(RS485_RE_DE_PIN, LOW); }

static unsigned long timerCekModbusTerakhir = 0;
static const unsigned long INTERVAL_CEK_MODBUS_MS = GSM_HEMAT ? 8000UL : 1500UL;
static long idTerakhirDiprosesModbus = -1;

// Float32 -> dua register, urutan low word dulu (CDAB, sama seperti sensor).
static void floatKeRegister(float v, uint16_t &w0, uint16_t &w1) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  w0 = bits & 0xFFFF;
  w1 = (bits >> 16) & 0xFFFF;
}

// Alamat 16 (baud) dan 17 (alamat node) dikunci: salah tulis bisa membuat
// sensor tidak terbaca lagi di bus.
static bool alamatTerlarang(int alamat, int jumlah) {
  return alamat <= 17 && (alamat + jumlah - 1) >= 16;
}

static bool baselineModbusSiap = false;

void cekPerintahModbusDariWeb() {
  if (millis() - timerCekModbusTerakhir < INTERVAL_CEK_MODBUS_MS)
    return;
  if (!internetSiap())
    return;
  timerCekModbusTerakhir = millis();

  PerintahModbus p = bacaPerintahModbusDariSupabase(idTerakhirDiprosesModbus);
  if (!p.ok)
    return;

  // SAFETY BOOT: perintah yang sudah menunggu saat alat baru menyala
  // dianggap kedaluwarsa (terutama 'tulis'), tidak dijalankan.
  if (!baselineModbusSiap) {
    baselineModbusSiap = true;
    idTerakhirDiprosesModbus = p.id;
    laporkanHasilModbus(p.id, false, "",
                        "Kedaluwarsa: alat baru menyala, kirim ulang.");
    return;
  }
  idTerakhirDiprosesModbus = p.id;

  bool sukses = false;
  String hasil;
  String pesan;

  if (p.slaveId != SENSOR_SLAVE_ID && p.slaveId != TSS_SLAVE_ID) {
    pesan = "Slave ID harus " + String(SENSOR_SLAVE_ID) + " (pH) atau " +
            String(TSS_SLAVE_ID) + " (TSS).";
  } else if (p.jumlah < 1 || p.jumlah > 16) {
    pesan = "Jumlah register harus 1..16.";
  } else if (!kunciModbus(10000)) {
    pesan = "Bus Modbus sibuk, coba lagi.";
  } else {
    nodeModbusTool.begin((uint8_t)p.slaveId, Serial2);
    nodeModbusTool.preTransmission(toolPreTx);
    nodeModbusTool.postTransmission(toolPostTx);

    if (p.aksi == "baca") {
      uint8_t r = nodeModbusTool.readHoldingRegisters(p.alamat, p.jumlah);
      if (r == nodeModbusTool.ku8MBSuccess) {
        for (int i = 0; i < p.jumlah; i++) {
          if (i)
            hasil += ",";
          hasil += String(nodeModbusTool.getResponseBuffer(i));
        }
        sukses = true;
        pesan = "OK";
      } else {
        pesan = "Modbus error 0x" + String(r, HEX) +
                (r == 0xE2 ? " (timeout: sensor tidak menjawab)" : "");
      }
    } else if (p.aksi == "tulis") {
      uint16_t kata[16];
      int n = 0;
      bool parseOk = true;
      int mulai = 0;
      String v = p.nilai + ",";
      while (mulai < (int)v.length() && parseOk) {
        int koma = v.indexOf(',', mulai);
        String tok = v.substring(mulai, koma);
        tok.trim();
        mulai = koma + 1;
        if (tok.length() == 0)
          continue;
        if (p.tipe == "float") {
          if (n + 2 > 16) {
            parseOk = false;
            break;
          }
          float f = tok.toFloat();
          if (!isfinite(f)) {
            parseOk = false;
            break;
          }
          floatKeRegister(f, kata[n], kata[n + 1]);
          n += 2;
        } else {
          long x = tok.toInt();
          if (x < -32768 || x > 65535 || n + 1 > 16) {
            parseOk = false;
            break;
          }
          kata[n++] = (uint16_t)(x & 0xFFFF);
        }
      }
      if (!parseOk || n == 0) {
        pesan = "Nilai tulis tidak valid.";
      } else if (alamatTerlarang(p.alamat, n)) {
        pesan = "Alamat 16-17 (baud/ID sensor) diblokir.";
      } else {
        uint8_t r;
        if (n == 1) {
          r = nodeModbusTool.writeSingleRegister(p.alamat, kata[0]);
        } else {
          for (int i = 0; i < n; i++)
            nodeModbusTool.setTransmitBuffer(i, kata[i]);
          r = nodeModbusTool.writeMultipleRegisters(p.alamat, n);
        }
        if (r == nodeModbusTool.ku8MBSuccess) {
          sukses = true;
          pesan = "Tertulis " + String(n) + " register di alamat " +
                  String(p.alamat) + ". Baca ulang untuk memastikan.";
        } else {
          pesan = "Modbus error 0x" + String(r, HEX) + " saat menulis.";
        }
      }
    } else {
      pesan = "Aksi tidak dikenal: " + p.aksi;
    }
    lepasModbus();
  }

  Serial.printf("[MODBUS WEB] #%ld %s slave %d @%d x%d -> %s (%s)\n", p.id,
                p.aksi.c_str(), p.slaveId, p.alamat, p.jumlah,
                sukses ? "OK" : "GAGAL", pesan.c_str());
  laporkanHasilModbus(p.id, sukses, hasil.c_str(), pesan.c_str());
}

// Batas tinggi air minimal dari dashboard (menu KALIBRASI). Diambil tiap 30 detik; hanya ditulis ke
// NVS bila berubah (hemat flash). Nilai kosong di web = pakai nilai lokal alat.
#include "Kolam.h"
void cekMinAirDariWeb() {
  static unsigned long terakhir = 0;
  static bool pernah = false;
  if (pernah && millis() - terakhir < 30000UL)
    return;
  if (!internetSiap())
    return;
  terakhir = millis();
  pernah = true;
  float mn = 0, mx = 0;
  bool adaMn, adaMx;
  if (!bacaBatasAirDariSupabase(mn, adaMn, mx, adaMx))
    return;
  // Maksimum dulu: batas minimal divalidasi terhadap maksimum yang baru.
  if (adaMx && fabsf(mx - kolamMaxAirCm()) >= 0.05f) {
    if (kolamSetMaxAirCm(mx))
      Serial.printf("[KOLAM] Tinggi air maksimum diperbarui dari web -> %.1f cm\n", (double)mx);
    else
      Serial.printf("[KOLAM] Tinggi maksimum %.1f cm dari web DITOLAK (di luar rentang / di bawah minimal)\n", (double)mx);
  }
  if (adaMn && fabsf(mn - kolamMinAirCm()) >= 0.05f) {
    if (kolamSetMinAirCm(mn))
      Serial.printf("[KOLAM] Batas tinggi air minimal diperbarui dari web -> %.1f cm\n", (double)mn);
    else
      Serial.printf("[KOLAM] Batas minimal %.1f cm dari web DITOLAK (harus 0..maksimum)\n", (double)mn);
  }
}

// Jadwal siklus mode otomatis dari dashboard web (kolom jadwal di tabel devices).
// Diambil tiap 30 detik; perubahan berlaku mulai penghitungan berikutnya.
static unsigned long timerCekJadwalTerakhir = 0;
static bool jadwalPernahDiambil = false;
static const unsigned long INTERVAL_CEK_JADWAL_MS = 30000UL;

void cekJadwalOtomatisDariWeb() {
  if (jadwalPernahDiambil &&
      millis() - timerCekJadwalTerakhir < INTERVAL_CEK_JADWAL_MS)
    return;
  if (!internetSiap())
    return;
  timerCekJadwalTerakhir = millis();
  JadwalOtomatis j = bacaJadwalOtomatisDariSupabase();
  if (!j.ok)
    return;
  bool pengambilanPertama = !jadwalPernahDiambil;
  jadwalPernahDiambil = true;
  unsigned long jeda =
      (unsigned long)constrain(j.intervalDetik, 10, 86400) * 1000UL;
  unsigned long pompa =
      (unsigned long)constrain(j.pompaDetik, 1, 7200) * 1000UL;
  unsigned long valve =
      (unsigned long)constrain(j.valveDetik, 1, 7200) * 1000UL;
  unsigned long bacaSensor =
      (unsigned long)constrain(j.bacaSensorDetik, 5, 3600) * 1000UL;
  unsigned long stabil =
      (unsigned long)constrain(j.stabilDetik, 1, 600) * 1000UL;
  unsigned long sampel = (unsigned long)constrain(j.sampelMs, 200, 10000);
  int trim = constrain(j.trimPersen, 0, 40);
  if (jeda != jedaSiklusMs || pompa != pompaMaksMs || valve != valveMaksMs ||
      bacaSensor != intervalBacaSensorMs || stabil != stabilMs ||
      sampel != intervalSampelMs || trim != trimPersen) {
    jedaSiklusMs = jeda;
    pompaMaksMs = pompa;
    valveMaksMs = valve;
    intervalBacaSensorMs = bacaSensor;
    stabilMs = stabil;
    intervalSampelMs = sampel;
    trimPersen = trim;
    // Jadwal disimpan dari web saat mode otomatis & tidak sedang siklus ->
    // langsung mulai siklus (bukan pengambilan pertama setelah boot).
    bool siklusJalan = state != STATE_AUTO_BACA_LEVEL &&
                       state != STATE_A_CEK_STOP && state != STATE_A_BACA_MODE;
    if (!pengambilanPertama && modeOtomatis && !siklusJalan)
      siklusPertama = true;
    Serial.printf("[JADWAL] Diperbarui dari web: siklus tiap %lu dtk, pompa "
                  "maks %lu dtk, valve maks %lu dtk, baca sensor tiap %lu dtk, "
                  "stabil %lu dtk, sampel tiap %lu ms, trim %d%%\n",
                  jeda / 1000UL, pompa / 1000UL, valve / 1000UL,
                  bacaSensor / 1000UL, stabil / 1000UL, sampel, trim);
  }
}

// Kalibrasi/verifikasi sensor pH dari dashboard web. Prosesnya sama dengan
// perintah Serial (SerialMenu.cpp): blocking sampai pembacaan stabil, dan
// memegang mutex Modbus selama berjalan. Hasil dilapor balik ke Supabase.
static unsigned long timerCekKalibrasiPhTerakhir = 0;
static const unsigned long INTERVAL_CEK_KALIBRASI_PH_MS = GSM_HEMAT ? 8000UL : 2000UL;
static long idTerakhirDiprosesKalibrasiPh = -1;

static bool baselinePhSiap = false;

void cekPerintahKalibrasiPhDariWeb() {
  if (millis() - timerCekKalibrasiPhTerakhir < INTERVAL_CEK_KALIBRASI_PH_MS)
    return;
  if (!internetSiap())
    return;
  timerCekKalibrasiPhTerakhir = millis();
  PerintahKalibrasiPh p =
      bacaPerintahKalibrasiPhDariSupabase(idTerakhirDiprosesKalibrasiPh);
  // SAFETY BOOT: perintah yang SUDAH menunggu saat alat baru nyala dianggap
  // kedaluwarsa dan TIDAK dieksekusi (menulis kalibrasi ke sensor harus
  // disengaja, bukan efek samping reboot). Hanya perintah yang dikirim
  // setelah boot yang dijalankan.
  if (!baselinePhSiap) {
    if (!internetSiap())
      return;
    baselinePhSiap = true;
    if (p.ok) {
      idTerakhirDiprosesKalibrasiPh = p.id;
      laporkanHasilKalibrasiPh(p.id, false,
                               "Kedaluwarsa: alat baru menyala, kirim ulang.");
    }
    return;
  }
  if (!p.ok)
    return;
  idTerakhirDiprosesKalibrasiPh = p.id;
  Serial.printf("\n[KALIBRASI PH] Perintah dari web #%ld: %s\n", p.id,
                p.perintah.c_str());

  bool sukses = false;
  String pesan;
  if (!kunciModbus(30000)) {
    pesan = "Bus Modbus sibuk, coba lagi.";
  } else {
    phCalLastOk = false;
    phCalLastMsg = "";
    phCalIdPerintahWeb = p.id;
    if (p.perintah == "kalibrasi_ph4") {
      calibratePH4();
    } else if (p.perintah == "kalibrasi_ph10") {
      calibratePH10();
    } else if (p.perintah == "kalibrasi_ph7") {
      calibratePH7();
    } else if (p.perintah == "verifikasi_ph4") {
      verifyPH4();
    } else if (p.perintah == "verifikasi_ph7") {
      verifyPH7();
    } else if (p.perintah == "verifikasi_ph10") {
      verifyPH10();
    } else if (p.perintah.startsWith("offset_ph:")) {
      const double nilai = p.perintah.substring(10).toDouble();
      if (nilai < -2.0 || nilai > 2.0) {
        phCalLastMsg = "Offset di luar batas (-2.00 .. +2.00 pH).";
      } else {
        setOffsetPhManual(nilai);
        phCalLastOk = true;
        phCalLastMsg = "Offset manual pH diset " + String(nilai, 2) +
                       " pH (ditambahkan ke pembacaan).";
      }
    } else if (p.perintah == "reset") {
      resetCalibration();
      phCalLastOk = true;
      phCalLastMsg = "Data kalibrasi tersimpan di perangkat dihapus.";
    } else {
      phCalLastMsg = "Perintah tidak dikenal: " + p.perintah;
    }
    phCalIdPerintahWeb = -1;
    sukses = phCalLastOk;
    pesan = phCalLastMsg;
    lepasModbus();
  }
  Serial.printf("[KALIBRASI PH] %s: %s\n", sukses ? "SUKSES" : "GAGAL",
                pesan.c_str());
  laporkanHasilKalibrasiPh(p.id, sukses, pesan.c_str());
}

// Eksekusi kalibrasi hardware TSS two-point (native di sensor, lihat Panduan
// Kalibrasi Sensor TSS) berdasarkan perintah step-by-step dari dashboard --
// menggantikan alur manual Modbus Poll + USB-RS485. Dipanggil tiap loop()
// (throttled). Tiap perintah baru langsung dilapor balik hasilnya ke
// Supabase supaya dashboard bisa update statusnya real-time.

void cekPerintahKalibrasiTssHwDariWeb() {
  if (millis() - timerCekKalibrasiHwTssTerakhir <
      INTERVAL_CEK_KALIBRASI_HW_TSS_MS)
    return;
  if (!internetSiap())
    return;
  timerCekKalibrasiHwTssTerakhir = millis();
  PerintahKalibrasiTssHw p =
      bacaPerintahKalibrasiTssHwDariSupabase(idTerakhirDiprosesKalibrasiHwTss);
  if (!p.ok)
    return;
  idTerakhirDiprosesKalibrasiHwTss = p.id;
  Serial.printf("\n[KALIBRASI HW TSS] Perintah baru #%ld: %s\n", p.id,
                p.perintah.c_str());
  bool sukses = false;
  String pesan;
  int16_t reg13 = -1, reg14 = -1;

  // Hanya perintah faktor sensor (alamat 4-5) & cek_status yang boleh jalan
  // selagi wizard two-point lama dimatikan (register 20-28 belum terbukti).
  const bool perintahFaktor = p.perintah == "baca_faktor" ||
                              p.perintah == "tulis_faktor" ||
                              p.perintah == "uji_faktor" ||
                              p.perintah == "baca_fc";
  if (!ENABLE_TSS_HW_REGISTER_CALIBRATION && !perintahFaktor &&
      p.perintah != "cek_status") {
    pesan = "Perintah '" + p.perintah +
            "' dinonaktifkan (register kalibrasi two-point belum terbukti).";
    Serial.printf("[KALIBRASI HW TSS] DITOLAK: %s\n", pesan.c_str());
    laporkanHasilKalibrasiTssHw(p.id, false, pesan.c_str(), reg13, reg14);
    return;
  }

  // Rata-rata TSS mentah: tunggu sensor menerapkan perubahan (response time
  // ~4 dtk), lalu 3 bacaan berjarak 1 dtk. -1 kalau semua gagal.
  auto tssStabil = []() -> float {
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

  if (p.perintah == "baca_fc") {
    // nilai_target = function code (1 coil, 2 discrete, 3 holding, 4 input),
    // nilai_aktual = alamat awal (bawaan 0). Hanya membaca.
    if (!kunciModbus(10000)) {
      pesan = "Bus Modbus sibuk, coba lagi.";
    } else {
      sukses = tssSensor.bacaFc((uint8_t)p.nilaiTarget,
                                p.adaNilaiAktual ? (uint16_t)p.nilaiAktual : 0,
                                pesan);
      lepasModbus();
    }
  } else if (perintahFaktor) {
    if (p.perintah != "baca_faktor" && modeOtomatis) {
      pesan = "Pindah ke mode MANUAL dulu (penulisan faktor menahan bus ~25 dtk).";
    } else if (!kunciModbus(10000)) {
      pesan = "Bus Modbus sibuk, coba lagi.";
    } else {
      float f0 = 0;
      if (!tssSensor.bacaFaktor(f0)) {
        pesan = "Gagal membaca register faktor (alamat " +
                String((unsigned)REG_CAL_FACTOR) + ").";
      } else if (p.perintah == "baca_faktor") {
        float t = tssSensor.readTSS();
        sukses = true;
        pesan = "faktor=" + String(f0, 4) + " tss_raw=" + String(t, 2);
      } else if (p.perintah == "tulis_faktor") {
        float tSebelum = tssSensor.readTSS();
        if (!tssSensor.tulisFactor(p.nilaiTarget)) {
          pesan = "Gagal menulis faktor (di luar 0.11-9.99 atau Modbus error).";
        } else {
          float f1 = 0;
          bool adaBalik = tssSensor.bacaFaktor(f1);
          float tSesudah = tssStabil();
          sukses = adaBalik && fabsf(f1 - p.nilaiTarget) < 0.001f;
          pesan = "faktor=" + (adaBalik ? String(f1, 4) : String("?")) +
                  " (sebelumnya " + String(f0, 4) +
                  ") tss_sebelum=" + String(tSebelum, 2) +
                  " tss_sesudah=" + String(tSesudah, 2) +
                  (sukses ? "" : " -- baca balik TIDAK sama dengan nilai tulis");
        }
      } else { // uji_faktor
        // Faktor uji = 2x faktor sekarang (atau setengahnya kalau melewati
        // batas 9.99). Bandingkan TSS; selalu dikembalikan ke faktor awal.
        float ft = (f0 * 2.0f <= TSS_FACTOR_MAX) ? f0 * 2.0f : f0 / 2.0f;
        float tAwal = tssStabil();
        bool tulisOk = tssSensor.tulisFactor(ft);
        float tUji = tulisOk ? tssStabil() : -1.0f;
        bool pulih = false;
        for (int i = 0; i < 3 && !pulih; i++) {
          float cek = 0;
          if (tssSensor.tulisFactor(f0) && tssSensor.bacaFaktor(cek) &&
              fabsf(cek - f0) < 0.001f)
            pulih = true;
          else
            delay(300);
        }
        float rasio = (tAwal > 0 && tUji > 0) ? tUji / tAwal : 0;
        float harapan = ft / f0;
        bool nempel = rasio > harapan * 0.8f && rasio < harapan * 1.2f;
        sukses = tulisOk && pulih;
        pesan = "uji alamat " + String((unsigned)REG_CAL_FACTOR) + ": faktor " +
                String(f0, 3) + "->" + String(ft, 3) + ", TSS " +
                String(tAwal, 2) + "->" + String(tUji, 2) + ", rasio=" +
                String(rasio, 2) + " (harapan " + String(harapan, 2) + ") " +
                (nempel ? "=> FAKTOR NEMPEL (alamat benar)"
                        : "=> TIDAK mempengaruhi TSS (bukan faktor)") +
                (pulih ? "; faktor dikembalikan ke " + String(f0, 4)
                       : "; PERINGATAN: gagal mengembalikan faktor!");
      }
      lepasModbus();
    }
  } else if (p.perintah == "cek_status") {
    bool ok13 = tssSensor.bacaStatusSensor(reg13);
    bool ok14 = tssSensor.bacaKelembabanProbe(reg14);
    float tssLive = tssSensor.readTSS();
    sukses = ok13 && ok14;
    pesan = "status=" + String(reg13) + " kelembaban=" + String(reg14) +
            " TSS_raw=" + String(tssLive, 2);
  } else if (p.perintah == "siapkan") {
    tssSensor.bacaKelembabanProbe(reg14);
    if (reg14 > 10) {
      pesan = "Kelembaban probe tinggi (reg14=" + String(reg14) +
              "), kemungkinan air masuk. Batal.";
    } else if (!tssSensor.keluarModeKalibrasi()) {
      pesan = "Gagal keluar mode kalibrasi (Modbus error).";
    } else {
      delay(200);
      tssSensor.bacaStatusSensor(reg13);
      if (reg13 != TSS_STATUS_NORMAL) {
        pesan = "Register 13 belum normal (=" + String(reg13) +
                ") setelah keluar mode kalibrasi.";
      } else if (!tssSensor.setModeKalibrasi(TSS_CAL_MODE_FACTOR)) {
        pesan = "Gagal set mode factor.";
      } else if (!tssSensor.tulisFactor(1.0f)) {
        pesan = "Gagal reset factor ke 1.0.";
      } else {
        sukses = true;
        pesan = "Siap kalibrasi. reg13=" + String(reg13) +
                ", reg14=" + String(reg14) + ", factor direset ke 1.0";
      }
    }
  } else if (p.perintah == "set_titik1" || p.perintah == "set_titik2") {
    uint16_t titik = (p.perintah == "set_titik1") ? 1 : 2;
    if (!tssSensor.setModeKalibrasi(TSS_CAL_MODE_TWO_POINT)) {
      pesan = "Gagal set mode two-point.";
    } else {
      delay(150);
      if (!tssSensor.setTitikKalibrasi(titik)) {
        pesan = "Gagal set titik kalibrasi " + String(titik);
      } else {
        sukses = true;
        pesan = "Mode two-point aktif, titik " + String(titik) +
                " dipilih. Silakan celup probe & tunggu stabil.";
      }
    }
  } else if (p.perintah == "tulis_titik1" || p.perintah == "tulis_titik2") {
    float nilaiAktual = p.adaNilaiAktual ? p.nilaiAktual : tssSensor.readTSS();
    if (!tssSensor.tulisTargetKalibrasi(p.nilaiTarget)) {
      pesan = "Gagal tulis target (P) kalibrasi.";
    } else {
      delay(150);
      if (!tssSensor.tulisAktualKalibrasi(nilaiAktual)) {
        pesan = "Gagal tulis actual (S) kalibrasi.";
      } else {
        sukses = true;
        pesan = "Tersimpan: P=" + String(p.nilaiTarget, 2) +
                " S=" + String(nilaiAktual, 2);
      }
    }
  } else if (p.perintah == "verifikasi_keluar") {
    if (!tssSensor.keluarModeKalibrasi()) {
      pesan = "Gagal keluar mode kalibrasi.";
    } else {
      delay(300);
      tssSensor.bacaStatusSensor(reg13);
      float tssLive = tssSensor.readTSS();
      sukses = (reg13 == TSS_STATUS_NORMAL);
      pesan = (sukses ? String("Kalibrasi selesai. ")
                      : String("Peringatan: reg13 belum normal. ")) +
              "reg13=" + String(reg13) + " TSS sekarang=" + String(tssLive, 2);
    }
  } else if (p.perintah == "reset_factor") {
    if (!tssSensor.setModeKalibrasi(TSS_CAL_MODE_FACTOR)) {
      pesan = "Gagal set mode factor.";
    } else if (!tssSensor.tulisFactor(1.0f)) {
      pesan = "Gagal reset factor.";
    } else {
      sukses = true;
      pesan = "Factor direset ke 1.0 (mode normal).";
    }
  } else {
    pesan = "Perintah tidak dikenal: " + p.perintah;
  }
  Serial.printf("[KALIBRASI HW TSS] %s: %s\n", sukses ? "SUKSES" : "GAGAL",
                pesan.c_str());
  laporkanHasilKalibrasiTssHw(p.id, sukses, pesan.c_str(), reg13, reg14);
}
