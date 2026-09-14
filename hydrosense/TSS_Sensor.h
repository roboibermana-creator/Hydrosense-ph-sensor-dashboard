#ifndef TSS_SENSOR_H
#define TSS_SENSOR_H

#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include "config.h" // Include config untuk pin, ID, register

class TSS_Sensor {
private:
 ModbusMaster node;
 HardwareSerial& modbusSerial = Serial2; // Gunakan Serial2 yang sama dengan pH

 static void preTransmission() { digitalWrite(RE_DE_PIN, HIGH); }
 static void postTransmission() { digitalWrite(RE_DE_PIN, LOW); }

public:
 void begin() {
  Serial.println("Init TSS Sensor (Modbus)...");
  pinMode(RE_DE_PIN, OUTPUT); digitalWrite(RE_DE_PIN, LOW);
  // Serial2 sudah di-begin oleh pH_Sensor
  node.begin(TSS_SLAVE_ID, modbusSerial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  Serial.print("✓ TSS Sensor Ready (Slave ID: "); Serial.print(TSS_SLAVE_ID); Serial.println(")");
 }

 float readTSS() {
  for (int i = 0; i < 3; i++) {
   uint8_t result = node.readHoldingRegisters(REG_TURBIDITY, 2); // Baca 2 register
   if (result == node.ku8MBSuccess) {
    uint16_t lowWord = node.getResponseBuffer(0);
    uint16_t highWord = node.getResponseBuffer(1);
    uint32_t combined = ((uint32_t)highWord << 16) | lowWord;
    float tssValue;
    memcpy(&tssValue, &combined, sizeof(tssValue)); // Konversi bit ke float
    return tssValue / 100.0; // <<< PERUBAHAN DI SINI
   }
   delay(500);
  }
  Serial.println("! Failed to read TSS sensor");
  return -1.0; // Error
 }

 void setScrapingTime(uint16_t minutes) {
  uint8_t result = node.writeSingleRegister(REG_AUTO_SCRAPING_INTERVAL, minutes);
  if (result == node.ku8MBSuccess) { Serial.print("✓ Auto scraping interval set to: "); Serial.print(minutes); Serial.println(" min"); }
  else { Serial.println("✗ Failed to set auto scraping interval!"); }
 }

 void startScraping() {
  Serial.println(">>> Starting Manual TSS Scraping...");
  uint8_t result = node.writeSingleRegister(REG_MANUAL_SCRAPING, 66);
  if (result == node.ku8MBSuccess) {
   Serial.println("✓ Scraping command sent. Waiting ~15s...");
   for (int i = 0; i < 15; i++) { delay(1000); Serial.print("."); }
   Serial.println("\n✓ Scraping done!");
  } else { Serial.print("✗ Failed to start scraping! Modbus Error: 0x"); Serial.println(result, HEX); }
 }

 void readStatus() {
  Serial.println("\n--- TSS Sensor Status ---");
  uint8_t result; uint32_t combinedValue; float floatValue;

  result = node.readHoldingRegisters(REG_TURBIDITY, 2);
  if (result == node.ku8MBSuccess) { combinedValue = ((uint32_t)node.getResponseBuffer(1) << 16) | node.getResponseBuffer(0); memcpy(&floatValue, &combinedValue, sizeof(floatValue)); Serial.print("Turbidity: "); Serial.print(floatValue / 100.0, 1); Serial.println(" mg/L"); } // <<< PERUBAHAN DI SINI
  else { Serial.println("Turbidity: Read Error"); }
  delay(200);

  result = node.readHoldingRegisters(REG_INTERNAL_TEMP, 2);
  if (result == node.ku8MBSuccess) { combinedValue = ((uint32_t)node.getResponseBuffer(1) << 16) | node.getResponseBuffer(0); memcpy(&floatValue, &combinedValue, sizeof(floatValue)); Serial.print("Temp Internal: "); Serial.print(floatValue, 1); Serial.println(" °C"); }
  else { Serial.println("Temp Internal: Read Error"); }
  delay(200);

  result = node.readHoldingRegisters(REG_ELECTRODE_TYPE, 1);
  if (result == node.ku8MBSuccess) { uint16_t type = node.getResponseBuffer(0); Serial.print("Electrode Type: "); Serial.println(type == 1 ? "With Cleaning" : "Without Cleaning"); }
  else { Serial.println("Electrode Type: Read Error"); }

  Serial.println("-------------------------");
 }
};

#endif // TSS_SENSOR_H
