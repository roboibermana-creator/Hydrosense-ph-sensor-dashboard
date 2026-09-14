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

// ========================= FUNGSI SETUP INTERAKTIF =========================
void waitForSerialInput(char* buffer, int maxLen, unsigned long timeoutMs = 30000) {
  unsigned long startTime = millis();
  int idx = 0;
  buffer[0] = '\0';

  while (millis() - startTime < timeoutMs && idx < maxLen - 1) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        buffer[idx] = '\0';
        Serial.println();
        return;
      } else if (c >= 32 && c < 127) {
        buffer[idx++] = c;
        Serial.print(c);
      } else if (c == 8 && idx > 0) { // backspace
        idx--;
        Serial.print("\b \b");
      }
    }
    delay(10);
  }
  buffer[idx] = '\0';
  Serial.println();
}

void setupWiFiConfiguration() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   HYDROSENSE SETUP KONFIGURASI WIFI   ║");
  Serial.println("╚════════════════════════════════════════╝");

  char ssidBuffer[64] = {0};
  char passBuffer[64] = {0};

  Serial.print("\n📡 Masukkan WiFi SSID [current: ");
  Serial.print(WIFI_SSID);
  Serial.print("]: ");
  waitForSerialInput(ssidBuffer, sizeof(ssidBuffer), 15000);

  if (strlen(ssidBuffer) == 0) {
    Serial.println("✓ Menggunakan SSID sebelumnya");
  } else {
    Serial.print("⚠  Perhatian: setup WiFi baru memerlukan compile ulang\n");
  }

  Serial.print("\n🔐 Masukkan WiFi Password [*** hidden ***]: ");
  waitForSerialInput(passBuffer, sizeof(passBuffer), 15000);

  if (strlen(passBuffer) > 0) {
    Serial.println("✓ Password diterima (perlu compile ulang)");
  } else {
    Serial.println("✓ Menggunakan password sebelumnya");
  }
}

void setupDeviceConfiguration() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   HYDROSENSE SETUP KONFIGURASI DEVICE ║");
  Serial.println("╚════════════════════════════════════════╝");

  Serial.print("\n🏷️  Device ID saat ini: ");
  Serial.println(DEVICE_ID);
  Serial.println("   (Ubah di Config.h jika perlu)");

  Serial.print("\n📍 Pin Configuration:");
  Serial.print("\n   • Pump Relay: GPIO");
  Serial.println(PIN_RELAY_POMPA);
  Serial.print("   • Valve1 Open: GPIO");
  Serial.println(VALVE1_OPEN_PIN);
  Serial.print("   • Valve1 Close: GPIO");
  Serial.println(VALVE1_CLOSE_PIN);
  Serial.print("   • Float Switch: GPIO");
  Serial.println(PIN_FLOAT_SWITCH);
  Serial.print("   • Level Sensor (ADC): GPIO");
  Serial.println(ADC_PIN);

  Serial.println("\n📋 RS485 Modbus Configuration:");
  Serial.print("   • RX: GPIO");
  Serial.print(RS485_RX_PIN);
  Serial.print(" | TX: GPIO");
  Serial.print(RS485_TX_PIN);
  Serial.print(" | DE: GPIO");
  Serial.println(RS485_RE_DE_PIN);
  Serial.print("   • Baud Rate: ");
  Serial.println(RS485_BAUD);
}

void setupHardwareTest() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║      HYDROSENSE HARDWARE TEST         ║");
  Serial.println("╚════════════════════════════════════════╝");

  Serial.print("\n🧪 Testing actuators... ");
  Serial.print("Pump[");
  pumpON();
  delay(500);
  pumpOFF();
  Serial.print("✓] ");

  Serial.print("Valve[");
  valveController.openValve1();
  delay(500);
  valveController.closeValve1();
  Serial.print("✓]");
  Serial.println();

  Serial.println("✓ Actuators OK");
}

// ========================= SETUP =========================
void setup() {
  Serial.begin(115200);
  delay(3000); // Tunggu Serial Monitor siap

  // Tampilkan splash screen
  Serial.println("\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   HYDROSENSE IOT WATER QUALITY MONITOR║");
  Serial.println("║       Supabase Backend Integration    ║");
  Serial.println("║   Settling Pond Level 1 - Inlet       ║");
  Serial.println("╚════════════════════════════════════════╝");

  Serial.println("\n[SISTEM] Initializing hardware...");
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

  Serial.println("[SISTEM] Hardware initialized ✓");

  // Run setup sequence
  setupWiFiConfiguration();
  delay(1000);
  setupDeviceConfiguration();
  delay(1000);
  setupHardwareTest();
  delay(1000);

  // Ready message
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   SETUP COMPLETE - STARTING SYSTEM   ║");
  Serial.println("╚════════════════════════════════════════╝");

  Serial.println("\n📋 AVAILABLE COMMANDS:");
  Serial.println("CALIBRATION:");
  Serial.println("  4 = Calibrate pH 4.01 (acidic buffer)");
  Serial.println("  1 = Calibrate pH 9.01 (basic buffer)");
  Serial.println("  A = Verify pH 4.01");
  Serial.println("  7 = Verify pH 7.01");
  Serial.println("  C = Verify pH 9.01");
  Serial.println("MONITORING & STATUS:");
  Serial.println("  S = System status");
  Serial.println("  T = TSS sensor status");
  Serial.println("  H = Health report");
  Serial.println("  D = Dump Modbus registers");
  Serial.println("MAINTENANCE:");
  Serial.println("  W = TSS wiper/scraping");
  Serial.println("  O = Write deviation");
  Serial.println("  R = Reset calibration");
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
