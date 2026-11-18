# HydroSense

Sistem monitoring dan kontrol kolam berbasis ESP32-S3 (komunikasi GSM SIM7600) dengan dashboard web dan backend Supabase.

## Struktur folder

| Folder / file | Isi |
|---|---|
| `src/`, `include/` | Firmware ESP32-S3 (PlatformIO, framework Arduino) |
| `platformio.ini` | Konfigurasi build; env `esp32-s3-devkitc-1` (USB) dan `ota` (WiFi) |
| `dashboardmonitoringhydrosense/` | Dashboard web statis (HTML/CSS/JS modul, Supabase via CDN). Punya repo git sendiri |
| `supabase/migrations/` | Skema database, urutan `0001` sampai `0014` |
| `docs/` | Wiring, register sensor TSS, rain gauge, flowchart dan state machine |
| `tools/` | Skrip diagnosa sensor TSS lewat USB-RS485 (Python, tanpa ESP32) |
| `test/` | Backtest pipeline (Python) |

## Build firmware

```
pio run                      # build
pio run -t upload            # upload via USB
pio run -e ota -t upload     # upload via WiFi (password = OTA_PASSWORD di include/Config.h)
```

## Konfigurasi

- `include/Config.h`: ID perangkat, pin, kredensial jaringan/Supabase, password OTA.
- `.env.example`: daftar variabel lingkungan. Salin ke `.env` dan isi sendiri. Jangan commit `.env`.

## Dokumentasi

Lihat folder `docs/`: `WIRING.md`, `REGISTER_TSS.md`, `RAINGAUGE.md`, `TEST_FLOWCHART.md`, serta `state_machine.workflow.json` yang menjadi acuan lane pada `src/Mode*.cpp`.
