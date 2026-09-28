#include "Valve_Controller.h"

// Level ON/OFF untuk Valve 1 (Relay Shield -> High Trigger)
const int V1_ON = RELAY_HIGH_TRIGGER; // HIGH = ON
const int V1_OFF = LOW;               // LOW = OFF

// Level ON/OFF untuk Pompa (Relay Eksternal -> Low Trigger)
const int PUMP_ON = RELAY_LOW_TRIGGER; // LOW = ON
const int PUMP_OFF = HIGH;             // HIGH = OFF

// Motorized valve butuh waktu fisik buat jalan penuh (bukan instan kayak relay
// biasa). Kalau web di-klik cepat bolak-balik, tiap klik langsung ngirim
// command baru dan valve dipaksa balik arah di tengah jalan -- itu yang
// bikin "bingung" (posisi jadi gak jelas / motor kepaksa reverse berkali-kali).
// Debounce ini nolak command baru kalau belum MIN_INTERVAL_AKSI_MS sejak
// aksi terakhir, kasih waktu motor selesai jalan dulu.
static const unsigned long MIN_INTERVAL_AKSI_MS = 3000;

Valve_Controller::Valve_Controller() {
  valve1Status = false;
  waktuAksiTerakhir = 0;
}

void Valve_Controller::begin() {
  Serial.println("Init Valves...");
  pinMode(VALVE1_OPEN_PIN, OUTPUT);
  pinMode(VALVE1_CLOSE_PIN, OUTPUT);
  digitalWrite(VALVE1_OPEN_PIN, V1_OFF);
  digitalWrite(VALVE1_CLOSE_PIN, V1_OFF);

  Serial.println("✓ Actuators Ready");
}

bool Valve_Controller::openValve1() {
  if (millis() - waktuAksiTerakhir < MIN_INTERVAL_AKSI_MS) {
    Serial.println("[VALVE] Command 'buka' diabaikan -- motor masih jalan dari "
                   "aksi sebelumnya.");
    return false;
  }
  digitalWrite(VALVE1_CLOSE_PIN, V1_OFF);
  delay(200);
  digitalWrite(VALVE1_OPEN_PIN, V1_ON);
  valve1Status = true;
  waktuAksiTerakhir = millis();
  Serial.println("✓ Valve 1 OPENING");
  return true;
}

bool Valve_Controller::closeValve1() {
  if (millis() - waktuAksiTerakhir < MIN_INTERVAL_AKSI_MS) {
    Serial.println("[VALVE] Command 'tutup' diabaikan -- motor masih jalan "
                   "dari aksi sebelumnya.");
    return false;
  }
  digitalWrite(VALVE1_OPEN_PIN, V1_OFF);
  delay(200);
  digitalWrite(VALVE1_CLOSE_PIN, V1_ON);
  valve1Status = false;
  waktuAksiTerakhir = millis();
  Serial.println("✓ Valve 1 CLOSING");
  return true;
}

bool Valve_Controller::getValve1Status() { return valve1Status; }

void Valve_Controller::printCompactStatus() {
  Serial.print("V1(Drain): ");
  Serial.print(valve1Status ? "OPEN" : "CLOSED");
  Serial.println();
}

void Valve_Controller::printStatus() {
  Serial.println("--- Status Valve ---");
  Serial.print("Valve 1 (Drain) : ");
  Serial.println(valve1Status ? "TERBUKA" : "TERTUTUP");
  Serial.println("--------------------");
}
