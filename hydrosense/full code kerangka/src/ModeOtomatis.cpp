#include "Modes.h"

void handleModeOtomatis() {
  switch (state) {
    case STATE_TUNGGU_AIR:
      {
        float jarak = bacaJarakAir();
        // Misal logika: kalau jarak ke air < AMBANG_TINGGI_AIR_CM berarti air cukup tinggi
        if (jarak > 0 && jarak < AMBANG_TINGGI_AIR_CM) {
          Serial.println("Air terdeteksi mencapai ambang batas.");
          Serial.println(">>> Transisi ke TUNGGU_FLOAT_SWITCH");
          state = STATE_TUNGGU_FLOAT_SWITCH;
        }
      }
      break;

    case STATE_AUTO_BACA_LEVEL: {
      float jarak = bacaJarakAir();
      float tinggiAir = TINGGI_PASANG_CM - jarak;

      if (tinggiAir >= AMBANG_TINGGI_AIR_CM) {
        pompaOn();
        timerIsiChamberMulai = millis();
        state = STATE_AUTO_ISI_CHAMBER;
      } else {
        state = STATE_A_CEK_STOP;
      }
      break;
    }

    case STATE_AUTO_ISI_CHAMBER: {
      bool floatHigh = bacaFloatSwitch();
      bool timeout = (millis() - timerIsiChamberMulai) >= TIMEOUT_ISI_MS;

      if (floatHigh) {
        pompaOff();
        timerStabilisasiMulai = millis();
        state = STATE_AUTO_STABILISASI;
      } else if (timeout) {
        pompaOff();
        tampilkanError("Selang tersumbat / pompa rusak");
        state = STATE_A_CEK_STOP;
      }
      break;
    }

    case STATE_AUTO_STABILISASI:
      if (millis() - timerStabilisasiMulai >= DELAY_STABIL_MS) {
        jumlahUlangBaca = 0;
        state = STATE_AUTO_BACA_KUALITAS;
      }
      break;

    case STATE_AUTO_BACA_KUALITAS:
      // Seluruh rantai (baca register -> skala -> validitas -> stabilitas ->
      // median -> bulatkan) terjadi di dalam bacaProsesPH(), bukan raw value.
      hasilPhTerakhir = bacaProsesPH();
      // MEDIAN_SAMPLES = 5 biasanya, kita gunakan define atau hardcode 5 di sini karena cuma lewat
      tssTerakhir     = bacaRataRataTSS(5);
      state = STATE_AUTO_VALIDASI;
      break;

    case STATE_AUTO_VALIDASI:
      if (hasilPhTerakhir.valid) {
        state = STATE_AUTO_KIRIM_DATA;
      } else {
        jumlahUlangBaca++;
        if (jumlahUlangBaca < MAKS_ULANG_BACA) {
          state = STATE_AUTO_BACA_KUALITAS; // ulangi seluruh proses baca
        } else {
          tampilkanError("Data pH tidak valid setelah 3x percobaan");
          kirimStatusErrorKeAPI(hasilPhTerakhir.modbusErrorCount);
          state = STATE_A_CEK_STOP;
        }
      }
      break;

    case STATE_AUTO_KIRIM_DATA: {
      valveBuka();
      bool terkirim = kirimDataKeAPI(hasilPhTerakhir, tssTerakhir);
      if (!terkirim) {
        simpanKeBuffer(hasilPhTerakhir, tssTerakhir);
      }
      state = STATE_AUTO_TUNGGU_KOSONG;
      break;
    }

    case STATE_AUTO_TUNGGU_KOSONG:
      if (!bacaFloatSwitch()) {
        valveTutup();
        state = STATE_A_CEK_STOP;
      }
      break;

    default:
      // State lainnya bukan milik otomatis
      break;
  }
}

