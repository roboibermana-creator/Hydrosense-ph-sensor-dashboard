#include "Pompa.h"
#include "Config.h" // Untuk mengambil PUMP_PIN dari Config.h

// ============================================================
// PIN
// ============================================================
// Menggunakan PUMP_PIN dari Config.h
const uint8_t PUMP_RELAY_PIN = PUMP_PIN;

// ============================================================
// VARIABLE
// ============================================================
bool pumpState = false;

// ============================================================
// FUNGSI INISIALISASI
// ============================================================
void pumpBegin() {
  // SAFETY: Pompa harus OFF saat startup. Set level OFF SEBELUM pin dijadikan
  // output supaya tidak ada glitch ON sesaat (penting untuk relay active LOW).
  // Dipanggil di baris pertama setup() (main.cpp).
  digitalWrite(PUMP_RELAY_PIN, RELAY_OFF);
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  digitalWrite(PUMP_RELAY_PIN, RELAY_OFF);
  pumpState = false;

  Serial.println("Pompa Peristaltik diinisialisasi (PIN " +
                 String(PUMP_RELAY_PIN) + ")");
}

// ============================================================
// FUNGSI POMPA ON
// ============================================================
void pumpON() {
  // KUNCI MUTLAK: float switch HIGH (chamber penuh) -> pompa TIDAK BOLEH ON,
  // apa pun modenya dan siapa pun yang memanggil (web, Serial, otomatis).
  // Kunci ini selalu aktif, tidak bergantung INTERLOCK_POMPA_MANUAL.
  // Tidak memakai tinggi air dari sensor level sama sekali.
  if (digitalRead(PIN_FLOAT_SWITCH) == HIGH) {
    digitalWrite(PUMP_RELAY_PIN, RELAY_OFF);
    pumpState = false;
    Serial.println("[POMPA] ON ditolak: float switch HIGH -> pompa dikunci OFF");
    return;
  }
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
void pumpOFF() {
  digitalWrite(PUMP_RELAY_PIN, RELAY_OFF);
  pumpState = false;

  Serial.println();
  Serial.println("================================");
  Serial.println("POMPA : OFF");
  Serial.println("Relay 1 : INACTIVE");
  Serial.println("================================");
}
