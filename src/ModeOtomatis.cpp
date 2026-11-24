// Lane "Mode Otomatis" (state_machine.workflow.json):
//   Baca Level -> Isi Chamber -> Tunggu Tenang -> Ukur pH & TSS -> Validasi?
//   -> Kirim Data -> (lane Kuras, ModeKuras.cpp)
#include "Kolam.h"
#include "Modes.h"
#include "Sensor.h"
#include "TSS_Sensor.h"

extern TSS_Sensor tssSensor;

// 1 = sampling tiap intervalSampelMs + trimmed mean trimPersen% (pH & TSS); 0 = cara lama: setelah
// tunggu stabil, pH dibaca sekali (median 5 sampel) dan TSS rata-rata 5 sampel.
#define PAKAI_TRIMMED_MEAN 1

// Sampling di dalam jendela baca (intervalBacaSensorMs): satu sampel TSS + satu
// sampel pH tiap intervalSampelMs (diatur dari web, bawaan 1 dtk) dan % trim
// dari trimPersen (bawaan 15). Jendela yang menghasilkan lebih dari MAKS_SAMPEL
// sampel hanya menyimpan sampel terbaru.
static const int MAKS_SAMPEL = 240;
static float bufSampelTss[MAKS_SAMPEL];
static float bufSampelPh[MAKS_SAMPEL];
static float bufSampelSuhu[MAKS_SAMPEL];
static float bufSampelMv[MAKS_SAMPEL];
static int jumlahSampelTss = 0;
static int jumlahSampelPh = 0;
static int galatBacaPh = 0;
static unsigned long timerBacaMulai = 0;
static unsigned long timerSampelTerakhir = 0;

