# Wiring HydroSense (ESP32-S3)

Sumber pin: `include/Config.h`.

## Arsitektur RS485

```
ESP32-S3 ──(TTL)── Modul konverter RS485 ──(A/B)── Sensor pH (ID 1) + TSS (ID 2)
```

Sensor pH dan TSS hanya memiliki kabel VCC, GND, A, dan B. Pin RO, DI, RE, dan DE berada di modul MAX485, bukan di sensor.

## 1. RS485: sensor pH dan TSS (`Serial2`, 9600 8N1)

### Opsi A: modul MAX485 (memiliki RO/DI/RE/DE)

| MAX485 | ESP32-S3 |
|---|---|
| RO | GPIO16 |
| DI | GPIO15 |
| RE + DE (dijumper) | GPIO14 |
| VCC / GND | 3V3 / GND |
| A / B | Ke A/B sensor pH dan TSS (paralel) |

### Opsi B: modul auto-direction (hanya TXD/RXD)

| Modul | ESP32-S3 |
|---|---|
| TXD | GPIO16 (RX ESP) |
| RXD | GPIO15 (TX ESP) |
| VCC / GND | 3V3 / GND |

Pada opsi ini GPIO14 tidak disambungkan.

### Sisi sensor

| Kabel sensor | Ke |
|---|---|
| VCC (umumnya 12–24V, cek label) | Power supply 12/24V, **bukan** dari ESP32 |
| GND | GND supply (disatukan dengan GND ESP32) |
| A (RS485+) | Terminal A modul |
| B (RS485−) | Terminal B modul |

## 2. Rain gauge tipping bucket (kontak reed, langsung ke pin)

Modul DFRobot SEN0575 rusak dan tidak dipakai. Sensor tipping bucket disambung langsung:

| Kabel sensor | ESP32-S3 |
|---|---|
| Kabel 1 | GPIO4 |
| Kabel 2 | GND |

- Memakai pull-up internal ESP32 (3V3 saja, jangan beri 5 V). Tidak perlu resistor.
- Kabel panjang (lebih dari beberapa meter): tambahkan kapasitor 100 nF antara GPIO4 dan GND.
- Isi `RAIN_MM_PER_TIP` di `Config.h` sesuai sensor (bawaan 0,2 mm per ketukan).

## 3. GSM/4G SIMCom SIM7600 (UART, `Serial1`)

Cadangan internet bila WiFi putus. Sambungan TX/RX **bersilang** (TXD modem ke RX ESP).

| Pin modem | ESP32-S3 |
|---|---|
| RXD | GPIO18 (TX ESP) |
| TXD | GPIO1 (RX ESP) |
| PWRKEY | GPIO48 |
| GND | GND bersama |
| VCC | 5 V terpisah, arus puncak ~2 A (atau lewat micro-USB board modem; jangan dobel) |

- Kartu SIM Telkomsel dengan paket data, APN `internet` (`GSM_APN` di `Config.h`).
- Antena LTE wajib terpasang sebelum modem dinyalakan. Antena GNSS (GPS) ke port GNSS.
- Perintah Serial `U` menampilkan status modem.

## 4. Aktuator

| Perangkat | ESP32-S3 | Catatan |
|---|---|---|
| Relay pompa peristaltik | GPIO6 | Active LOW |
| Relay valve, arah BUKA | GPIO47 | |
| Relay valve, arah TUTUP | GPIO21 | |

## 5. Sensor lain

| Perangkat | ESP32-S3 | Catatan |
|---|---|---|
| Float switch, kuning 1 | GPIO5 | INPUT_PULLUP |
| Float switch, kuning 2 | GND | |
| Radar 4-20mA, converter DFRobot SEN0262 (OUT, kabel biru) | GPIO9 | Sensor level. Keluaran converter maks 3,3 V (4 mA = 0,48 V, 20 mA = 2,4 V) |
| Converter SEN0262 (VCC, merah) | 3V3 | |
| Converter SEN0262 (GND, hitam) | GND | GND bersama dengan minus catu 24 V |

## 6. Indikator

| Perangkat | ESP32-S3 |
|---|---|
| LED error | GPIO2 |

## 7. SD card (antrean data offline)

Slot microSD di Shield ESP32-S3 Pro, SPI (silkscreen "TF Card Pinout").

| Sinyal | ESP32-S3 |
|---|---|
| CS (SS) | GPIO40 |
| MOSI | GPIO11 |
| SCK | GPIO12 |
| MISO | GPIO13 |


Format kartu FAT32. File: `/queue.jsonl` (antrean) dan `/queue.pos` (posisi terkirim).

## Peta pin

```
GPIO1  GSM RX (dari TXD)   GPIO14 RS485 RE/DE   GPIO21 Valve TUTUP
GPIO2  LED error           GPIO15 RS485 TX      GPIO47 Valve BUKA
GPIO4  Rain gauge          GPIO16 RS485 RX      GPIO48 GSM PWRKEY
GPIO5  Float switch        GPIO18 GSM TX (ke RXD)
GPIO6  Pompa               GPIO40 SD CS (SPI 11/12/13)
GPIO9  ADC radar 4-20 mA   GPIO3  entropi TLS GSM (biarkan kosong)
```

- **Cadangan:** 7, 8, 10, 17, 38, 39, 41, 42
- **Jangan dipakai:** 0, 3, 45, 46 (strapping), 19/20 (USB D-/D+), 26–32 (flash), 35–37 (PSRAM octal)

## Catatan penting

- **GND bersama:** semua GND (ESP32, relay, modul RS485, rain gauge, modem GSM, power supply) harus disatukan.
- **Supply modem GSM:** SIM7600 butuh 5 V dengan arus puncak ~2 A. Pin 3V3/5V board tidak cukup.
- **Interlock pompa:** interlock pompa manual saat ini nonaktif (`INTERLOCK_POMPA_MANUAL 0`). Aktifkan kembali setelah float switch terpasang.
