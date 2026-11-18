# Tes sesuai flowchart: LoRa node HydroSense ↔ gateway Groundtank

Acuan: `Flowchart_HydroSense_GroundTank_LoRa.pdf` (SYS-00 … GT-05). Aturan tes:
- **Tanpa beban nyata dulu.** Relay pompa/valve cukup dilihat lewat LED relay, pompa dan valve tidak dialiri.
- **Mode manual dulu** sebelum otomatis (preferensi proyek).
- Buka dua Serial Monitor (node dan gateway) berdampingan; lihat `pio device list`.
- Catat hasil (LULUS / GAGAL + RSSI + catatan) di kolom paling kanan.

Firmware yang dipakai: node `LORA_ENABLED 1`, gateway `LORA_ENABLED 1`, keduanya sudah diupload ulang.

## Bagian A: tautan radio (HS-04, GT-02)

| # | Tes | Cara | Hasil yang diharapkan | Hasil |
|---|---|---|---|---|
| A1 | Kirim + ACK | Node: kirim `J` | Node: `ACK DITERIMA`; gateway: `PESAN dari node 1 (RSSI …): halo #n` | LULUS (8 Nov, RSSI -40 dBm) |
| A2 | Status modul | Node: kirim `I` | Konfigurasi `00 01 64 03 47 C3 00 00`, kanal 71, AUX HIGH | |
| A3 | Retry dan gagal | Matikan gateway (atau cabut antenanya), node `J` | Node mencoba 3×, lalu `TIDAK ADA ACK` setelah ± 3-5 dtk | |
| A4 | Jarak | Jauhkan node dan gateway bertahap, `J` tiap titik | Catat RSSI dan titik ACK mulai gagal (target ± 2 km) | |

## Bagian B: uplink data (HS-02, GT-02, GT-05)

| # | Tes | Cara | Hasil yang diharapkan | Hasil |
|---|---|---|---|---|
| B1 | DATA lewat LoRa | Sensor pH/TSS terpasang; biarkan satu siklus otomatis sampai "Kirim data" | Node: `[LORA] DATA seq N -> ACK diterima`; gateway: `[NET] DATA node 1 (RSSI …) -> HTTP 201` | |
| B2 | Baris di database | Lihat dashboard/tabel "Hydrosense table" | Baris baru dengan pH, TSS, suhu | |
| B3 | Tanpa ACK, jatuh ke WiFi | Matikan gateway saat node mengirim DATA | Node: `TANPA ACK`, lalu data tetap sampai lewat jalur WiFi lama | |
| B4 | Duplikat | (Bila ACK hilang sehingga node mengulang) | Gateway meneruskan **satu** baris untuk seq yang sama | |
| B0 | Level kosong (ultrasonik belum terpasang) | Paket DATA tanpa sensor level | Kolom level di paket bernilai "tidak ada" (0xFFFF), bukan 0 cm palsu; gateway tidak mengirimnya ke tabel | |
| B5 | Sensor tidak menjawab | Lepas sensor RS485 | DATA tetap dikirim dengan nilai kosong; jangan ada angka 0 palsu | |

## Bagian C: perintah manual / downlink (HS-03, GT-03, GT-05)

Mode manual dari dashboard; relay tidak dialiri beban.

| # | Tes | Cara | Hasil yang diharapkan | Hasil |
|---|---|---|---|---|
| C1 | Perintah pompa | Dashboard: mode manual, pompa **jalan** | Gateway: `[NET] perintah N -> SELESAI`; node: `[LORA] CMD N aksi=1 nilai=1 … dijalankan`; LED relay pompa menyala | |
| C2 | Perintah valve | Dashboard: valve **buka** lalu **tutup** | Node menjalankan buka/tutup; ACK tiap perintah | |
| C3 | Interlock float | Float switch HIGH (chamber penuh), kirim pompa **jalan** | Node: `DITOLAK interlock`; gateway: `perintah N -> DITOLAK`; pompa tidak menyala | |
| C4 | Perintah kedaluwarsa | Matikan gateway sebentar saat ada perintah baru, nyalakan setelah > 120 dtk | Perintah dibuang (`EXPIRED`), pompa tidak menyala | |
| C5 | Watchdog TTL | Mode manual, pompa jalan; matikan gateway | Setelah ± 120 dtk: node `TTL perintah habis -> pompa & valve OFF, kembali otomatis`, lalu paket EVENT | |
| C6 | Kembali otomatis | Dashboard: ubah ke otomatis | Node menerima aksi "kembali otomatis"; pompa dan valve OFF | |
| C8 | Status nyata di dashboard | Saat C1/C2 berjalan, lihat kartu pompa/valve di dashboard | Status berubah dalam ± 10 detik **tanpa WiFi node**: node mengirim status lewat ping LoRa, gateway meneruskan ke `devices` | |
| C7 | Perintah lama saat gateway baru menyala | Ada perintah di tabel, lalu nyalakan gateway | Gateway **tidak** menjalankannya (safety boot) | |

## Bagian D: siklus otomatis (SFC-01 S0…S6, HS-01/HS-02)

| # | Tes | Cara | Hasil yang diharapkan | Hasil |
|---|---|---|---|---|
| D1 | S0 → S1 | Mode otomatis, jadwal berjalan | Valve diperintah **tutup** dulu, lalu pompa ON (relay) | |
| D2 | S1 → S2 | Float HIGH (atau timeout pompa) | Pompa OFF, tunggu stabilisasi | |
| D3 | S2 → S3 | Setelah `stabilMs` | Sampling pH+TSS selama jendela baca, trimmed mean | |
| D4 | S4 | Setelah sampling | DATA terkirim (lihat B1), lalu valve **buka** | |
| D5 | S5 → S6 | Float LOW atau waktu valve habis | Valve tutup, siklus selesai, jeda ke siklus berikut | |

