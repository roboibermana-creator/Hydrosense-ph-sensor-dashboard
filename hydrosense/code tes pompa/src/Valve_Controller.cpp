#include "../include/Valve_Controller.h"
#include "../include/config.h"

void Valve_Controller::relayWrite(uint8_t pin, bool on) {
#if RELAY_ACTIVE_LOW
  digitalWrite(pin, on ? LOW : HIGH);
#else
  digitalWrite(pin, on ? HIGH : LOW);
#endif
}

void Valve_Controller::begin() {
  const uint8_t pins[] = { PUMP_PIN, VALVE1_PIN, VALVE2_OPEN_PIN, VALVE2_CLOSE_PIN };
  for (uint8_t p : pins) {
    pinMode(p, OUTPUT);
    relayWrite(p, false);                 // semua relay OFF saat boot
  }
  _pumpOn = _valve1Open = _valve2Open = false;
  Serial.println("✓ Valve Controller ready (pump, valve1, valve2 all OFF)");
}

// --- Pompa -----------------------------------------------------------
void Valve_Controller::pumpOn()  { relayWrite(PUMP_PIN, true);  _pumpOn = true;  Serial.println("  [PUMP]   ON"); }
void Valve_Controller::pumpOff() { relayWrite(PUMP_PIN, false); _pumpOn = false; Serial.println("  [PUMP]   OFF"); }

// --- Valve 1 (drain) -------------------------------------------------
void Valve_Controller::openValve1()  { relayWrite(VALVE1_PIN, true);  _valve1Open = true;  Serial.println("  [VALVE1] OPEN"); }
void Valve_Controller::closeValve1() { relayWrite(VALVE1_PIN, false); _valve1Open = false; Serial.println("  [VALVE1] CLOSED"); }

// --- Valve 2 (motorized, dua relay: buka & tutup) --------------------
// Kedua relay tidak boleh ON bersamaan; relay lawan selalu dimatikan dulu.
void Valve_Controller::openValve2() {
  relayWrite(VALVE2_CLOSE_PIN, false);
  relayWrite(VALVE2_OPEN_PIN, true);
  _valve2Open = true;
  Serial.println("  [VALVE2] OPENING");
}

void Valve_Controller::closeValve2() {
  relayWrite(VALVE2_OPEN_PIN, false);
  relayWrite(VALVE2_CLOSE_PIN, true);
  _valve2Open = false;
  Serial.println("  [VALVE2] CLOSING");
}

unsigned long Valve_Controller::doseDurationMs(int percent) const {
  return VALVE2_START_DELAY_MS +
         (unsigned long)((VALVE2_FULL_OPEN_TIME_MS - VALVE2_START_DELAY_MS) * (percent / 100.0));
}

// Urutan sama dengan main v3.1: buka selama durasi, lalu tutup selama durasi
// yang sama supaya motor kembali ke posisi tertutup, kemudian relay dilepas.
void Valve_Controller::doseValve2(int percent) {
  if (percent <= 0 || percent > 100) { Serial.println("✗ Invalid! Use 1-100"); return; }
  unsigned long duration = doseDurationMs(percent);
  Serial.print("\n>>> Dosing "); Serial.print(percent); Serial.print("% -> ");
  Serial.print(duration / 1000.0, 1); Serial.println(" s");

  Serial.println("  Opening valve...");
  openValve2();
  delay(duration);

  Serial.print("  Closing valve (for "); Serial.print(duration / 1000.0, 1); Serial.println(" s)...");
  closeValve2();
  delay(duration);
  relayWrite(VALVE2_CLOSE_PIN, false);   // motor sudah di posisi tutup, lepas relay
  Serial.println("✓ Dosing complete.");
}

// --- Status ------------------------------------------------------------
void Valve_Controller::printStatus() {
  Serial.println("\n┌─ Valve / Pump Status ───────────────┐");
  Serial.print  ("│ Pump    : "); Serial.println(_pumpOn     ? "ON                        │" : "OFF                       │");
  Serial.print  ("│ Valve 1 : "); Serial.println(_valve1Open ? "OPEN  (drain)             │" : "CLOSED                    │");
  Serial.print  ("│ Valve 2 : "); Serial.println(_valve2Open ? "OPEN  (dosing)            │" : "CLOSED                    │");
  Serial.println("└─────────────────────────────────────┘");
}

void Valve_Controller::printCompactStatus() {
  Serial.print("Pump: ");    Serial.print(_pumpOn     ? "ON"   : "OFF");
  Serial.print(" | V1: ");   Serial.print(_valve1Open ? "OPEN" : "CLOSED");
  Serial.print(" | V2: ");   Serial.println(_valve2Open ? "OPEN" : "CLOSED");
}
