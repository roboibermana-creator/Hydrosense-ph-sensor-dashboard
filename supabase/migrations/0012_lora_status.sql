-- Status tautan LoRa node HydroSense, untuk indikator "LoRa" di header dashboard.
-- Diisi oleh GATEWAY Groundtank tiap 30 detik (gateway punya internet; node di lapangan tidak),
-- ke baris perangkat node (devices: project 'hydrosense', name 'hydrosense-inlet-01').
--   lora_status     : 'nonaktif' (LORA_ENABLED=0 di gateway) | 'mati' (modul gateway tidak siap) | 'siap'
--   lora_rssi       : RSSI (dBm) paket terakhir dari node, diukur gateway
--   lora_ack_age_s  : detik sejak paket valid terakhir dari node (NULL = belum pernah). Node
--                     mem-ping tiap 60 detik, jadi > 180 detik berarti node tidak terdengar.
--   lora_status_at  : waktu laporan gateway (dipakai dashboard untuk menilai segar/basi)
-- JALANKAN MANUAL di SQL Editor project vupbropaxsqireeullbm (project yang dipakai alat &
-- dashboard; konektor MCP tidak bisa mengakses project itu) SEBELUM mengupload firmware gateway
-- yang baru. Tanpa migrasi ini PATCH status LoRa ditolak (gateway mencetak peringatan, fungsi lain aman).
alter table public.devices
  add column if not exists lora_status    text,
  add column if not exists lora_rssi      integer,
  add column if not exists lora_ack_age_s integer,
  add column if not exists lora_status_at timestamptz;
