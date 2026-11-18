-- HydroSense: login kata sandi per peran + penguncian penulisan di sisi database.
-- Jalankan di project Supabase TUJUAN, SETELAH 0005 (dan 0007/0008 kalau dipakai).
-- Aman dijalankan ulang.
--
-- Konsep
--   * Dua peran: 'operator' (kontrol penuh) dan 'tamu' (lihat semua menu, tanpa kontrol).
--     Orang yang belum login = 'visitor' (hanya Dashboard Utama).
--   * Tiap peran punya SATU kata sandi bersama. Kata sandinya TIDAK ada di kode web; yang
--     tersimpan hanya hash bcrypt-nya di tabel public.access_keys, yang tidak bisa dibaca
--     lewat API (RLS menyala tanpa policy).
--   * Saat login, dashboard menyimpan kata sandi di browser dan mengirimkannya sebagai
--     header  x-access-key  pada setiap permintaan. Database yang memeriksanya
--     (fungsi request_role) -- jadi pembatasan tulis tetap berlaku walau seseorang
--     memanggil API langsung tanpa lewat dashboard.
--   * Membaca data tetap terbuka (alat dan dashboard memakai kunci anon).
--   * Menulis hal-hal yang mengendalikan alat hanya boleh operator:
--       - kirim perintah (device_commands: control / modbus / ph_cal / tss_hw)
--       - simpan faktor kalibrasi TSS (calibration, aksi = 'faktor')
--       - ubah jadwal otomatis & nama/project alat (devices)
--     Alat (anon, tanpa header) tetap boleh: mengisi hasil perintah, mengirim sampel
--     kalibrasi, mengirim pembacaan sensor, dan memperbarui status/heartbeat di devices.
--   * Penjaga berupa trigger yang hanya berlaku untuk peran API (anon / authenticated);
--     SQL Editor (postgres) dan service_role tidak dibatasi.
--
-- PENTING setelah menjalankan file ini: kata sandi BELUM diisi, jadi tidak ada yang bisa
-- masuk sebagai operator. Isi dengan menjalankan ini di SQL Editor (ganti teksnya, pakai
-- kalimat sandi yang panjang, minimal 16 karakter):
--
--   insert into public.access_keys (role, key_hash) values
--     ('operator', extensions.crypt('GANTI-SANDI-OPERATOR-YANG-PANJANG', extensions.gen_salt('bf', 10))),
--     ('tamu',     extensions.crypt('GANTI-SANDI-TAMU-YANG-PANJANG',     extensions.gen_salt('bf', 10)))
--   on conflict (role) do update set key_hash = excluded.key_hash;
--
-- Mengganti kata sandi = jalankan perintah yang sama dengan teks baru. Browser yang masih
-- memakai kata sandi lama otomatis kembali ke tampilan "belum login".
--
-- Batasan: tidak ada pembatasan jumlah percobaan. Karena itu pakai kata sandi panjang dan
-- acak; kata sandi pendek bisa ditebak lewat percobaan berulang ke API.

create extension if not exists pgcrypto with schema extensions;

-- =====================================================================
--  1. Hash kata sandi per peran (tidak terbaca dari API)
-- =====================================================================
create table if not exists public.access_keys (
  role     text primary key check (role in ('operator', 'tamu')),
  key_hash text not null
);
alter table public.access_keys enable row level security;   -- tanpa policy = tertutup
revoke all on public.access_keys from anon, authenticated;

-- Peran pemanggil menurut header x-access-key: 'operator' | 'tamu' | 'visitor'.
create or replace function public.request_role() returns text
language plpgsql stable security definer set search_path = public, extensions as $$
declare
  k text;
  r text;
begin
  begin
    k := nullif(current_setting('request.headers', true)::json ->> 'x-access-key', '');
  exception when others then
    k := null;
  end;
  if k is null then
    return 'visitor';
  end if;
  select a.role into r
  from public.access_keys a
  where a.key_hash = crypt(k, a.key_hash)
  order by (a.role = 'operator') desc
  limit 1;
  return coalesce(r, 'visitor');
