/* =====================================================================
 * HydroSense - Sistem Kontrol & Monitoring Kualitas Air Settling Pond
 * Kerangka kode ESP32 berdasarkan flowchart "cara_kerja" + rantai
 * pengolahan data pH sensor RS485 Modbus
 * ===================================================================== */

#include "Network.h"
#include <math.h>
#include "Config.h"
#include "Sensor.h"
#include "Valve_Controller.h"
#include "TSS_Sensor.h"
#include "Level_Sensor.h"
#include "Pompa.h"
#include "Modes.h"

// Valve Controller (Motorized Valve) menggunakan relay
Valve_Controller valveController;

// TSS Sensor Modbus
TSS_Sensor tssSensor;

// ========================= VARIABEL GLOBAL RUNTIME =========================
SystemState state = STATE_INIT;
unsigned long timerIsiChamberMulai = 0;
unsigned long timerStabilisasiMulai = 0;
unsigned long timerPollTerakhir = 0;
unsigned long timerRetryWifiTerakhir = 0;

int   jumlahUlangBaca = 0;
HasilPH hasilPhTerakhir;
float   tssTerakhir = 0.0;
bool    modeOtomatis = true;

const float TINGGI_PASANG_CM          = 100.0;
const float AMBANG_TINGGI_AIR_CM      = 20.0;
const unsigned long TIMEOUT_ISI_MS    = 5UL * 60UL * 1000UL;
const unsigned long DELAY_STABIL_MS   = 10UL * 1000UL;
const int     MAKS_ULANG_BACA         = 3;
const unsigned long INTERVAL_POLL_MS  = 5000;
const unsigned long INTERVAL_RETRY_WIFI_MS = 3000;

// ---------------- Aktuator ----------------
void pompaOn()  { pumpON(); }
void pompaOff() { pumpOFF(); }
void valveBuka()  { valveController.openValve1(); }
void valveTutup() { valveController.closeValve1(); }

// ========================= DEKLARASI FUNGSI =========================
void kalibrasiAwal();
void connectWiFi();
bool wifiTersambung();
bool supabaseSiap();
void tampilkanError(const char* pesan);
bool permintaanBerhenti();
float bacaJarakAir();
bool bacaFloatSwitch();
float bacaRataRataTSS(int nSample);
HasilPH bacaProsesPH();
bool kirimDataKeAPI(HasilPH hasilPh, float tss);
bool kirimStatusErrorKeAPI(int errorCount);
void simpanKeBuffer(HasilPH hasilPh, float tss);
void cobaKirimUlangBuffer();
bool bacaModeOperasiDariSupabase();

// ========================= SETUP =========================
void setup() {
  Serial.begin(115200);
  delay(3000); // Tunggu Serial Monitor siap
  Serial.println("\n[SISTEM] Booting Hydrosense...");

  setupSensor(); // Inisialisasi Modbus pH sensor dari Sensor.cpp

  pinMode(PIN_TRIG_ULTRASONIK, OUTPUT);
  pinMode(PIN_ECHO_ULTRASONIK, INPUT);
  pinMode(PIN_FLOAT_SWITCH, INPUT_PULLUP);
  pinMode(PIN_RELAY_POMPA, OUTPUT);
  pinMode(PIN_LED_ERROR, OUTPUT);

  valveController.begin();
  tssSensor.begin();
  sensorBegin(); // Inisialisasi ADC Level Sensor
  pumpBegin();   // Inisialisasi Pompa (sudah otomatis dimatikan saat startup)
  digitalWrite(PIN_LED_ERROR, LOW);

  for (int i = 0; i < UKURAN_BUFFER; i++) buffer[i].terisi = false;

  Serial.println("\n=== Hydrosense Inlet System ===");
  Serial.println("COMMAND KALIBRASI PH SENSOR:");
  Serial.println(" 4 = Kalibrasi titik 1 (pH 4.01)");
  Serial.println(" 1 = Kalibrasi titik 2 (pH 9.01)");
  Serial.println(" A = Verifikasi pH 4.01");
  Serial.println(" 7 = Verifikasi pH 7.01");
  Serial.println(" C = Verifikasi pH 9.01");
  Serial.println(" S = Tampilkan Status Sensor & Sistem");
  Serial.println(" T = Baca Status & Data TSS Sensor");
  Serial.println(" W = Jalankan Wiper/Scraping TSS");
  Serial.println();
  
  state = STATE_INIT;
}

