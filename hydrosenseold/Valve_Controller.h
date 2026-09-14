#ifndef VALVE_CONTROLLER_H
#define VALVE_CONTROLLER_H

#include <Arduino.h>
#include "config.h" // Include config untuk pin

// Logika relay berdasarkan tipe trigger
#define RELAY_HIGH_TRIGGER HIGH
#define RELAY_LOW_TRIGGER LOW

// Level ON/OFF untuk Valve 1 (Relay Shield -> High Trigger)
const int V1_ON = RELAY_HIGH_TRIGGER; // HIGH = ON
const int V1_OFF = LOW;               // LOW = OFF

// Level ON/OFF untuk Valve 2 (Relay Eksternal -> Low Trigger)
const int V2_ON = RELAY_LOW_TRIGGER; // LOW = ON
const int V2_OFF = HIGH;              // HIGH = OFF

// Level ON/OFF untuk Pompa (Relay Eksternal -> Low Trigger)
const int PUMP_ON = RELAY_LOW_TRIGGER; // LOW = ON
const int PUMP_OFF = HIGH;              // HIGH = OFF


class Valve_Controller {
private:
  bool pumpStatus = false;   // Status pompa (true=ON)
  bool valve1Status = false; // Status Valve 1 (true=OPEN)
  bool valve2Status = false; // Status Valve 2 (true=OPEN)

public:
  void begin() {
    Serial.println("Init Valves & Pump...");
    pinMode(PUMP_PIN, OUTPUT); digitalWrite(PUMP_PIN, PUMP_OFF);
    pinMode(VALVE1_OPEN_PIN, OUTPUT); pinMode(VALVE1_CLOSE_PIN, OUTPUT);
    digitalWrite(VALVE1_OPEN_PIN, V1_OFF); digitalWrite(VALVE1_CLOSE_PIN, V1_OFF);
    pinMode(VALVE2_OPEN_PIN, OUTPUT); pinMode(VALVE2_CLOSE_PIN, OUTPUT);
    digitalWrite(VALVE2_OPEN_PIN, V2_OFF); digitalWrite(VALVE2_CLOSE_PIN, V2_OFF);
    Serial.println("✓ Actuators Ready");
  }

  void pumpOn() { digitalWrite(PUMP_PIN, PUMP_ON); pumpStatus = true; Serial.println("✓ Pump ON"); }
  void pumpOff() { digitalWrite(PUMP_PIN, PUMP_OFF); pumpStatus = false; Serial.println("✓ Pump OFF"); }

  void openValve1() { digitalWrite(VALVE1_CLOSE_PIN, V1_OFF); delay(200); digitalWrite(VALVE1_OPEN_PIN, V1_ON); valve1Status = true; Serial.println("✓ Valve 1 OPENING"); }
  void closeValve1() { digitalWrite(VALVE1_OPEN_PIN, V1_OFF); delay(200); digitalWrite(VALVE1_CLOSE_PIN, V1_ON); valve1Status = false; Serial.println("✓ Valve 1 CLOSING"); }

  void openValve2() { digitalWrite(VALVE2_CLOSE_PIN, V2_OFF); delay(200); digitalWrite(VALVE2_OPEN_PIN, V2_ON); valve2Status = true; Serial.println("✓ Valve 2 (Tawas) OPENING"); }
  void closeValve2() { digitalWrite(VALVE2_OPEN_PIN, V2_OFF); delay(200); digitalWrite(VALVE2_CLOSE_PIN, V2_ON); valve2Status = false; Serial.println("✓ Valve 2 (Tawas) CLOSING"); }

  bool getPumpStatus() { return pumpStatus; }
  bool getValve1Status() { return valve1Status; }
  bool getValve2Status() { return valve2Status; }

  void printCompactStatus() {
    Serial.print("Pump: "); Serial.print(pumpStatus ? "ON" : "OFF");
    Serial.print(" | V1(Drain): "); Serial.print(valve1Status ? "OPEN" : "CLOSED");
    Serial.print(" | V2(Tawas): "); Serial.println(valve2Status ? "OPEN" : "CLOSED");
  }

  void printStatus() {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║      ACTUATOR STATUS                   ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.print("Pump: "); Serial.println(pumpStatus ? "🟢 ON" : "🔴 OFF");
    Serial.print("Valve 1 (Drain): "); Serial.println(valve1Status ? "🟢 OPEN" : "🔴 CLOSED");
    Serial.print("Valve 2 (Tawas): "); Serial.println(valve2Status ? "🟢 OPEN" : "🔴 CLOSED");
    Serial.println("════════════════════════════════════════\n");
  }
};

#endif // VALVE_CONTROLLER_H