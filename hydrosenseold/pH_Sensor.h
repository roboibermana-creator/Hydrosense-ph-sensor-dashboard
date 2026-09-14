#ifndef PH_SENSOR_H
#define PH_SENSOR_H

#include <ModbusMaster.h>   // Pastikan library ModbusMaster terinstal
#include <HardwareSerial.h> // Untuk Serial2
#include "config.h"         // Include config untuk pin & ID

class pH_Sensor {
private:
  ModbusMaster node; // Objek ModbusMaster
  HardwareSerial& modbusSerial = Serial2;

  static void preTransmission() { digitalWrite(RE_DE_PIN, HIGH); }
  static void postTransmission() { digitalWrite(RE_DE_PIN, LOW); }

public:
  void begin() {
    Serial.println("Init pH Sensor (Modbus)...");
    pinMode(RE_DE_PIN, OUTPUT); digitalWrite(RE_DE_PIN, LOW);
    modbusSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    node.begin(PH_SLAVE_ID, modbusSerial);
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);
    Serial.print("✓ pH Sensor Ready (Slave ID: "); Serial.print(PH_SLAVE_ID); Serial.println(")");
  }

  float readPH() {
    for (int i = 0; i < 3; i++) {
      uint8_t result = node.readHoldingRegisters(PH_REGISTER_ADDRESS, 1);
      if (result == node.ku8MBSuccess) {
        return node.getResponseBuffer(0) / 100.0; // Asumsi pH * 100
      }
      delay(300);
    }
    Serial.println("! Failed to read pH sensor");
    return -1.0; // Error
  }
};

#endif // PH_SENSOR_H