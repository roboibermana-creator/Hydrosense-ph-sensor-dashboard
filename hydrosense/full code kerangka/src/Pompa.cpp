#include "Pompa.h"
#include "Config.h" // Untuk mengambil PUMP_PIN dari Config.h

// ============================================================
// PIN
// ============================================================
// Menggunakan PUMP_PIN (48) dari Config.h
const uint8_t PUMP_RELAY_PIN = PUMP_PIN;

// ============================================================
// MODE OPERASI & PARAMETER
// ============================================================
bool pumpAutoMode = false;
const unsigned long PUMP_ON_TIME = 10000;
const unsigned long PUMP_OFF_TIME = 20000;

// ============================================================
// VARIABLE
// ============================================================
bool pumpState = false;
unsigned long pumpPreviousMillis = 0;

// ============================================================
// FUNGSI INISIALISASI
// ============================================================
void pumpBegin()
{
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  // SAFETY: Pompa harus OFF saat startup
  digitalWrite(PUMP_RELAY_PIN, RELAY_OFF);
  pumpState = false;
  
  Serial.println("Pompa Peristaltik diinisialisasi (PIN " + String(PUMP_RELAY_PIN) + ")");
}

// ============================================================
// FUNGSI POMPA ON
// ============================================================
void pumpON()
{
  digitalWrite(PUMP_RELAY_PIN, RELAY_ON);
  pumpState = true;

  Serial.println();
  Serial.println("================================");
  Serial.println("POMPA : ON");
  Serial.println("Relay 1 : ACTIVE");
  Serial.println("================================");
}

// ============================================================
// FUNGSI POMPA OFF
// ============================================================
void pumpOFF()
{
  digitalWrite(PUMP_RELAY_PIN, RELAY_OFF);
  pumpState = false;

  Serial.println();
  Serial.println("================================");
  Serial.println("POMPA : OFF");
  Serial.println("Relay 1 : INACTIVE");
  Serial.println("================================");
}

// ============================================================
// STATUS POMPA
// ============================================================
void pumpShowStatus()
{
  Serial.println();
  Serial.println("------------- PUMP STATUS ------------");
  Serial.print("Mode       : ");
  if (pumpAutoMode) Serial.println("AUTO (10s ON / 20s OFF)");
  else Serial.println("MANUAL");

  Serial.print("Pump       : ");
  if (pumpState) Serial.println("ON");
  else Serial.println("OFF");

  Serial.print("GPIO       : ");
  Serial.println(PUMP_RELAY_PIN);
  Serial.println("--------------------------------------");
}

// ============================================================
// MENU SERIAL
// ============================================================
void pumpShowMenu()
{
  Serial.println();
  Serial.println("========== PUMP COMMANDS ==========");
  Serial.println("1 = Pump ON");
  Serial.println("0 = Pump OFF");
  Serial.println("P_A = Pump Auto mode (10s/20s)");
  Serial.println("P_M = Pump Manual mode");
  Serial.println("P_S = Pump Status");
  Serial.println("===================================");
}

// ============================================================
// LOOP MODE OTOMATIS (Opsional untuk dipanggil di main loop)
// ============================================================
void pumpUpdateAuto()
{
  if (pumpAutoMode)
  {
    unsigned long currentMillis = millis();

    // POMPA SEDANG OFF -> Setelah 20 detik -> ON
    if (!pumpState) {
      if (currentMillis - pumpPreviousMillis >= PUMP_OFF_TIME) {
        pumpON();
        pumpPreviousMillis = currentMillis;
      }
    }
    // POMPA SEDANG ON -> Setelah 10 detik -> OFF
    else {
      if (currentMillis - pumpPreviousMillis >= PUMP_ON_TIME) {
        pumpOFF();
        pumpPreviousMillis = currentMillis;
      }
    }
  }
}

