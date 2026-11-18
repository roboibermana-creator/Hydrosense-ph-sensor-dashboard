-- Pembersihan SEKALI JALAN, hanya untuk project groundtank yang sempat menjalankan
-- skrip versi pertama (tabel Hydrosense bergaya uuid). Jalankan SEBELUM 0001_schema.sql.
-- BUKAN bagian migrations, jangan dijalankan di project baru.
--
-- Pengaman: sebuah tabel hanya dihapus kalau isinya 0 baris. Tabel yang berisi
-- dilewati dan dilaporkan sebagai NOTICE, supaya tidak ada data yang hilang.

do $$
declare
  t text;
  n bigint;
begin
  foreach t in array array[
    'device_commands','auto_schedule','modbus_cmd','ph_calibration_cmd',
    'calibration_samples','tss_calibration_config','tss_calibration_samples',
    'tss_hw_calibration_cmd'
  ] loop
    if to_regclass(format('public.%I', t)) is null then
      raise notice 'Tidak ada: %', t;
      continue;
    end if;
    execute format('select count(*) from public.%I', t) into n;
    if n = 0 then
      execute format('drop table public.%I cascade', t);
      raise notice 'Dihapus (kosong): %', t;
    else
      raise notice 'DILEWATI, berisi % baris: %', n, t;
    end if;
  end loop;
end $$;
