#pragma once

// OTA (upload firmware lewat WiFi) via ArduinoOTA, berjalan di task FreeRTOS
// sendiri. Panggil sekali dari setup(); task menunggu WiFi tersambung sendiri.
void mulaiTaskOTA();

// true selama upload OTA berlangsung -- task lain boleh cek ini untuk
// menahan kerja berat (Modbus/HTTPS) supaya upload tidak gagal.
bool otaSedangBerjalan();