void handleModeOtomatis() {
  switch (state) {
  case STATE_TUNGGU_AIR: {
    float jarak = bacaJarakAir();
    // Misal logika: kalau jarak ke air < AMBANG_TINGGI_AIR_CM berarti air cukup
    // tinggi
    if (jarak > 0 && jarak < AMBANG_TINGGI_AIR_CM) {
      Serial.println("Air terdeteksi mencapai ambang batas.");
      Serial.println(">>> Transisi ke TUNGGU_FLOAT_SWITCH");
      state = STATE_TUNGGU_FLOAT_SWITCH;
    }
  } break;

  case STATE_AUTO_BACA_LEVEL: {
    // Pemicu = JADWAL dari web (jeda antar siklus), BUKAN sensor level air.
    // Siklus pertama langsung jalan begitu mode otomatis aktif atau jadwal
    // baru disimpan; berikutnya setelah jedaSiklusMs sejak siklus sebelumnya
    // SELESAI (waktuSiklusMulai di-set saat valve menutup, ModeKuras.cpp).
    bool waktunya =
        siklusPertama || (millis() - waktuSiklusMulai >= jedaSiklusMs);
    if (!waktunya) {
      // Belum waktunya: putar lagi lewat cek stop/mode (supaya perintah
      // berhenti & perpindahan mode dari web tetap terbaca), tapi jangan
      // spam HTTP -- tunggu 2 detik antar putaran.
      static unsigned long timerTunggu = 0;
      if (millis() - timerTunggu < 2000)
        break;
      timerTunggu = millis();
      state = STATE_A_CEK_STOP;
      break;
    }
    // INTERLOCK valve-pompa: pompa TIDAK boleh nyala sebelum valve pasti
    // tertutup. Awal tiap siklus valve diperintah TUTUP (posisi fisik setelah
    // boot / mode manual tidak diketahui), lalu tunggu motor sampai ujung
    // (VALVE_TRAVEL_MS) baru pompa menyala.
    static bool valveSudahDiperintahTutup = false;
    static unsigned long valveTutupSelesai = 0;
    // [BARU] Gerbang air kolam: air kurang dari batas (Config.h KOLAM_MIN_AIR_CM) = siklus ditolak,
    // pompa tidak dinyalakan; dicek lagi tiap 5 detik lewat putaran cek stop/mode. Hanya berlaku
    // sebelum siklus dimulai (setelah valve diperintah tutup, siklus dilanjutkan seperti biasa).
    if (!valveSudahDiperintahTutup && !kolamBolehSiklus()) {
      static unsigned long timerTolak = 0;
      if (millis() - timerTolak < 5000)
        break;
      timerTolak = millis();
      state = STATE_A_CEK_STOP;
      break;
    }
    if (!valveSudahDiperintahTutup) {
      pompaOff(); // pastikan pompa mati selagi valve belum tertutup
      if (!valveTutup())
        break; // masih debounce -- coba lagi iterasi berikutnya
      Serial.println(F("[SIKLUS] Valve diperintah TUTUP dulu, pompa menunggu "
                       "valve tertutup penuh."));
      valveSudahDiperintahTutup = true;
      valveTutupSelesai = millis() + VALVE_TRAVEL_MS;
      break;
    }
    if ((long)(millis() - valveTutupSelesai) < 0)
      break; // motor valve masih menutup
    valveSudahDiperintahTutup = false;

    siklusPertama = false;
    Serial.printf("\n[JADWAL] Mulai siklus otomatis (jeda %lu dtk, pompa maks "
                  "%lu dtk, valve maks %lu dtk)\n",
                  jedaSiklusMs / 1000UL, pompaMaksMs / 1000UL,
                  valveMaksMs / 1000UL);
    pompaOn();
    timerIsiChamberMulai = millis();
    state = STATE_AUTO_ISI_CHAMBER;
    break;
  }

  case STATE_AUTO_ISI_CHAMBER: {
    // Debounce: HIGH harus stabil FLOAT_DEBOUNCE_MS (riak air jangan dihitung penuh).
    // Pompa sendiri tetap dikunci OFF seketika oleh pumpON().
    static unsigned long floatHighSejak = 0;
    if (!bacaFloatSwitch())
      floatHighSejak = 0;
    else if (floatHighSejak == 0)
      floatHighSejak = millis();
    bool floatHigh =
        floatHighSejak != 0 && millis() - floatHighSejak >= FLOAT_DEBOUNCE_MS;
    bool timeout = (millis() - timerIsiChamberMulai) >= pompaMaksMs;

    if (floatHigh || timeout)
      floatHighSejak = 0; // reset untuk siklus berikutnya
    if (floatHigh) {
      pompaOff();
      timerStabilisasiMulai = millis();
      state = STATE_AUTO_STABILISASI;
    } else if (timeout) {
      // Waktu pompa dari jadwal habis = akhir fase isi yang normal (bukan
      // error); lanjut ke stabilisasi dan pengukuran.
      pompaOff();
      Serial.println(F("[JADWAL] Waktu pompa habis sebelum float switch HIGH "
                       "-> lanjut stabilisasi"));
      timerStabilisasiMulai = millis();
      state = STATE_AUTO_STABILISASI;
    }
    break;
  }

  case STATE_AUTO_STABILISASI:
    if (millis() - timerStabilisasiMulai >= stabilMs) {
      timerBacaMulai = millis(); // jendela sampling (intervalBacaSensorMs)
      timerSampelTerakhir = 0;
      jumlahSampelTss = 0;
      jumlahSampelPh = 0;
      galatBacaPh = 0;
      jumlahUlangBaca = 0;
      state = STATE_AUTO_BACA_KUALITAS;
    }
    break;

  case STATE_AUTO_BACA_KUALITAS: {
#if !PAKAI_TRIMMED_MEAN
    // Cara lama. Seluruh rantai pH (baca register -> skala -> validitas ->
    // stabilitas -> median -> bulatkan) ada di bacaProsesPH(). Bus Modbus
    // dipakai juga oleh taskSerial -> pegang mutex; kalau sibuk coba lagi di
    // iterasi berikutnya.
    if (!kunciModbus(30000))
      break;
    hasilPhTerakhir = bacaProsesPH();
    tssTerakhir = bacaRataRataTSS(5, &tssRawTerakhir);
    lepasModbus();
    Serial.printf("[BACA] pH=%s TSS=%.2f (raw %.2f)\n",
                  hasilPhTerakhir.valid ? String(hasilPhTerakhir.ph, 2).c_str()
                                        : "n/a",
                  tssTerakhir, tssRawTerakhir);
    state = STATE_AUTO_VALIDASI;
    break;
#else
    // Sampling TSS DAN pH mentah tiap intervalSampelMs selama jendela
    // (mis. 45 dtk x 1 dtk = 45 sampel tiap sensor). Sesudah jendela habis:
    // trimmed mean trimPersen% (mis. 15% = buang 6 terendah & 6 tertinggi, rata-rata 33 di
    // tengah) -> TSS dikalibrasi -> itulah nilai yang dikirim. Valve baru buka
    // sesudah ini.
    if (timerSampelTerakhir != 0 &&
        millis() - timerSampelTerakhir < intervalSampelMs)
      break;
    // Bus Modbus dipakai juga oleh taskSerial -> pegang mutex. Kalau sibuk,
    // coba lagi di iterasi berikutnya (state tidak berubah).
    if (!kunciModbus(30000))
      break;
    timerSampelTerakhir = millis();
    // pH dulu (bus sudah hening ~500 ms), lalu TSS. Jeda antar-frame RTU
    // (>3,5 karakter) supaya slave kedua tidak menganggap permintaannya
    // lanjutan frame sebelumnya. Keduanya satu putaran = "barengan".
    double ph = 0, suhu = 0, mvSampel = NAN;
    bool phOk = readModbusSensor(ph, suhu, nullptr, nullptr,
                                 &mvSampel); // 1 transaksi + gerbang validitas
    delay(30);
    float tssMentah = tssSensor.readTSS();
    bool selesai = (millis() - timerBacaMulai) >= intervalBacaSensorMs;
    float suhuTss = NAN, ntuSampel = NAN;
    if (selesai && !tssSensor.readSuhu(suhuTss))
      suhuTss = NAN;
    if (selesai && !tssSensor.readNtu(ntuSampel))
      ntuSampel = NAN;
    lepasModbus();

    // Jendela panjang: simpan sampel TERBARU saja (buffer melingkar).
    if (tssMentah >= 0.0f) {
      bufSampelTss[jumlahSampelTss % MAKS_SAMPEL] = tssMentah;
      jumlahSampelTss++;
    }
    if (phOk) {
      bufSampelPh[jumlahSampelPh % MAKS_SAMPEL] = (float)ph;
      bufSampelSuhu[jumlahSampelPh % MAKS_SAMPEL] = (float)suhu;
      bufSampelMv[jumlahSampelPh % MAKS_SAMPEL] = (float)mvSampel;
      jumlahSampelPh++;
    } else {
      galatBacaPh++;
    }
    if (!selesai)
      break;

    int nTss = min(jumlahSampelTss, MAKS_SAMPEL);
    float raw = 0.0f;
    float tss = trimmedMeanTSS(bufSampelTss, nTss, trimPersen / 100.0f, &raw);
    suhuTssTerakhir = suhuTss;
    ntuTerakhir = ntuSampel;

    int nPh = min(jumlahSampelPh, MAKS_SAMPEL);
    HasilPH h = {};
    h.valid = nPh > 0;
    h.kualitas = h.valid ? "good" : "error";
    h.modbusErrorCount = galatBacaPh;
    if (h.valid) {
      h.ph = roundf(trimmedMean(bufSampelPh, nPh, trimPersen / 100.0f) * 100.0f) / 100.0f;
      h.suhu = roundf(trimmedMean(bufSampelSuhu, nPh, trimPersen / 100.0f) * 10.0f) / 10.0f;
      h.compliant = h.ph >= BAKU_MUTU_PH_MIN && h.ph <= BAKU_MUTU_PH_MAX;
      h.mv = roundf(trimmedMean(bufSampelMv, nPh, trimPersen / 100.0f) * 10.0f) / 10.0f;
    }
    hasilPhTerakhir = h;
    Serial.printf("[BACA] TSS %d sampel -> %.2f mg/L (raw %.2f) | pH %d sampel "
                  "(gagal baca %d) -> %s\n",
                  nTss, tss, raw, nPh, galatBacaPh,
                  h.valid ? String(h.ph, 2).c_str() : "n/a");
    state = STATE_AUTO_VALIDASI;
    break;
#endif
  }

  case STATE_AUTO_VALIDASI:
    if (hasilPhTerakhir.valid) {
      state = STATE_AUTO_KIRIM_DATA;
#if !PAKAI_TRIMMED_MEAN
    } else if (++jumlahUlangBaca < MAKS_ULANG_BACA) {
      state = STATE_AUTO_BACA_KUALITAS; // ulangi seluruh proses baca
#endif
    } else {
      // Tidak ada satu pun sampel pH valid sepanjang jendela (mis. sensor pH
      // belum terpasang) -- TSS yang sudah terolah TIDAK boleh ikut dibuang.
      // Tetap lanjut ke KIRIM_DATA; kirimDataKeAPI() akan mengirim TSS+level
      // seperti biasa dan ph/water_temperature sebagai null.
      tampilkanError("Tidak ada sampel pH valid sepanjang jendela baca -- "
                     "lanjut kirim TSS saja");
      kirimStatusErrorKeAPI(hasilPhTerakhir.modbusErrorCount);
      state = STATE_AUTO_KIRIM_DATA;
    }
    break;

  case STATE_AUTO_KIRIM_DATA: {
    // Urutan: data stabil dikirim DULU, baru valve dibuka. Pompa dipastikan
    // mati sebelum valve bergerak. valveBuka() bisa menolak (debounce) --
    // diulang tiap iterasi sampai benar-benar jalan, jangan lanjut kalau
    // belum.
    static bool dataSudahDikirim = false;
    pompaOff();
    if (!dataSudahDikirim) {
      // Supabase lewat WiFi atau data seluler (GSM). Gagal -> buffer (kirim ulang nanti).
      bool terkirim = kirimDataKeAPI(hasilPhTerakhir, tssTerakhir);
      if (!terkirim) {
        simpanKeBuffer(hasilPhTerakhir, tssTerakhir);
      }
      dataSudahDikirim = true;
    }
    if (valveBuka()) {
      dataSudahDikirim = false;
      timerKurasMulai = millis(); // batas waktu valve buang mulai dihitung
      state = STATE_AUTO_TUNGGU_KOSONG;
    }
    break;
  }

  default:
    // State lainnya bukan milik otomatis
    break;
  }
}
