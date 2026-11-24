#pragma once
// Update firmware lewat Supabase Storage (bucket publik "firmware"), dipicu dari dashboard:
// dashboard menulis baris device_commands type='ota' {file, sha256, versi}; alat mengunduh
// file itu, memeriksa SHA-256, menulis ke partisi OTA cadangan, lalu reboot.
// Lewat WiFi (satu aliran) atau GSM (potongan HTTP Range, belum teruji di modem sungguhan).
void cekPerintahOtaDariWeb();
