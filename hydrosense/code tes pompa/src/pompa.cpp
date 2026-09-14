#include "pompa.h"

// ============================================================
// PIN
// ============================================================
const uint8_t PUMP_RELAY_PIN = 48;

// ============================================================
// MODE OPERASI & PARAMETER
// ============================================================
bool automaticMode = false;
const unsigned long PUMP_ON_TIME = 10000;
const unsigned long PUMP_OFF_TIME = 20000;

// ============================================================
// VARIABLE
// ============================================================
bool pumpState = false;
unsigned long previousMillis = 0;

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
void showStatus()
{
  Serial.println();
  Serial.println("------------- STATUS ------------");

  Serial.print("Mode       : ");
  if (automaticMode) Serial.println("AUTO");
  else Serial.println("MANUAL");

  Serial.print("Pump       : ");
  if (pumpState) Serial.println("ON");
  else Serial.println("OFF");

  Serial.print("GPIO       : ");
  Serial.println(PUMP_RELAY_PIN);

  Serial.println("---------------------------------");
}

// ============================================================
// MENU SERIAL
// ============================================================
void showMenu()
{
  Serial.println();
  Serial.println("========== HYDROSENSE PUMP ==========");
  Serial.println("1 = Pump ON");
  Serial.println("0 = Pump OFF");
  Serial.println("A = Automatic mode");
  Serial.println("M = Manual mode");
  Serial.println("S = Status");
  Serial.println("======================================");
}