## Bagian E: selisih firmware dan flowchart (belum diimplementasi)

| # | Item flowchart | Kondisi sekarang | Keputusan |
|---|---|---|---|
| E1 | S0 → S1 hanya bila **tinggi air ≥ 20 cm** (HS-01) | Siklus dipicu **jadwal** dashboard; `STATE_TUNGGU_AIR` tidak dipanggil | **Diputuskan 8 Nov:** pakai **jadwal** dulu karena sensor ultrasonik belum terpasang. Syarat level ≥ 20 cm ditambahkan setelah ultrasonik terpasang (revisi flowchart HS-01/SFC-01 untuk sementara) |
| E2 | S9 alarm: pompa ON ≥ 5 menit tanpa float HIGH → pompa OFF, alarm, paket ALARM (HS-01) | Timeout pompa dianggap normal, tidak ada alarm/ALARM | Belum diputuskan |
| E3 | Retry baca sensor < 3 kali (HS-02) | Mode trimmed mean tidak mengulang; bila tanpa sampel pH valid, TSS tetap dikirim | Dapat diterima (catat di flowchart) |
| E4 | Valve **dibuka sebelum** DATA dikirim (HS-02) | Data dikirim **dulu**, baru valve dibuka | Dapat diterima (catat di flowchart) |
| E5 | Frekuensi 921,5 MHz, modul SPI (SYS-00) | Modul E220 UART, 921,125 MHz | Revisi flowchart |
| E6 | Tabel `hs_system_control` (GT-05) | Perintah dari `device_commands` (type=control) | Revisi flowchart |
| E7 | HS-03: "ACK + data" setelah perintah | Node membalas ACK lalu **status** (pompa/valve/float), bukan DATA pH/TSS | Mengirim ulang sampel lama membuat baris ganda di tabel data; status cukup untuk dashboard (catat di flowchart) |
| E8 | Node tanpa WiFi (`PAKAI_WIFI 0`) | Boot tidak menunggu internet, mulai MANUAL; otomatis hanya lewat perintah LoRa atau watchdog TTL; jadwal siklus memakai bawaan firmware (tidak bisa diubah dari dashboard lewat LoRa) | Perlu keputusan bila node di lapangan benar-benar tanpa internet: jadwal lewat LoRa? |

## Bagian F: fitur dashboard lewat LoRa (node `PAKAI_WIFI 0`, gateway online)

Semua fitur dashboard kini punya jalur LoRa. Waktu tempuh lebih lama dari WiFi (perintah kalibrasi/Modbus ± 20-60 dtk).
Dashboard: lihat tab terkait; Serial: node `[LORA]`/`[JADWAL]`, gateway `[NET]`/`[PROXY]`.

| # | Fitur | Cara | Hasil yang diharapkan | Hasil |
|---|---|---|---|---|
| F1 | Jadwal otomatis | Ubah jeda siklus di dashboard | Gateway: `Jadwal baru ... dikirim ke node` lalu `jadwal diterima node (ACK)`; node: `[JADWAL] Diterima via LoRa`; reboot node: `Dimuat dari NVS` | |
| F2 | Mode/fase/hitung mundur | Biarkan node jalan di mode otomatis | Kartu countdown/fase di dashboard bergerak (laporan gateway tiap 10 dtk) | |
| F3 | Level air dan hujan | Ketuk rain gauge / (ultrasonik terpasang) | Baris baru `rain_mm_*` dan `water_level` di tabel (gateway: `[NET] hujan dari node -> HTTP 201`) | |
| F4 | Data lengkap (probe_mv, NTU) | Siklus otomatis sampai kirim data | Gateway: `[PROXY] POST /rest/v1/Hydrosense%20table -> HTTP 201`; kolom probe_mv/ntu terisi | |
| F5 | Kalibrasi pH | Dashboard: perintah kalibrasi pH | Gateway: `Perintah baru ... (jenis 2)`, `[PROXY] GET ...`; hasil muncul di dashboard (≤ ± 60 dtk) | |
| F6 | Kalibrasi TSS: faktor | Ubah faktor di dashboard | Gateway memberi tahu (jenis 4), node mengambil faktor, `[KALIBRASI TSS] Faktor diperbarui` | |
| F7 | Wizard TSS hardware | Jalankan satu langkah (mis. `cek_status`) | Jenis 3 diberitahukan; hasil PATCH lewat jembatan; dashboard menampilkan hasil | |
| F8 | Alat Modbus | Baca register (INSERT ke `device_commands` type=modbus) | Jenis 1; hasil di kolom `hasil` baris itu | |
| F9 | Perintah lama tidak dijalankan | Ada perintah pending sebelum gateway menyala | Tidak diberitahukan ke node (safety boot) | |
| F10 | Gateway sibuk | Dua permintaan jembatan hampir bersamaan | Yang kedua gagal cepat (-1), diulang pada siklus berikutnya; tidak ada data hilang diam-diam | |

Catatan batasan (E9): sampel per titik kalibrasi pH/TSS dan pesan progres kalibrasi **tidak** dikirim lewat LoRa
(terlalu banyak paket); hanya perintah, hasil akhir, dan faktor. Heartbeat/level/hujan diganti ping status.
