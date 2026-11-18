# Peta Register Sensor TSS (RD-SSBWT-01, Modbus RTU slave ID 2)

Hasil pindai langsung ke sensor (1-3 Nov 2026) lewat `modbus_cmd` dan `tss_hw_calibration_cmd`.
Alamat 0-based (gaya Modbus Poll). Float32 = **CDAB** (word bawah dulu).
Peta sensor ini **bergeser -2** dari manual OEM untuk register baca (TSS di 0-1, bukan 2-3).

Status: **TERBUKTI** = diuji langsung, **DUGAAN** = masuk akal tapi belum diuji, **TIDAK ADA** = diuji dan tidak ada.

## FC03 - Holding register (baca/tulis)

| Alamat | Isi | Format | Status | Keterangan |
|---|---|---|---|---|
| 0-1 | TSS | Float32 CDAB | TERBUKTI | mg/L, sudah dikali faktor |
| 2-3 | Suhu air | Float32 CDAB | TERBUKTI | derajat C, sekitar 27-30 |
| 4-5 | Faktor kalibrasi | Float32 CDAB | TERBUKTI | sekarang 1.0 (`0,16256`). Faktor 2.0 -> TSS x2, 3.0 -> TSS x3. Tulis pakai FC16. |
| 6-7 | Nilai float tambahan | Float32 CDAB | DUGAAN | berubah-ubah (1526, 386), tidak sebanding TSS. Mungkin turbidity (NTU) |
| 8-10 | - | - | - | selalu 0 |
| 11 | Interval sapu otomatis | UInt16 | TERBUKTI | menit, default **60**. Cermin baca dari alamat tulis 21 |
| 12 | Response time | UInt16 | DUGAAN | detik, isi 4 (manual: 3-60) |
| 13 | Parameter pengukuran | UInt16 | TERBUKTI | **1 = turbidity, 2 = suspended solids (TSS)**. Sekarang 2 |
| 14 | Kelembaban probe | UInt16 | DUGAAN | isi 0 (manual: sebaiknya < 10) |
| 15 | - | - | - | selalu 0 |
| 16-111 | - | - | - | semua 0 |
| 112-191 | Tabel kalibrasi pabrik | Float32 | TERBUKTI ada | titik 100, 500, 1000, 10000, 20000, 50000, 80000, 100000 mg/L + koefisien (ada nilai 1.0 di 122-123 dan 126-127) |
| 192-399 | - | - | - | semua 0 |
| 400-500 | Salinan tabel kalibrasi | Float32 | TERBUKTI ada | salinan dari 112-191 |
| 496-1000, 4096-4127, 8192, 12288, 16384 | - | - | - | semua 0 |

## FC04 - Input register (hanya baca)

| Alamat | Isi | Status |
|---|---|---|
| 0-1 | 2.0 (tetap) | TERBUKTI (arti belum diketahui) |
| 2-3 | Suhu air (float) | TERBUKTI |
| 4-5 | Nilai ~255 mirip TSS (float) | DUGAAN |
| 6-7, 8-9, 10-11 | Tiga nilai float ratusan-ribuan, berubah-ubah | DUGAAN (pembacaan tambahan, mungkin turbidity) |
| 12-13 | Salinan 4-5 | TERBUKTI |
| 14-15 | Salinan 2-3 | TERBUKTI |
| 16-47 | - | semua 0 |

## FC01 / FC02 (coil, discrete input)

**TIDAK ADA** - timeout 0xE2, sensor tidak menjawab.

## Perilaku tulis yang sudah teramati

| Aksi | Hasil |
|---|---|
| Tulis ke 4-5 (FC16, float) | **Berfungsi**, mengubah faktor |
| Tulis 1 ke alamat **21** (0x15) | Mengubah register baca **11** dari 60 ke 1 (interval sapu 1 menit). Dipulihkan dengan tulis 60 ke 21, terverifikasi |
| Tulis 66 ke 18, 19, 20, 21, 22, 23, 26 | ACK tapi **tidak ada efek terlihat**, wiper tidak menyapu |
| Tulis 1 atau 66 ke 8, 9, 10 | ACK tanpa efek |
| Tulis 60 ke 11 | ACK tanpa efek (11 hanya cermin baca) |
| Alamat 16-17 (baud/ID) | Diblokir di firmware, jangan ditulis |

Kesimpulan: alamat tulis manual OEM (20-28) bisa valid walau hasilnya muncul di alamat baca yang berbeda.

## Yang BELUM ketemu

- **Wiper manual:** tidak ada register yang menggerakkan wiper (fitur wiper dihapus dari firmware dan dashboard).
- **Mode kalibrasi (1 = faktor, 2 = 2 titik, 3 = 4 titik):** kode memakai 27 (0x1B), kandidat lain 25. Terbaca 0, belum diuji tulis.
- **Titik kalibrasi** (kode: 28), **target** (kode: 20-21), **actual** (kode: 22-23): tidak terbukti dan bentrok dengan alamat 21 yang ternyata interval sapu.
- Arti pasti register 6-7 (FC03) dan 4-11 (FC04).

## Aturan keamanan

- Tulis ke sensor hanya lewat fungsi `modbus_tulis(pin, ...)` dengan PIN.
- Jangan tulis ke 16-17 (baud/ID sensor).
- Faktor di 4-5 sebaiknya dibiarkan 1.0 kecuali sengaja dikalibrasi.
- Perintah pertama setelah alat boot selalu kedaluwarsa, kirim satu perintah baca dummy dulu.
