-- Tegangan elektroda pH (mV) dari register Modbus sensor pH, untuk panel
-- "PROBE VOLTAGE" di dashboard.
-- JALANKAN MANUAL di SQL Editor project vupbropaxsqireeullbm (project yang
-- dipakai alat & dashboard). Konektor MCP tidak bisa mengakses project itu.
alter table public."Hydrosense table" add column if not exists probe_mv numeric;
