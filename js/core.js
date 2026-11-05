// Klien Supabase, id alat, helper umum, dan state bersama antar-modul.
import { createClient } from "https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2/+esm";

const SUPABASE_URL = "https://vupbropaxsqireeullbm.supabase.co";
const SUPABASE_PUBLISHABLE_KEY = "sb_publishable_mvifCe-YeWbL4Wyp3jf1fA_Xwz2P26Z";

export const supabase = createClient(SUPABASE_URL, SUPABASE_PUBLISHABLE_KEY);
export const DEVICE_ID = "hydrosense-inlet-01";
export const el = (id) => document.getElementById(id);

// State bersama antar-modul (modul ES tidak boleh menugaskan ulang impor).
export const state = {
  // Bacaan terakhir (diisi tiap refreshDashboard()) -- dipakai panel sampel kalibrasi TSS.
  latestSensorSnapshot: null,
  // Denyut alat terakhir dari tabel devices (diisi countdown.js):
  // { sisa, fase, mode, diterimaMs, hbMs, tssRaw }
  hb: null,
};

export const numeric = (value) => {
  // Number(null) === 0 dan Number("") === 0 -- keduanya HARUS dianggap
  // "tidak ada data" (mis. baris TSS-only dengan ph:null dari Supabase),
  // bukan literal angka 0, supaya tidak salah tampil sebagai pH 0.00.
  if (value === null || value === undefined || value === "") return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
};

export const parseTimestamp = (value) => {
  if (value === null || value === undefined) return null;
  if (typeof value === "string") {
    const ms = Date.parse(value);
    return Number.isFinite(ms) ? ms : null;
  }
  const n = Number(value);
  if (!Number.isFinite(n)) return null;
  return n < 100000000000 ? n * 1000 : n;
};

export const formatTime = (value) => {
  const ms = parseTimestamp(value);
  if (ms === null) return "--:--:--";
  return new Date(ms).toLocaleTimeString("id-ID", {
    timeZone: "Asia/Makassar",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  });
};
export const formatDate = (value) => {
  const ms = parseTimestamp(value);
  if (ms === null) return "--.--.----";
  return new Date(ms).toLocaleDateString("id-ID", {
    timeZone: "Asia/Makassar",
  });
};
