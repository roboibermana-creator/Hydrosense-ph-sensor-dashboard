#include "Valve_Controller.h"

// Level ON/OFF untuk Valve 1 (Relay Shield -> High Trigger)
const int V1_ON = RELAY_HIGH_TRIGGER; // HIGH = ON
const int V1_OFF = LOW;               // LOW = OFF

// Level ON/OFF untuk Pompa (Relay Eksternal -> Low Trigger)
const int PUMP_ON = RELAY_LOW_TRIGGER; // LOW = ON
const int PUMP_OFF = HIGH;              // HIGH = OFF

Valve_Controller::Valve_Controller() {
    valve1Status = false;
}

void Valve_Controller::begin() {
  Serial.println("Init Valves...");
  pinMode(VALVE1_OPEN_PIN, OUTPUT); 
  pinMode(VALVE1_CLOSE_PIN, OUTPUT);
  digitalWrite(VALVE1_OPEN_PIN, V1_OFF); 
  digitalWrite(VALVE1_CLOSE_PIN, V1_OFF);

  Serial.println("✓ Actuators Ready");
}

void Valve_Controller::openValve1() { 
  digitalWrite(VALVE1_CLOSE_PIN, V1_OFF); 
  delay(200); 
  digitalWrite(VALVE1_OPEN_PIN, V1_ON); 
  valve1Status = true; 
  Serial.println("✓ Valve 1 OPENING"); 
}

void Valve_Controller::closeValve1() { 
  digitalWrite(VALVE1_OPEN_PIN, V1_OFF); 
  delay(200); 
  digitalWrite(VALVE1_CLOSE_PIN, V1_ON); 
  valve1Status = false; 
  Serial.println("✓ Valve 1 CLOSING"); 
}

bool Valve_Controller::getValve1Status() { 
  return valve1Status; 
}

void Valve_Controller::printCompactStatus() {
  Serial.print("V1(Drain): "); 
  Serial.print(valve1Status ? "OPEN" : "CLOSED");
  Serial.println();
}

void Valve_Controller::printStatus() {
  Serial.println("--- Status Valve ---");
  Serial.print("Valve 1 (Drain) : "); Serial.println(valve1Status ? "TERBUKA" : "TERTUTUP");
  Serial.println("--------------------");
}
