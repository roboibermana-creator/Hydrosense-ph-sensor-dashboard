#include "wifi_manager.h"
#include "config.h"
#include "supabase_client.h"

#include <WiFi.h>

static unsigned long lastWiFiReconnect = 0;

// ============================================================
// SETUP AWAL WIFI
// ============================================================

void wifiBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
}

// ============================================================
// WIFI CONNECT
// ============================================================

bool connectWiFi() {
  Serial.println();
  Serial.println("======================================");
  Serial.println(" WIFI CONNECTION");
  Serial.println("======================================");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);

  Serial.println("Disconnect WiFi lama...");
  WiFi.disconnect(true);
  delay(500);

  WiFi.mode(WIFI_STA);
  Serial.println("Memulai koneksi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < WIFI_CONNECT_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("======================================");
    Serial.println(" WIFI CONNECTED");
    Serial.println("======================================");
    printWiFiStatus();
    return true;
  }

  Serial.println("======================================");
  Serial.println(" WIFI CONNECTION FAILED");
  Serial.println("======================================");
  Serial.print("WiFi status code: ");
  Serial.println(WiFi.status());
  Serial.println();
  Serial.println("Kemungkinan:");
  Serial.println("1. SSID bukan 2.4 GHz");
  Serial.println("2. Password salah");
  Serial.println("3. Router menggunakan WPA3-only");
  Serial.println("4. AP corporate menggunakan WPA2-Enterprise");
  Serial.println("5. Sinyal WiFi terlalu lemah");
  Serial.println("6. Power ESP32 tidak stabil");

  return false;
}

// ============================================================
// WIFI MAINTENANCE (dipanggil terus tiap loop, auto reconnect)
// ============================================================

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (millis() - lastWiFiReconnect >= WIFI_RECONNECT_INTERVAL) {
    lastWiFiReconnect = millis();

    Serial.println();
    Serial.println("[WiFi] Connection lost. Reconnecting...");

    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] Reconnected!");
      updateDeviceOnline(true);
    }
  }
}

// ============================================================
// PRINT WIFI STATUS
// ============================================================

void printWiFiStatus() {
  Serial.print("IP Address : "); Serial.println(WiFi.localIP());
  Serial.print("Gateway    : "); Serial.println(WiFi.gatewayIP());
  Serial.print("Subnet     : "); Serial.println(WiFi.subnetMask());
  Serial.print("DNS        : "); Serial.println(WiFi.dnsIP());
  Serial.print("RSSI       : "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  Serial.print("MAC        : "); Serial.println(WiFi.macAddress());
  Serial.println();
}