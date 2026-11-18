-- Turbidity (NTU) dari register 6-7 sensor TSS (DUGAAN, untuk dibandingkan dengan
-- TSS di dashboard). JALANKAN MANUAL di SQL Editor project vupbropaxsqireeullbm
-- SEBELUM mengupload firmware / men-deploy dashboard yang baru.
alter table public."Hydrosense table" add column if not exists ntu numeric;
