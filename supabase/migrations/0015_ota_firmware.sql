-- =====================================================================
--  0015: update firmware lewat Supabase Storage (lihat src/OtaWeb.cpp)
--  - device_commands.type boleh 'ota' (params: {file, sha256, versi})
--  - bucket publik "firmware": alat/dashboard BOLEH membaca, tapi TIDAK ADA policy
--    tulis untuk anon -- hanya pemilik proyek (service_role / dashboard Supabase)
--    yang bisa mengunggah firmware. Jangan tambahkan policy insert untuk anon:
--    siapa pun yang bisa menulis ke bucket ini bisa memasang firmware ke alat.
-- =====================================================================
alter table public.device_commands drop constraint if exists device_commands_type_check;
alter table public.device_commands
  add constraint device_commands_type_check
  check (type in ('control', 'modbus', 'ph_cal', 'tss_hw', 'ota'));

insert into storage.buckets (id, name, public)
values ('firmware', 'firmware', true)
on conflict (id) do update set public = true;

-- Baca publik (unduh oleh alat & dashboard). Tidak ada policy insert/update/delete.
drop policy if exists "firmware_baca_publik" on storage.objects;
create policy "firmware_baca_publik" on storage.objects
  for select using (bucket_id = 'firmware');
