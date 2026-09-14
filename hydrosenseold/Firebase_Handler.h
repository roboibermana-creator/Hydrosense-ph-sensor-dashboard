#ifndef FIREBASE_HANDLER_H
#define FIREBASE_HANDLER_H

#include <WiFi.h>
#include <FirebaseESP32.h>
#include "config.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 28800);

class Firebase_Handler {
private:
  FirebaseData fbdo_log;
  FirebaseData fbdo_latest;
  FirebaseData fbdo_stream;
  FirebaseConfig config;
  FirebaseAuth auth;
  unsigned long lastUpdate = 0;
  bool _firebaseInitialized = false; // Flag status inisialisasi

  void connectWiFi() {
    // Jika sudah connect, tidak perlu reconnect
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.println("Connecting to WiFi...");
    WiFi.mode(WIFI_STA); // Pastikan mode Station
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    // Tambah waktu tunggu jadi 10 detik (20 x 500ms)
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✓ WiFi Connected!");
      Serial.print("IP: "); Serial.println(WiFi.localIP());
    } else {
      Serial.println("\n✗ WiFi Failed to connect in setup (Will retry in loop)");
    }
  }

  // Fungsi Internal untuk Inisialisasi Firebase (Bisa dipanggil di setup atau loop)
  void initFirebase() {
    if (WiFi.status() == WL_CONNECTED && !_firebaseInitialized) {
      Serial.println("Initializing Firebase Config...");
      
      config.host = FIREBASE_HOST;
      config.signer.tokens.legacy_token = FIREBASE_AUTH;
      
      // Mengatur timeout agar tidak blocking terlalu lama
      config.timeout.wifiReconnect = 10000;
      config.timeout.socketConnection = 10000;
      config.timeout.sslHandshake = 10000;
      config.timeout.rtdbKeepAlive = 45000;
      config.timeout.rtdbStreamReconnect = 1000;
      config.timeout.rtdbStreamError = 3000;

      Firebase.begin(&config, &auth);
      Firebase.reconnectWiFi(true);
      
      _firebaseInitialized = true;
      Serial.println("✓ Firebase Initialized & Auth Token Set");

      // Init NTP hanya jika belum
      timeClient.begin();
      if(!timeClient.update()) {
        timeClient.forceUpdate();
      }
    }
  }

public:
  void begin() {
    connectWiFi();
    // Coba init di awal, tapi kalau WiFi gagal, ini akan dilewati dan dicoba lagi di loop
    initFirebase();
  }

  void sendData(float phValue, float tssValue, float flowRate, bool valve1Status, bool valve2Status, bool pumpStatus, bool autoMode, uint8_t dosingPercent) {
    // 1. Cek Interval
    if (millis() - lastUpdate < FIREBASE_UPDATE_INTERVAL) { return; }

    // 2. Cek Koneksi WiFi (Auto Reconnect jika putus)
    if (WiFi.status() != WL_CONNECTED) {
       Serial.println("⚠ WiFi lost, attempting reconnect...");
       WiFi.reconnect();
       return; 
    }

    // 3. CEK KRUSIAL: Apakah Firebase sudah di-init?
    if (!_firebaseInitialized) {
       Serial.println("⚠ Firebase not initialized yet, retrying init...");
       initFirebase();
       // Jika masih gagal setelah dicoba init, return dulu tunggu loop berikutnya
       if (!_firebaseInitialized) return; 
    }

    timeClient.update();
    unsigned long epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime ((time_t *)&epochTime);

    char datePath[11];
    sprintf(datePath, "%d-%02d-%02d", (ptm->tm_year + 1900), (ptm->tm_mon + 1), ptm->tm_mday);

    char timePath[7];
    sprintf(timePath, "%02d%02d%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);

    FirebaseJson latestData;
    FirebaseJson historyEntry;

    // Masukkan data ke JSON
    if (phValue >= 0) { latestData.set("ph_value", phValue); historyEntry.set("ph_value", phValue); } 
    else { latestData.set("ph_value", "null"); }

    if (tssValue >= 0) { latestData.set("tss_value", tssValue); historyEntry.set("tss_value", tssValue); } 
    else { latestData.set("tss_value", "null"); }

    if (flowRate >= 0) { latestData.set("flow_rate_m3d", flowRate * 86400); historyEntry.set("flow_rate_m3d", flowRate * 86400); } 
    else { latestData.set("flow_rate_m3d", "null"); }

    latestData.set("valve1_status", valve1Status ? 1 : 0);
    latestData.set("valve2_status", valve2Status ? 1 : 0);
    latestData.set("pump_status", pumpStatus ? 1 : 0);
    latestData.set("auto_mode", autoMode ? 1 : 0);
    latestData.set("last_update/.sv", "timestamp");
    latestData.set("valve2_percent", (int)dosingPercent);
    
    historyEntry.set("timestamp", epochTime);
    historyEntry.set("valve2_percent", (int)dosingPercent);

    // Kirim Data
    if (Firebase.updateNode(fbdo_latest, FIREBASE_LATEST_DATA_PATH, latestData)) {
       // Sukses silent
    } else {
        Serial.print("Data Send FAILED: "); Serial.println(fbdo_latest.errorReason());
        // Jika errornya karena auth, reset flag biar di-init ulang
        if (fbdo_latest.errorReason() == "Firebase authentication was not initialized") {
           _firebaseInitialized = false;
        }
    }

    // Kirim History
    String historyPath = String("history/") + datePath + "/" + timePath;
    if (historyEntry.iteratorBegin() > 1) { 
      if (!Firebase.setJSON(fbdo_log, historyPath, historyEntry)) {
          Serial.println("✗ History fail: " + fbdo_log.errorReason());
      }
    }
    
    lastUpdate = millis();
  }
  
   void beginControlStream(FirebaseData::StreamEventCallback dataAvailableCallback) {
     if (!_firebaseInitialized) return; // Jangan start stream kalau belum init

     Serial.print("Starting Firebase stream: "); Serial.println(FIREBASE_CONTROL_REQUEST_PATH);
     if (Firebase.beginStream(fbdo_stream, FIREBASE_CONTROL_REQUEST_PATH)) {
        Firebase.setStreamCallback(fbdo_stream, dataAvailableCallback, nullptr, FIREBASE_STREAM_TIMEOUT_MS);
        Serial.println("✓ Stream started.");
     } else {
        Serial.println("✗ Stream failed: " + fbdo_stream.errorReason());
     }
  }

  void readControlStream() {
      if (WiFi.status() != WL_CONNECTED || !_firebaseInitialized) return;
      
      if (!Firebase.ready()) return;

      if (!fbdo_stream.httpConnected()) return;

      if (Firebase.readStream(fbdo_stream)) {
          if (fbdo_stream.streamTimeout()) { Serial.println("! Stream timeout."); }
      }
  }

  bool deleteNode(const String& nodePath) {
      if (!_firebaseInitialized) return false;
      if(Firebase.deleteNode(fbdo_log, nodePath)){ return true; }
      return false;
  }

  FirebaseData& getFirebaseDataObject() { return fbdo_log; }
};

#endif // FIREBASE_HANDLER_H