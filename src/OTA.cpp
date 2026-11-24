#include "OTA.h"
#include "Config.h"
#include "Network.h"
#include "Pompa.h"
#include <Arduino.h>
#include <ArduinoOTA.h>

static volatile bool otaAktif = false;

bool otaSedangBerjalan() { return otaAktif; }
void otaTandaiSibuk(bool b) { otaAktif = b; }

static void siapkanOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    otaAktif = true;
    // Aktuator ke kondisi aman: device akan reboot setelah upload, jangan
    // biarkan pompa menyala tanpa pengawasan selama proses flash.
    pumpOFF();
    Serial.println(F("\n[OTA] Mulai upload firmware -- pompa dimatikan"));
  });
  ArduinoOTA.onProgress([](unsigned int progres, unsigned int total) {
    static unsigned int persenTerakhir = 101;
    unsigned int persen = progres * 100 / total;
    if (persen / 10 != persenTerakhir / 10) {
      persenTerakhir = persen;
      Serial.printf("[OTA] %u%%\n", persen);
    }
  });
  ArduinoOTA.onEnd([]() { Serial.println(F("[OTA] Selesai, reboot...")); });
  ArduinoOTA.onError([](ota_error_t err) {
    otaAktif = false;
    Serial.printf("[OTA] GAGAL (kode %u)\n", (unsigned)err);
  });
  ArduinoOTA.begin();
  Serial.printf("[OTA] Siap: %s.local / %s\n", OTA_HOSTNAME,
                WiFi.localIP().toString().c_str());
}

static void taskOTA(void *) {
  bool siap = false;
  for (;;) {
    if (wifiTersambung()) {
      if (!siap) {
        siapkanOTA();
        siap = true;
      }
      ArduinoOTA.handle();
    }
    vTaskDelay(pdMS_TO_TICKS(otaAktif ? 1 : 20));
  }
}

void mulaiTaskOTA() {
  xTaskCreatePinnedToCore(taskOTA, "ota", 8192, nullptr, 2, nullptr, 0);
}
