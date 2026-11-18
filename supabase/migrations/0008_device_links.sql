-- Hubungkan device_id (teks) di calibration dan device_commands ke devices.name dengan
-- foreign key, supaya Schema Visualizer menggambar garisnya. Jalankan di project BARU,
-- setelah 0005 (dan 0007). Aman dijalankan ulang.
--
-- Efek ke tabel groundtank: `devices.name` jadi UNIK (satu nama = satu alat, lintas
-- project). Kalau sudah ada nama kembar, langkah 1 berhenti dengan pesan dan tidak ada
-- yang berubah.
--
-- Supaya data tidak ditolak, alat yang belum terdaftar didaftarkan OTOMATIS ke
-- `devices` (project 'hydrosense') oleh trigger saat baris pertamanya masuk.

-- 1. devices.name harus unik supaya bisa jadi sasaran foreign key.
do $$
begin
  if exists (select 1 from public.devices group by name having count(*) > 1) then
    raise exception 'devices.name ada yang kembar; rapikan dulu (select name, count(*) from public.devices group by name having count(*) > 1)';
  end if;
  if not exists (
    select 1 from pg_constraint
    where conrelid = 'public.devices'::regclass and contype in ('u', 'p')
      and conkey = array[(select attnum from pg_attribute
                          where attrelid = 'public.devices'::regclass and attname = 'name')]
  ) and not exists (
    select 1 from pg_indexes
    where schemaname = 'public' and tablename = 'devices' and indexdef ilike '%unique%(name)%'
  ) then
    alter table public.devices add constraint devices_name_key unique (name);
  end if;
end $$;

-- 2. Daftarkan alat otomatis saat baris dengan device_id baru masuk.
create or replace function public.hydrosense_ensure_device() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  if new.device_id is not null then
    insert into public.devices (name, project)
    values (new.device_id, 'hydrosense')
    on conflict (name) do nothing;
  end if;
  return new;
end $$;

-- 3. Trigger + foreign key di calibration dan device_commands.
do $$
declare t text;
begin
  foreach t in array array['calibration', 'device_commands'] loop
    -- daftarkan device_id yang sudah ada di tabel (baris hasil impor)
    execute format(
      'insert into public.devices (name, project)
         select distinct device_id, ''hydrosense'' from public.%I where device_id is not null
         on conflict (name) do nothing', t);

    execute format('drop trigger if exists %I on public.%I', t || '_ensure_device', t);
    execute format(
      'create trigger %I before insert or update of device_id on public.%I
         for each row execute function public.hydrosense_ensure_device()',
      t || '_ensure_device', t);

    execute format('alter table public.%I drop constraint if exists %I', t, t || '_device_id_name_fkey');
    execute format(
      'alter table public.%I add constraint %I foreign key (device_id)
         references public.devices (name) on update cascade', t, t || '_device_id_name_fkey');
  end loop;
end $$;
