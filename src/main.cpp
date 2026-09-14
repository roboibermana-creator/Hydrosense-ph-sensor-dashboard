#include <Arduino.h>
#include <WiFi.h>

constexpr unsigned long SCAN_INTERVAL_MS = 10000;
constexpr char WIFI_SSID[] = "SPRM-CORP";
// Backslash pada password ditulis sebagai \\ dalam string C++.
constexpr char WIFI_PASSWORD[] = "e$kr1mN@N@s3000";

const char *encryptionName(wifi_auth_mode_t type) {
  switch (type) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "Tidak diketahui";
  }
}

void scanWifi() {
  Serial.println("\nMemindai jaringan Wi-Fi...");
  const int networkCount = WiFi.scanNetworks();

  if (networkCount <= 0) {
    Serial.println("Tidak ada jaringan Wi-Fi ditemukan.");
  } else {
    Serial.printf("Ditemukan %d jaringan:\n", networkCount);

    for (int i = 0; i < networkCount; i++) {
      Serial.printf("%2d. SSID: %s | RSSI: %d dBm | Channel: %d | Enkripsi: %s\n",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    WiFi.channel(i),
                    encryptionName(WiFi.encryptionType(i)));
    }
  }

  WiFi.scanDelete();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Menghubungkan ke %s", WIFI_SSID);

  const unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nTerhubung ke Wi-Fi.");
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nGagal terhubung dalam 20 detik. Scanner tetap dijalankan.");
  }
}

void loop() {
  scanWifi();
  delay(SCAN_INTERVAL_MS);
}
