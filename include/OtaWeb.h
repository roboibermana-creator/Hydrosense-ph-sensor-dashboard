#pragma once
// Update firmware lewat Supabase Storage (bucket publik "firmware"), dipicu dari dashboard:
// dashboard menulis baris device_commands type='ota' {file, sha256, versi}; alat mengunduh
// file itu, memeriksa SHA-256, menulis ke partisi OTA cadangan, lalu reboot.
// Saat ini hanya lewat WiFi (unduhan 1 MB lewat GSM belum didukung, lihat OtaWeb.cpp).
void cekPerintahOtaDariWeb();
