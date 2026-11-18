-- Jalankan di project "Groundtank monitoring" SEBELUM 0005.
-- Membuang 11 tabel Hydrosense versi lama (hasil 0001) yang masih KOSONG, lalu
-- menambah kolom baru ke "Hydrosense table". Penghapusan dijaga: kalau ada tabel yang
-- ternyata sudah berisi baris, seluruh skrip berhenti dengan error dan tidak ada
-- yang dihapus. tank_readings dan tabel groundtank lain TIDAK disentuh.
do $$
declare t text; n bigint;
begin
  foreach t in array array[
    'auto_schedule','calibration_samples','device_status','modbus_cmd',
    'ph_calibration_cmd','rain_gauge_readings','system_control',
    'tss_calibration_config','tss_calibration_samples','tss_hw_calibration_cmd',
    'water_level_readings'] loop
    if to_regclass('public.' || t) is not null then
      execute format('select count(*) from public.%I', t) into n;
      if n > 0 then
        raise exception 'TIDAK dihapus: % berisi % baris', t, n;
      end if;
    end if;
  end loop;

  foreach t in array array[
    'auto_schedule','calibration_samples','device_status','modbus_cmd',
    'ph_calibration_cmd','rain_gauge_readings','system_control',
    'tss_calibration_config','tss_calibration_samples','tss_hw_calibration_cmd',
    'water_level_readings'] loop
    execute format('drop table if exists public.%I', t);
  end loop;
end $$;

-- "Hydrosense table" versi lama belum punya kolom level air & hujan; 0005 hanya
-- membuat tabel kalau belum ada, jadi kolomnya ditambah di sini.
alter table public."Hydrosense table"
  add column if not exists distance_cm numeric,
  add column if not exists rain_mm_per_hour numeric,
  add column if not exists rain_mm_today numeric;
