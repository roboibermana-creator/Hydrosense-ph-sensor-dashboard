-- HydroSense: gabungkan 6 tabel lagi. Jalankan SETELAH 0003. Hanya tabel Hydrosense.
--   water_level_readings + rain_gauge_readings -> "Hydrosense table" (kolom baru)
--   system_control                             -> device_commands (type 'control')
--   auto_schedule                              -> device_status (kolom jadwal)
--   calibration_samples + tss_calibration_config -> calibration
-- Data lama disalin, lalu tabel lama dihapus. Aman dijalankan ulang.
-- Firmware (src/Network.cpp) dan dashboard (index.html) harus yang sudah diperbarui.
--
-- Hasil akhir: "Hydrosense table", device_status, device_commands, calibration.
--
-- Kenapa system_control TIDAK digabung dengan auto_schedule: firmware mendeteksi
-- perintah baru lewat id baris yang terus bertambah (pengaman boot di ModeManual.cpp),
-- jadi perintah kontrol harus tetap berupa log (insert baris baru), bukan 1 baris.

-- =====================================================================
--  1. Pembacaan: level air + hujan masuk "Hydrosense table"
--  Baris level-only: water_level + distance_cm. Baris hujan: rain_*.
--  Dashboard memfilter baris per jenis dengan "kolom X tidak null".
-- =====================================================================
alter table public."Hydrosense table"
  add column if not exists distance_cm numeric,
  add column if not exists rain_mm_per_hour numeric,
  add column if not exists rain_mm_today numeric;

do $$
begin
  if to_regclass('public.water_level_readings') is not null then
    insert into public."Hydrosense table" ("timestamp", water_level, distance_cm)
    select "timestamp", water_level, distance_cm
      from public.water_level_readings order by id;
    drop table public.water_level_readings;
  end if;

  if to_regclass('public.rain_gauge_readings') is not null then
    insert into public."Hydrosense table" ("timestamp", rain_mm_per_hour, rain_mm_today)
    select "timestamp", rain_mm_per_hour, rain_mm_today
      from public.rain_gauge_readings order by id;
    drop table public.rain_gauge_readings;
  end if;
end $$;

-- =====================================================================
--  2. Kontrol mode/valve/pompa: system_control -> device_commands
--  type 'control', params {mode, valve_cmd, pump_cmd}, status 'aktif'.
-- =====================================================================
alter table public.device_commands drop constraint if exists device_commands_type_check;
alter table public.device_commands
  add constraint device_commands_type_check
  check (type in ('modbus', 'ph_cal', 'tss_hw', 'control'));

do $$
begin
  if to_regclass('public.system_control') is not null then
    insert into public.device_commands (device_id, type, params, status, created_at)
    select device_id, 'control',
           jsonb_build_object('mode', mode, 'valve_cmd', valve_cmd, 'pump_cmd', pump_cmd),
           'aktif', created_at
      from public.system_control order by id;
    drop table public.system_control;
  end if;
end $$;

-- =====================================================================
--  3. Jadwal otomatis: auto_schedule -> kolom di device_status
--  Jangan sentuh updated_at di sini (dipakai dashboard untuk status pompa basi).
-- =====================================================================
alter table public.device_status
  add column if not exists interval_detik integer,
  add column if not exists pompa_detik integer,
  add column if not exists valve_detik integer,
  add column if not exists baca_sensor_detik integer,
  add column if not exists stabil_detik integer,
  add column if not exists sampel_ms integer,
  add column if not exists trim_persen integer;

do $$
begin
  if to_regclass('public.auto_schedule') is not null then
    insert into public.device_status
      (device_id, interval_detik, pompa_detik, valve_detik, baca_sensor_detik,
       stabil_detik, sampel_ms, trim_persen)
    select device_id, interval_detik, pompa_detik, valve_detik, baca_sensor_detik,
           stabil_detik, sampel_ms, trim_persen
      from public.auto_schedule
    on conflict (device_id) do update set
      interval_detik    = excluded.interval_detik,
      pompa_detik       = excluded.pompa_detik,
      valve_detik       = excluded.valve_detik,
      baca_sensor_detik = excluded.baca_sensor_detik,
      stabil_detik      = excluded.stabil_detik,
      sampel_ms         = excluded.sampel_ms,
      trim_persen       = excluded.trim_persen;
    drop table public.auto_schedule;
  end if;
end $$;

-- device_status sebelumnya hanya boleh insert/update oleh anon; dashboard juga upsert
-- jadwal, dan policy yang ada sudah mengizinkannya.

-- =====================================================================
--  4. Kalibrasi: calibration_samples + tss_calibration_config -> calibration
--  aksi 'kalibrasi'/'verifikasi' = sampel; aksi 'faktor' = faktor aktif (baris
--  terbaru per sensor): nilai slope/cal_offset/source/points_count terisi.
-- =====================================================================
do $$
begin
  if to_regclass('public.calibration_samples') is not null
     and to_regclass('public.calibration') is null then
    alter table public.calibration_samples rename to calibration;
    alter index if exists public.calibration_samples_lookup_idx
      rename to calibration_lookup_idx;
  end if;
end $$;

alter table public.calibration
  add column if not exists slope numeric,
  add column if not exists cal_offset numeric,
  add column if not exists source text,
  add column if not exists points_count integer;

do $$
begin
  if to_regclass('public.tss_calibration_config') is not null then
    insert into public.calibration
      (device_id, sensor, aksi, slope, cal_offset, source, points_count, created_at)
    select device_id, 'tss', 'faktor', slope, cal_offset, source, points_count, created_at
      from public.tss_calibration_config order by id;
    drop table public.tss_calibration_config;
  end if;
end $$;
