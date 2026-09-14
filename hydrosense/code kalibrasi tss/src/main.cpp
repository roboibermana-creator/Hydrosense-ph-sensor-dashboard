#include <Arduino.h>
#include "config.h"
#include "TSS_Sensor.h"

TSS_Sensor tss;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // TODO: kalau memang sudah di-begin oleh pH_Sensor di project lain,
  // baris Serial2.begin ini boleh dihapus. Untuk project TSS berdiri
  // sendiri ini, Serial2 perlu di-begin di sini.
  Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);

  tss.begin();
}

void loop() {
  tss.readStatus();
  delay(5000);
}
