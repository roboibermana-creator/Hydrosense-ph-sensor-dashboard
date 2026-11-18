# Rain Gauge Tipping Bucket HydroSense

Dokumen ini menjelaskan persamaan yang dipakai firmware ([src/Rain.cpp](src/Rain.cpp)),
cara mengkalibrasi, dan batasan sensornya.

## 1. Prinsip

Corong menampung hujan dan mengalirkannya ke ember kecil berpasangan. Setiap kali ember
terisi sebesar volume tertentu, ia terjungkit dan kontak reed menutup sekali (satu
"ketukan"). Jumlah ketukan sebanding dengan tinggi hujan.

## 2. Persamaan

**Tinggi hujan per ketukan** (kedalaman air di atas luas corong):

```
h_tip [mm] = V_tip [mm³] / A [mm²]          (1 mL = 1000 mm³)
A = p · l                                   corong persegi panjang (sensor ini: bukaan dalam ≈ 110 × 50 mm)
A = π · (d / 2)²                            corong bulat, d = diameter
```

Contoh sensor ini: yang menangkap hujan adalah bukaan DALAM, bukan ukuran bodi luar
(118 × 59 mm). Perkiraan dari foto: 110 × 50 = 5.500 mm² (55 cm²). Dengan resolusi
spesifikasi h_tip = 0,28 mm, ember terjungkit tiap V_tip = 0,28 × 5.500 = 1.540 mm³ ≈ 1,54 mL.
100 mL air = 18,18 mm = ± 65 ketukan.

Dasarnya: 1 mm hujan di atas 1 m² sama dengan 1 liter air.

**Tinggi hujan dalam selang waktu:**

```
R [mm] = N · h_tip                          N = jumlah ketukan
```

**Intensitas rata-rata (mm/jam):**

```
I [mm/jam] = R / Δt [jam] = N · h_tip · (60 / Δt [menit])
```

## 3. Yang dihitung firmware

| Nilai di dashboard | Rumus di firmware |
|---|---|
| **mm/jam** (`rain_mm_per_hour`) | jumlah ketukan dalam **60 menit terakhir** × `RAIN_MM_PER_TIP` (jendela bergulir, 60 ember per menit) |
| **Hari ini** (`rain_mm_today`) | ketukan sejak 00:00 WIB × `RAIN_MM_PER_TIP`, disimpan di NVS dan dinolkan tengah malam |

Karena jendelanya satu jam penuh, **mm/jam = curah hujan yang jatuh dalam 1 jam terakhir**.
Ini bukan intensitas sesaat. Contoh: 191 ketukan dalam satu menit (hasil uji dengan
digoyang) tampil 38,2 mm/jam karena total 38,2 mm tercatat dalam jam itu, padahal
intensitas sesaatnya setara 2.292 mm/jam. Itu mustahil untuk hujan nyata, jadi tidak
ada yang salah dengan sensor.

Kategori di dashboard (per jam, mengikuti klasifikasi BMKG):
< 0,5 tidak hujan · 0,5-5 ringan · 5-10 sedang · 10-20 lebat · > 20 sangat lebat.
(Ambang ini ada di [rain.js](dashboardmonitoringhydrosense/js/rain.js); cek ulang
dengan publikasi BMKG terbaru bila dipakai di laporan resmi.)

## 4. Kalibrasi `RAIN_MM_PER_TIP`

Spesifikasi menyebut 0,28 mm, tetapi uji 300 mL (lihat bawah) memberi 0,39 mm. Cara memastikan:

1. Siapkan volume air yang diketahui, misalnya V = 500 mL. Luas tangkapan A ada di `RAIN_LUAS_CORONG_MM2` (Config.h, sekarang 110 × 50 mm). **Ukur ulang** bukaan dalam dengan penggaris; kesalahan luas langsung menjadi kesalahan hasil.
2. Tuang **perlahan** (sekitar 5-10 menit) ke corong dan catat jumlah ketukan N dari Serial Monitor.
3. Hitung:
   ```
   h_tip = V [mm³] / (A [mm²] · N)
   ```
   Contoh: 500 mL = 500.000 mm³, A = 5.500 mm², N = 325 → h_tip = 0,280 mm.
   (500 mL setara 90,9 mm hujan di atas bukaan; itu normal untuk uji kalibrasi.)
4. Isi `RAIN_MM_PER_TIP` di [include/Config.h](include/Config.h), lalu upload ulang.

**Tanpa hitung manual, lewat Serial Monitor** (115200, kirim satu huruf lalu Enter):

| Huruf | Fungsi |
|---|---|
| `Y` | Tampilkan status: ketukan, mm/ketukan, total hari ini |
| `Z` | Nolkan total hujan hari ini |
| `M` | Set mm per ketukan; ketik angka (mis. `0.2`) lalu Enter |
| `B` lalu `E` | Kalibrasi air takaran: `B` mulai, tuang air, `E` selesai. Firmware meminta volume (mL), memakai luas corong dari Config.h, menghitung `h_tip` dengan rumus di atas, lalu menyimpannya |

