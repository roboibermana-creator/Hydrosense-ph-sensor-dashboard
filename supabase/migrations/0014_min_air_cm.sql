-- Batas tinggi air kolam, diatur dari menu KALIBRASI dashboard:
--   min_air_cm : tinggi air minimal (cm); di bawahnya siklus otomatis ditolak ("air kolam kering")
--   max_air_cm : tinggi maksimum (cm) = jarak sensor ke dasar kolam (dasar hitung tinggi air)
-- Alat membaca kolom ini tiap ~30 dtk (Kolam.cpp). NULL = pakai nilai lokal alat
-- (Serial 'J' / default KOLAM_MIN_AIR_CM & TANK_HEIGHT_CM di Config.h).
-- JALANKAN MANUAL di SQL Editor project vupbropaxsqireeullbm (konektor MCP tidak bisa mengakses
-- project itu). Tanpa migrasi ini alat hanya mencetak peringatan [KOLAM]; fungsi lain tidak terpengaruh.
alter table public.devices
  add column if not exists min_air_cm numeric,
  add column if not exists max_air_cm numeric;

-- Hanya operator yang boleh mengubah batas ini (sama seperti jadwal otomatis).
create or replace function public.guard_devices() returns trigger
language plpgsql as $$
begin
  if not public.is_api_role() or public.is_operator() then
    return new;
  end if;
  if (new.interval_detik, new.pompa_detik, new.valve_detik, new.baca_sensor_detik,
      new.stabil_detik, new.sampel_ms, new.trim_persen, new.name, new.project, new.min_air_cm, new.max_air_cm)
     is distinct from
     (old.interval_detik, old.pompa_detik, old.valve_detik, old.baca_sensor_detik,
      old.stabil_detik, old.sampel_ms, old.trim_persen, old.name, old.project, old.min_air_cm, old.max_air_cm) then
    raise exception 'Hanya operator yang boleh mengubah jadwal, batas/tinggi air, atau identitas alat'
      using errcode = '42501';
  end if;
  return new;
end $$;
