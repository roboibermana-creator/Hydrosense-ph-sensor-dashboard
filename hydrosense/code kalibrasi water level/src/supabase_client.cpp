#include "supabase_client.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
// HELPER
// ============================================================

// Pasang header standar Supabase / PostgREST
static void addSupabaseHeaders(HTTPClient &http, bool withContentType, bool minimalReturn) {
  http.setTimeout(10000);
  if (withContentType) {
    http.addHeader("Content-Type", "application/json");
  }
  http.addHeader("apikey", SUPABASE_PUBLISHABLE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_PUBLISHABLE_KEY);
  if (minimalReturn) {
    http.addHeader("Prefer", "return=minimal");
  }
}

// ============================================================
// INSERT SENSOR LOG
// ============================================================

void insertSensorLog(float distanceCm, float levelPercent) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[sensor_logs] WiFi disconnected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // testing; production sebaiknya pakai Root CA Supabase

  HTTPClient http;
  String endpoint = String(SUPABASE_URL) + "/rest/v1/sensor_logs";

  Serial.println();
  Serial.println("[sensor_logs] POST");

  if (!http.begin(client, endpoint)) {
    Serial.println("[sensor_logs] HTTP begin FAILED");
    return;
  }

  addSupabaseHeaders(http, true, true);

  StaticJsonDocument<256> doc;
  doc["device_id"]       = DEVICE_ID;
  doc["level_percent"]   = levelPercent;
  doc["raw_distance_cm"] = distanceCm;

  String payload;
  serializeJson(doc, payload);

  Serial.print("Payload: ");
  Serial.println(payload);

  int code = http.POST(payload);
  Serial.printf("[sensor_logs] HTTP %d\n", code);

  if (code > 0) {
    if (code >= 200 && code < 300) {
      Serial.println("[sensor_logs] SUCCESS");
    } else {
      Serial.println("[sensor_logs] SERVER ERROR");
      Serial.print("Response: ");
      Serial.println(http.getString());
    }
  } else {
    Serial.print("[sensor_logs] ERROR: ");
    Serial.println(http.errorToString(code));
  }

  http.end();
}

// ============================================================
// INSERT ALERT LOG
// ============================================================

void insertAlertLog(String alertType, float levelAtTrigger) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (alertType == "NORMAL") return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String endpoint = String(SUPABASE_URL) + "/rest/v1/alert_logs";

  if (!http.begin(client, endpoint)) {
    Serial.println("[alert_logs] HTTP begin FAILED");
    return;
  }

  addSupabaseHeaders(http, true, true);

  StaticJsonDocument<256> doc;
  doc["device_id"]                = DEVICE_ID;
  doc["alert_type"]               = alertType;
  doc["level_percent_at_trigger"] = levelAtTrigger;

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.printf("[alert_logs] %s | HTTP %d\n", alertType.c_str(), code);

  if (code > 0) {
    if (code < 200 || code >= 300) {
      Serial.print("Response: ");
      Serial.println(http.getString());
    }
  } else {
    Serial.println(http.errorToString(code));
  }

  http.end();
}

// ============================================================
// UPDATE DEVICE ONLINE
// ============================================================

void updateDeviceOnline(bool online) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String endpoint = String(SUPABASE_URL) + "/rest/v1/devices?id=eq." + DEVICE_ID;

  if (!http.begin(client, endpoint)) {
    Serial.println("[devices] HTTP begin FAILED");
    return;
  }

  addSupabaseHeaders(http, true, true);

  StaticJsonDocument<128> doc;
  doc["is_online"] = online;

  String payload;
  serializeJson(doc, payload);

  int code = http.PATCH(payload);
  Serial.printf("[devices] PATCH HTTP %d\n", code);

  if (code > 0) {
    if (code >= 200 && code < 300) {
      Serial.println("[devices] update SUCCESS");
    } else {
      Serial.print("[devices] Response: ");
      Serial.println(http.getString());
    }
  } else {
    Serial.println(http.errorToString(code));
  }

  http.end();
}

// ============================================================
// FETCH THRESHOLD CONFIG
// ============================================================

void fetchThresholdConfig() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String endpoint = String(SUPABASE_URL) +
    "/rest/v1/threshold_configs" +
    "?device_id=eq." + DEVICE_ID +
    "&select=low_threshold_percent,high_threshold_percent" +
    "&limit=1";

  Serial.println();
  Serial.println("[threshold_configs] GET");

  if (!http.begin(client, endpoint)) {
    Serial.println("[threshold_configs] HTTP begin FAILED");
    return;
  }

  addSupabaseHeaders(http, false, false);

  int code = http.GET();
  Serial.printf("[threshold_configs] HTTP %d\n", code);

  if (code == 200) {
    String response = http.getString();
    Serial.print("Response: ");
    Serial.println(response);

    StaticJsonDocument<512> resDoc;
    DeserializationError err = deserializeJson(resDoc, response);

    if (!err && resDoc.is<JsonArray>()) {
      JsonArray arr = resDoc.as<JsonArray>();

      if (arr.size() > 0) {
        JsonObject obj = arr[0];

        if (!obj["low_threshold_percent"].isNull()) {
          thLow = obj["low_threshold_percent"].as<float>();
        }
        if (!obj["high_threshold_percent"].isNull()) {
          thHigh = obj["high_threshold_percent"].as<float>();
        }

        Serial.printf("[threshold_configs] LOW=%.1f HIGH=%.1f\n", thLow, thHigh);
      } else {
        Serial.println("[threshold_configs] Tidak ada konfigurasi.");
        Serial.printf("Pakai default LOW=%.1f HIGH=%.1f\n", thLow, thHigh);
      }
    } else {
      Serial.println("[threshold_configs] JSON parsing gagal.");
    }
  } else {
    Serial.print("[threshold_configs] ERROR HTTP: ");
    Serial.println(code);

    if (code > 0) {
      Serial.print("Response: ");
      Serial.println(http.getString());
    }
  }

  http.end();
}