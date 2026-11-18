-- Jalankan di project BARU, SETELAH 0005. Membawa isian kecil dari project Hydrosense
-- lama yang tidak ikut dibuat oleh 0005: daftar sensor dan jadwal otomatis alat.
-- Aman dijalankan ulang; tidak menimpa baris sensor yang sudah ada.

-- 1. Daftar sensor (sama dengan project lama). `sensors` milik struktur groundtank,
--    jadi dicek per nama, bukan lewat on conflict.
insert into public.sensors (name, unit, data_type)
select v.name, v.unit, v.data_type
from (values
  ('ph',          'pH',   'numeric'),
  ('tss',         'mg/L', 'numeric'),
  ('suhu',        '°C',   'numeric'),
  ('level_air',   '%',    'numeric'),
  ('curah_hujan', 'mm',   'numeric')
) as v(name, unit, data_type)
where not exists (select 1 from public.sensors s where s.name = v.name);

-- 2. Baris alat (kalau 0005 belum membuatnya) + jadwal otomatis dari project lama.
insert into public.devices (name, project)
select 'hydrosense-inlet-01', 'hydrosense'
where not exists (
  select 1 from public.devices where name = 'hydrosense-inlet-01' and project = 'hydrosense');

update public.devices set
  interval_detik    = 600,   -- siklus tiap 10 menit
  pompa_detik       = 210,
  valve_detik       = 60,
  baca_sensor_detik = 45,
  stabil_detik      = 5,
  sampel_ms         = 1000,
  trim_persen       = 15
where name = 'hydrosense-inlet-01' and project = 'hydrosense';

-- Sengaja TIDAK dibawa:
--  * device 'test' (baris percobaan di project lama)
--  * riwayat device_commands (500 baris, tidak ada yang pending; sebagian besar log
--    kontrol lama). Perintah pending lama bisa dieksekusi ulang oleh alat.
--  * faktor kalibrasi TSS: nilai aktif di project lama slope=1, offset=0 (manual) =
--    sama dengan bawaan firmware, jadi tidak perlu disalin.
-- Dibawa lewat CSV (Table Editor > Export di project lama, Import di project baru):
--  * "Hydrosense table" (6.662 baris data sensor)
--  * calibration (258 sampel pH, 1 sampel TSS), opsional
-- Dibawa manual: modbus_pin (1 baris hash). Salin sendiri, jangan ditaruh di file.
