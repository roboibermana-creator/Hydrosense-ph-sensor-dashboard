-- HydroSense: hubungkan device_id (teks) di tabel Hydrosense ke devices.name dengan
-- foreign key sungguhan. Efeknya Schema Visualizer Supabase bisa menggambar garis dan
-- menata tabelnya, bukan menumpuk lurus.
-- Jalankan SETELAH 0001_schema.sql. Aman dijalankan ulang.
--
-- Supaya data tidak ditolak, alat yang belum terdaftar didaftarkan OTOMATIS ke
-- `devices` (project 'hydrosense') lewat trigger saat baris pertamanya masuk.

-- 1. devices.name harus unik supaya bisa jadi sasaran foreign key.
--    Kalau sudah ada nama kembar, langkah ini berhenti dengan pesan, tidak mengubah apa pun.
do $$
begin
  if exists (select 1 from public.devices group by name having count(*) > 1) then
    raise exception 'devices.name ada yang kembar; rapikan dulu sebelum menjalankan 0002 (select name, count(*) from public.devices group by name having count(*) > 1)';
  end if;
  if not exists (select 1 from pg_constraint where conname = 'devices_name_key') then
    alter table public.devices add constraint devices_name_key unique (name);
  end if;
end $$;

-- 2. Daftarkan alat otomatis saat baris dengan device_id baru masuk.
create or replace function public.ensure_device() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if new.device_id is not null then
    insert into public.devices (name, project)
    values (new.device_id, 'hydrosense')
    on conflict (name) do nothing;
  end if;
  return new;
end $$;

-- 3. Pasang trigger + foreign key di 9 tabel.
do $$
declare
  t text;
begin
  foreach t in array array[
    'system_control','device_status','auto_schedule','modbus_cmd','ph_calibration_cmd',
    'calibration_samples','tss_calibration_config','tss_calibration_samples','tss_hw_calibration_cmd'
  ] loop
    -- daftarkan device_id yang sudah ada di tabel (kalau ada baris lama)
    execute format(
      'insert into public.devices (name, project)
         select distinct device_id, ''hydrosense'' from public.%I where device_id is not null
         on conflict (name) do nothing', t);

    execute format('drop trigger if exists %I on public.%I', t || '_ensure_device', t);
    execute format(
      'create trigger %I before insert or update of device_id on public.%I
         for each row execute function public.ensure_device()', t || '_ensure_device', t);

    execute format('alter table public.%I drop constraint if exists %I', t, t || '_device_id_name_fkey');
    execute format(
      'alter table public.%I add constraint %I foreign key (device_id)
         references public.devices (name) on update cascade', t, t || '_device_id_name_fkey');
  end loop;
end $$;
