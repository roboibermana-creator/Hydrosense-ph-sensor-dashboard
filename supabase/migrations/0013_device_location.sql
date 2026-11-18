-- Lokasi alat HydroSense dari GNSS (GPS) bawaan modem SIM7600, untuk kartu "Lokasi Alat" di dashboard.
-- Diisi node (PATCH ke baris perangkat) saat dapat fix, lalu tiap 6 jam.
--   latitude / longitude : derajat desimal (WGS84)
--   altitude_m           : ketinggian (m), boleh NULL
--   location_at          : waktu fix terakhir dilaporkan
-- JALANKAN MANUAL di SQL Editor project vupbropaxsqireeullbm (konektor MCP tidak bisa mengakses
-- project itu) SEBELUM firmware yang baru dipakai. Tanpa migrasi ini hanya pengiriman posisi yang
-- ditolak (node mencetak peringatan [GPS]); fungsi lain tidak terpengaruh.
alter table public.devices
  add column if not exists latitude    double precision,
  add column if not exists longitude   double precision,
  add column if not exists altitude_m  double precision,
  add column if not exists location_at timestamptz;
