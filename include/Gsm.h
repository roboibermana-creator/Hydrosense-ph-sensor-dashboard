#pragma once
// Data seluler lewat modem SIMCom SIM7600 (TinyGSM). Modem melayani TLS sendiri, jadi ESP32
// tidak memakai memori mbedTLS. Satu modem = satu permintaan HTTP sekali waktu (mutex).
#include <Arduino.h>

bool gsmSiap();              // true bila data seluler (LTE/GPRS) tersambung
void gsmMulai();             // buat task yang menyalakan modem dan menjaga koneksi
int gsmRssiDbm();            // kekuatan sinyal (dBm), 0 bila tidak diketahui
const char *gsmStatusTeks(); // untuk Serial Monitor

// Satu permintaan HTTPS ke SUPABASE_URL. `path` diawali '/'. `header` berisi baris
// "Nama: nilai\r\n". Hasil: kode HTTP, atau <=0 bila gagal. Badan respons di `resp`.
int gsmHttp(const char *metode, const String &path, const String &header,
            const String &badan, String &resp);

void gsmCetakStatus(); // perintah Serial 'U': diagnosa modem (AT, SIM, sinyal, operator, jam, tes Supabase)
bool gsmPosisi(float *lat, float *lon); // true bila GNSS sudah dapat fix (derajat desimal)
