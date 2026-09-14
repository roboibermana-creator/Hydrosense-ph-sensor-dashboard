/*
 * ============================================================
 * GROUND TANK MONITORING - PRODUCTION (SENSOR ASLI)
 * ESP32-S3 -> Supabase REST API / PostgREST
 * Sensor: Radar Level Sensor 4-20mA -> DFRobot SEN0262 -> ADC
 *
 * TABLE: devices, sensor_logs, threshold_configs, alert_logs
 *
 * STRUKTUR:
 *   include/secrets.h          -> kredensial WiFi & Supabase
 *   include/config.h           -> konstanta & variabel global
 *   include/sensor.h           -> pembacaan sensor & status
 *   include/wifi_manager.h     -> koneksi + auto reconnect
 *   include/supabase_client.h  -> semua request ke Supabase
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "sensor.h"
#include "wifi_manager.h"
#include "supabase_client.h"

static unsigned long lastSendTime       = 0;
static unsigned long lastThresholdFetch = 0;

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("======================================");
  Serial.println(" GROUND TANK MONITORING");
  Serial.println(" ESP32-S3 -> SUPABASE (SENSOR ASLI)");
  Serial.println("======================================");

  sensorBegin();
  wifiBegin();
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    printWiFiStatus();
    fetchThresholdConfig();
    updateDeviceOnline(true);
  } else {
    Serial.println();
    Serial.println("WARNING: WiFi belum terhubung.");
    Serial.println("Program tetap berjalan, akan reconnect otomatis.");
  }
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  // ---- MAINTAIN WIFI (non-blocking, auto reconnect) ----
  maintainWiFi();

  // ---- FETCH THRESHOLD SETIAP 1 MENIT ----
  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastThresholdFetch >= THRESHOLD_FETCH_INTERVAL_MS) {
    lastThresholdFetch = millis();
    fetchThresholdConfig();
  }

  // ---- BACA SENSOR & KIRIM DATA SETIAP 5 DETIK ----
  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();

    float levelPercent = 0;
    float distanceCm   = 0;
    float currentMa    = 0;

    readTankSensor(levelPercent, distanceCm, currentMa);

    String status = getStatus(levelPercent);

    Serial.println();
    Serial.println("--------------------------------------");
    Serial.printf("Arus     : %.2f mA\n", currentMa);
    Serial.printf("Level    : %.1f %%\n", levelPercent);
    Serial.printf("Jarak    : %.1f cm\n", distanceCm);
    Serial.printf("Status   : %s\n", status.c_str());
    Serial.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());

    // Peringatan fault/overrun sesuai datasheet converter
    if (currentMa < I_MIN_MA - 0.2) {
      Serial.println("PERINGATAN: Arus < 4mA -> kemungkinan sensor/loop putus!");
    } else if (currentMa > I_MAX_MA + 0.2) {
      Serial.println("PERINGATAN: Arus > 20mA -> overrun!");
    }

    if (WiFi.status() == WL_CONNECTED) {
      insertSensorLog(distanceCm, levelPercent);

      if (status != lastStatus) {
        Serial.printf("STATUS BERUBAH: %s -> %s\n", lastStatus.c_str(), status.c_str());
        insertAlertLog(status, levelPercent);
        lastStatus = status;
      }

      updateDeviceOnline(true);
    } else {
      Serial.println("Supabase dilewati: WiFi disconnected.");
    }
  }
}