// ========================= LOOP UTAMA =========================
void loop() {
  // 1. Cek perintah kalibrasi dari Serial Monitor
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
    else if (command == 'T' || command == 't')  tssSensor.readStatus();
    else if (command == 'W' || command == 'w')  tssSensor.startScraping();
    else if (command == 'H' || command == 'h')  showHealthReport();
    else if (command == 'D' || command == 'd')  dumpModbusRegisters();
  }

  // 2. Jalankan State Machine
  switch (state) {
    case STATE_INIT:
      kalibrasiAwal();
      connectWiFi();
      state = STATE_CEK_KONEKSI;
      break;

    case STATE_CEK_KONEKSI:
      if (wifiTersambung() && supabaseSiap()) {
        digitalWrite(PIN_LED_ERROR, LOW);
        state = STATE_A_CEK_STOP;
      } else {
        tampilkanError("WiFi/Supabase belum siap");
        if (millis() - timerRetryWifiTerakhir >= INTERVAL_RETRY_WIFI_MS) {
          timerRetryWifiTerakhir = millis();
          connectWiFi();
        }
      }
      break;

    case STATE_A_CEK_STOP:
      state = permintaanBerhenti() ? STATE_STOPPED : STATE_A_BACA_MODE;
      break;

    case STATE_A_BACA_MODE:
      modeOtomatis = bacaModeOperasiDariSupabase();
      state = modeOtomatis ? STATE_AUTO_BACA_LEVEL : STATE_M_BACA_PERINTAH;
      break;

    case STATE_STOPPED:
      pompaOff();
      valveTutup();
      break;

    default:
      if (modeOtomatis) {
        handleModeOtomatis();
      } else {
        handleModeManual();
      }
      break;
  }

  cobaKirimUlangBuffer();
}

// ===================================================================
//                      FUNGSI PENDUKUNG UMUM
// ===================================================================

void kalibrasiAwal() {
  // Kalibrasi awal jika perlu
}

void tampilkanError(const char* pesan) {
  Serial.print("[ERROR] ");
  Serial.println(pesan);
  digitalWrite(PIN_LED_ERROR, HIGH);
}

bool permintaanBerhenti() {
  // TODO: baca flag "stop" dari kolom terpisah di TABLE_MODE
  return false;
}

// ---------------- Sensor Level (Radar 4-20mA) ----------------
float bacaJarakAir() {
  float levelPercent = 0.0;
  float distanceCm = 0.0;
  float currentMa = 0.0;
  digitalWrite(PIN_TRIG_ULTRASONIK, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG_ULTRASONIK, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG_ULTRASONIK, LOW);
  
  readTankSensor(levelPercent, distanceCm, currentMa);
  long duration = pulseIn(PIN_ECHO_ULTRASONIK, HIGH, 30000); // 30ms timeout
  if (duration == 0) return 999.0; // Jika timeout, asumsikan jarak sangat jauh
  
  float distanceCm = duration * 0.0343 / 2.0;
  return distanceCm;
}

bool bacaFloatSwitch() { return digitalRead(PIN_FLOAT_SWITCH) == HIGH; }

// ---------------- TSS (Modbus) ----------------
float bacaRataRataTSS(int nSample) {
  float tss = tssSensor.readTSS();
  if (tss < 0) {
    return 0.0;
  float sum = 0.0;
  int count = 0;
  for (int i = 0; i < nSample; i++) {
    float val = tssSensor.readTSS();
    if (val >= 0) {
      sum += val;
      count++;
    }
    delay(100);
  }
  return tss;
  return (count > 0) ? (sum / count) : 0.0;
}

// ===================================================================
//        MODBUS RTU (baca sensor pH via Sensor.cpp)
// ===================================================================

HasilPH bacaProsesPH() {
  HasilPH hasil;
  PhReading phRead = readSensor();
  
  hasil.valid = phRead.valid;
  hasil.ph = phRead.ph;
  hasil.suhu = phRead.tempC;
  hasil.sd = phRead.sd;
  hasil.drift = phRead.drift;
  hasil.kualitas = String(phRead.quality);
  hasil.compliant = phRead.compliant;
  hasil.rawPh = phRead.rawPh;
  hasil.rawTemp = phRead.rawTemp;
  hasil.modbusErrorCount = phRead.modbusErrors;
  
  return hasil;
}
