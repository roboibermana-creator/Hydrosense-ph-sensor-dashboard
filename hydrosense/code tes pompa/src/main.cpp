#include <Arduino.h>
#include "pompa.h"

// ============================================================
// SETUP
// ============================================================
void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("======================================");
  Serial.println(" HYDROSENSE PERISTALTIC PUMP TEST");
  Serial.println(" ESP32-S3 + RELAY 1 + KPK400");
  Serial.println("======================================");

  // Set pin sebagai output
  pinMode(PUMP_RELAY_PIN, OUTPUT);

  // SAFETY: Pompa harus OFF saat startup
  digitalWrite(PUMP_RELAY_PIN, RELAY_OFF);
  pumpState = false;

  Serial.println();
  Serial.println("Safety startup:");
  Serial.println("Pump forced OFF.");

  showMenu();
}

// ============================================================
// LOOP
// ============================================================
void loop()
{
  // ==========================================================
  // SERIAL CONTROL
  // ==========================================================
  if (Serial.available() > 0)
  {
    char command = Serial.read();

    if (command == '1') {
      automaticMode = false;
      pumpON();
    }
    else if (command == '0') {
      automaticMode = false;
      pumpOFF();
    }
    else if (command == 'A' || command == 'a') {
      automaticMode = true;
      pumpOFF();
      previousMillis = millis();

      Serial.println();
      Serial.println("AUTO MODE AKTIF");
      Serial.println("Cycle:");
      Serial.println("ON  = 10 detik");
      Serial.println("OFF = 20 detik");
    }
    else if (command == 'M' || command == 'm') {
      automaticMode = false;
      pumpOFF();

      Serial.println();
      Serial.println("MANUAL MODE AKTIF");
    }
    else if (command == 'S' || command == 's') {
      showStatus();
    }
    else if (command == 'H' || command == 'h') {
      showMenu();
    }
  }

  // ==========================================================
  // AUTOMATIC MODE
  // ==========================================================
  if (automaticMode)
  {
    unsigned long currentMillis = millis();

    // POMPA SEDANG OFF -> Setelah 20 detik -> ON
    if (!pumpState) {
      if (currentMillis - previousMillis >= PUMP_OFF_TIME) {
        pumpON();
        previousMillis = currentMillis;
      }
    }
    // POMPA SEDANG ON -> Setelah 10 detik -> OFF
    else {
      if (currentMillis - previousMillis >= PUMP_ON_TIME) {
        pumpOFF();
        previousMillis = currentMillis;
      }
    }
  }
}