end $$;
revoke all on function public.request_role() from public;
grant execute on function public.request_role() to anon, authenticated;

create or replace function public.is_operator() returns boolean
language sql stable security definer set search_path = public as $$
  select public.request_role() = 'operator';
$$;
revoke all on function public.is_operator() from public;
grant execute on function public.is_operator() to anon, authenticated;

-- Dipanggil dashboard (rpc) untuk mengetahui peran dari kata sandi yang dimasukkan.
create or replace function public.login_role() returns text
language sql stable security definer set search_path = public as $$
  select public.request_role();
$$;
revoke all on function public.login_role() from public;
grant execute on function public.login_role() to anon, authenticated;

-- Kecualikan SQL Editor / service_role: hanya peran API yang dibatasi.
create or replace function public.is_api_role() returns boolean
language sql stable as $$
  select current_user in ('anon', 'authenticated');
$$;

-- =====================================================================
--  2. Penjaga penulisan
-- =====================================================================
-- device_commands: hanya operator yang boleh MEMBUAT perintah. Alat (anon) hanya boleh
-- mengisi hasil (status / hasil / pesan) pada perintah yang ada.
create or replace function public.guard_device_commands() returns trigger
language plpgsql as $$
begin
  if not public.is_api_role() or public.is_operator() then
    return new;
  end if;
  if tg_op = 'INSERT' then
    raise exception 'Hanya operator yang boleh mengirim perintah ke alat'
      using errcode = '42501';
  end if;
  if new.device_id is distinct from old.device_id
     or new.type is distinct from old.type
     or new.perintah is distinct from old.perintah
     or new.params is distinct from old.params
     or new.created_at is distinct from old.created_at then
    raise exception 'Perintah tidak boleh diubah, hanya hasilnya yang boleh diisi'
      using errcode = '42501';
  end if;
  return new;
end $$;

drop trigger if exists device_commands_guard on public.device_commands;
create trigger device_commands_guard before insert or update on public.device_commands
  for each row execute function public.guard_device_commands();

-- calibration: faktor kalibrasi TSS (aksi 'faktor') hanya boleh dibuat operator.
-- Sampel (aksi 'kalibrasi' / 'verifikasi') tetap boleh dari alat.
create or replace function public.guard_calibration() returns trigger
language plpgsql as $$
begin
  if public.is_api_role() and new.aksi = 'faktor' and not public.is_operator() then
    raise exception 'Hanya operator yang boleh menyimpan faktor kalibrasi'
      using errcode = '42501';
  end if;
  return new;
end $$;

drop trigger if exists calibration_guard on public.calibration;
create trigger calibration_guard before insert on public.calibration
  for each row execute function public.guard_calibration();

-- devices: jadwal otomatis, nama, dan project hanya boleh diubah operator.
-- Status/heartbeat dari alat (anon) tetap boleh.
create or replace function public.guard_devices() returns trigger
language plpgsql as $$
begin
  if not public.is_api_role() or public.is_operator() then
    return new;
  end if;
  if (new.interval_detik, new.pompa_detik, new.valve_detik, new.baca_sensor_detik,
      new.stabil_detik, new.sampel_ms, new.trim_persen, new.name, new.project)
     is distinct from
     (old.interval_detik, old.pompa_detik, old.valve_detik, old.baca_sensor_detik,
      old.stabil_detik, old.sampel_ms, old.trim_persen, old.name, old.project) then
    raise exception 'Hanya operator yang boleh mengubah jadwal atau identitas alat'
      using errcode = '42501';
  end if;
  return new;
end $$;

drop trigger if exists devices_guard on public.devices;
create trigger devices_guard before update on public.devices
  for each row execute function public.guard_devices();

-- Policy insert/update lama (anon + authenticated) tetap dipakai alat; penjaga di atas
-- yang membedakan operator dari yang bukan.
