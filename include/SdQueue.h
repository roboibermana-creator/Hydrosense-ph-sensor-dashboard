#ifndef SD_QUEUE_H
#define SD_QUEUE_H

#include <Arduino.h>

// Antrean data offline di SD card. Tiap pembacaan yang gagal terkirim ke
// Supabase ditambahkan sebagai satu baris JSON ("/queue.jsonl"); posisi baris
// pertama yang belum terkirim disimpan di "/queue.pos". Task di belakang layar
// mengirim ulang dari yang paling lama begitu WiFi tersambung, dengan WAKTU
// PENGUKURAN ASLI (NTP), lalu memajukan posisi supaya tidak terkirim dua kali.

// Buat mutex + task. Panggil sekali dari mulaiSemuaTask().
void sdQueueMulai();

// true kalau SD card sudah ter-mount dan siap dipakai.
bool sdQueueSiap();

// Tambah satu payload JSON (satu baris, tanpa newline) ke antrean. false kalau
// SD tidak siap atau gagal menulis.
bool sdQueueTambah(const String &payload);

// Cetak status antrean (SD, ukuran, byte tertunda, waktu NTP) ke Serial.
void sdQueueStatus();

#endif // SD_QUEUE_H