Nilai dari `M` atau `E` disimpan di NVS dan menimpa `RAIN_MM_PER_TIP` di `Config.h`
tanpa upload ulang.

Tuang pelan karena kalau terlalu cepat, sebagian air lolos saat ember menjungkit
(lihat bagian 5) dan hasilnya terbaca terlalu kecil.

### Hasil kalibrasi pertama (300 mL)

Uji tanggal 8 Nov 2026 dengan botol berlubang kecil di atas corong. Saat itu alat masih
memakai firmware lama dengan `RAIN_MM_PER_TIP` = 0,2 mm, sehingga jumlah ketukan bisa
dipulihkan dari angka dashboard.

**Data**

| Besaran | Nilai |
|---|---|
| Volume air dituang, V | 300 mL = 300.000 mm³ |
| Luas tangkapan, A | 110 × 50 mm = 5.500 mm² (perkiraan dari foto) |
| Dashboard "hari ini" | 27,8 mm |
| Dashboard "mm/jam" | 12,0 mm/jam |
| Pengali firmware saat uji | 0,2 mm/ketukan |

**Langkah hitung**

1. Pulihkan jumlah ketukan dari angka dashboard. Karena pengalinya 0,2 mm:
   - N hari ini = 27,8 / 0,2 = **139 ketukan** (bilangan bulat tepat).
   - N jendela 1 jam = 12,0 / 0,2 = 60 ketukan, juga bulat.
   - Kalau pengalinya 0,28, angka 27,8 mustahil muncul (99 ketukan jadi 27,7; 100 ketukan jadi 28,0). Ini bukti alat masih memakai 0,2.
   - Angka mm/jam lebih kecil karena itu jendela 60 menit terakhir, dan tuangan berlangsung lebih dari satu jam.
2. Hitung mm per ketukan:
   ```
   h_tip = V / (A · N) = 300.000 / (5.500 × 139) = 300.000 / 764.500 = 0,392 mm
   ```
3. Volume air per ketukan = 0,392 × 5.500 = 2.158 mm³ ≈ **2,16 mL** (spesifikasi menyebut 0,28 mm ≈ 1,54 mL).
4. Cek silang: 139 × 0,39 = 54,2 mm, dekat dengan 300 mL / 5.500 mm² = 54,5 mm.

**Hasil:** `RAIN_MM_PER_TIP` = **0,39 mm** (sudah diisi di `Config.h`).

**Asumsi (hasil hanya sebaik ini)**
- Seluruh 300 mL masuk ke corong. Sisa air di botol akan menurunkan nilai sebenarnya. Contoh: bila 20 mL tertinggal, V = 280 mL → h_tip ≈ 0,366 mm.
- Seluruh 139 ketukan berasal dari tuangan itu (total hari ini ditolkan sebelum uji).
- Luas 5.500 mm² benar. Sensitivitas: bila luas sebenarnya 5.000 mm², h_tip = 0,432 mm; bila 6.000 mm², h_tip = 0,360 mm. Ukur bibir dalam corong dengan penggaris.
- Satu uji saja. Ulangi sekali (mis. 250-500 mL) dan bandingkan: dua hasil yang berbeda
  lebih dari ± 3% menandakan masalah pada cara menuang atau pada luas.
- Riwayat hujan di dashboard sebelum pengali diubah tersimpan dengan 0,2 mm/ketukan,
  jadi nilainya lebih kecil dari yang sebenarnya dan tidak ikut diperbarui otomatis.

## 5. Batasan tipping bucket

- **Resolusi** sama dengan h_tip (mis. 0,28 mm). Hujan gerimis yang sangat lemah baru
  tercatat setelah ember penuh, jadi angka naik bertahap, tidak mulus.
- **Meremehkan pada hujan deras:** saat ember menjungkit, air yang masuk di waktu itu
  tidak terhitung. Makin deras hujan, makin besar kekurangannya. Literatur
  menyajikan kalibrasi dinamis dan koreksi intensitas untuk ini (mis. Calder & Kidd,
  1978; Marsalek, 1981). Koreksi seperti itu belum dipasang di firmware.
- **Penguapan dan tersumbat:** daun atau kotoran di corong membuat angka terlalu
  kecil. Bersihkan corong secara berkala.
- **Pemasangan:** gauge harus datar dan di tempat terbuka. Standar pengukuran presipitasi
  secara umum ada di WMO *Guide to Instruments and Methods of Observation* (WMO-No. 8),
  bab pengukuran presipitasi.

> Catatan: nama referensi di atas saya tuliskan dari ingatan dan **belum saya
> verifikasi** ke sumber aslinya. Cek judul, tahun, dan isinya sebelum dikutip di
> laporan.

## 6. Pin dan wiring

Kabel sensor ke **GPIO4** dan GND (pull-up internal, 3,3 V saja). Lihat
[WIRING.md](WIRING.md).
