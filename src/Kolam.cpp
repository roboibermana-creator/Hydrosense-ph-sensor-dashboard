// Gerbang "air kolam kering" untuk jadwal otomatis (permintaan: siklus hanya jalan bila air kolam
// cukup, mis. >= 20 cm; di bawah itu siklus ditolak dan dashboard menampilkan "air kolam kering").
// Ini tambahan murni: dipanggil dari awal STATE_AUTO_BACA_LEVEL, logika siklus lama tidak berubah.
#include "Kolam.h"
#include "Config.h"
#include "Level_Sensor.h"

static bool ditolak = false;
static const char *teks = "";
static bool hasilTerakhir = true;
static unsigned long cekTerakhirMs = 0;
static unsigned long logTerakhirMs = 0;

#include <Preferences.h>

// Batas minimal & tinggi maksimum runtime: default dari Config.h, bisa ditimpa lewat Serial 'J'
// atau dashboard (tersimpan di NVS, tanpa upload ulang). Tinggi maksimum = jarak sensor ke dasar
// kolam (dulu konstanta TANK_HEIGHT_CM).
static float minAirCm = KOLAM_MIN_AIR_CM;
static float maxAirCm = TANK_HEIGHT_CM;
static bool batasDimuat = false;

static void muatBatas() {
  if (batasDimuat)
    return;
  batasDimuat = true;
  Preferences p;
  if (p.begin("kolam", true)) {
    minAirCm = p.getFloat("minair", KOLAM_MIN_AIR_CM);
    maxAirCm = p.getFloat("maxair", TANK_HEIGHT_CM);
    p.end();
  }
  // Jaga-jaga data NVS tidak konsisten.
  if (!(maxAirCm >= 10.0f && maxAirCm <= RADAR_RANGE_CM))
    maxAirCm = TANK_HEIGHT_CM;
  if (!(minAirCm >= 0.0f && minAirCm <= maxAirCm))
    minAirCm = min((float)KOLAM_MIN_AIR_CM, maxAirCm);
}

float kolamMinAirCm() {
  muatBatas();
  return minAirCm;
}

float kolamMaxAirCm() {
  muatBatas();
  return maxAirCm;
}

static bool simpanBatas(const char *kunci, float nilai) {
  Preferences p;
  if (!p.begin("kolam", false))
    return false;
  p.putFloat(kunci, nilai);
  p.end();
  cekTerakhirMs = 0; // berlaku langsung di pengecekan berikutnya
  return true;
}

bool kolamSetMinAirCm(float cm) {
  muatBatas();
  if (!(cm >= 0.0f && cm <= maxAirCm))
    return false;
  if (!simpanBatas("minair", cm))
    return false;
  minAirCm = cm;
  return true;
}

bool kolamSetMaxAirCm(float cm) {
  muatBatas();
  if (!(cm >= 10.0f && cm <= RADAR_RANGE_CM && cm >= minAirCm))
    return false;
  if (!simpanBatas("maxair", cm))
    return false;
  maxAirCm = cm;
  return true;
}

bool kolamDitolak() { return ditolak; }
const char *kolamStatusTeks() { return teks; }

bool kolamBolehSiklus() {
#if !KOLAM_CEK_AKTIF
  return true;
#else
  // Dipanggil tiap putaran loop: ukur paling sering tiap 3 detik, selebihnya pakai hasil terakhir.
  if (cekTerakhirMs != 0 && millis() - cekTerakhirMs < 3000UL)
    return hasilTerakhir;
  cekTerakhirMs = millis();

  float level = 0, jarak = 0, arus = 0;
  readTankSensor(level, jarak, arus);
  float tinggiCm = level / 100.0f * kolamMaxAirCm();
  const float batasCm = kolamMinAirCm();

  bool ok = true;
  const char *alasan = "";
  if (!levelSensorValid()) {
    ok = false; // sensor rusak jangan dianggap air penuh
    alasan = "sensor level kolam tidak terbaca - siklus ditolak";
  } else if (tinggiCm < batasCm) {
    ok = false;
    alasan = "air kolam kering - siklus ditolak";
  }

  bool berubah = (ok != hasilTerakhir) || (!ok && strcmp(alasan, teks) != 0);
  if (berubah || (!ok && millis() - logTerakhirMs > 60000UL)) {
    logTerakhirMs = millis();
    if (ok)
      Serial.printf("[KOLAM] Air cukup (%.1f cm >= %.1f cm) -> siklus otomatis boleh jalan\n",
                    tinggiCm, batasCm);
    else
      Serial.printf("[KOLAM] %s (tinggi air %.1f cm, batas %.1f cm, arus %.2f mA)\n", alasan,
                    tinggiCm, batasCm, arus);
  }
  hasilTerakhir = ok;
  ditolak = !ok;
  teks = ok ? "" : alasan;
  return ok;
#endif
}
