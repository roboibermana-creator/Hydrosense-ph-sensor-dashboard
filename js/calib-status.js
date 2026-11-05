// Status Kalibrasi pH (titik 4, 7, 9) di Dashboard Utama, dibaca dari tabel calibration.
// Nama titik di database mengikuti firmware: ph4, ph7, ph10 (ph10 = buffer 9.01, tampil "pH 9").
// COMPLETE = ada sampel kalibrasi atau verifikasi untuk titik itu.
import { supabase, el, parseTimestamp, formatDate } from "./core.js";

const POINTS = [
  ["ph4", "calStripPh4"],
  ["ph7", "calStripPh7"],
  ["ph10", "calStripPh10"],
];

function paint(titik, id, rows) {
  const mine = rows.filter((r) => r.titik === titik);
  const cal = mine.find((r) => r.aksi === "kalibrasi"); // baris terbaru dulu
  const ver = mine.find((r) => r.aksi === "verifikasi");
  const latest = cal || ver;
  const status = el(id);
  const when = el(`${id}When`);
  if (!status) return;
  status.textContent = latest ? "COMPLETE" : "PENDING";
  status.dataset.st = latest ? "complete" : "pending";
  if (when) {
    const ms = latest ? parseTimestamp(latest.created_at) : null;
    when.textContent = latest ? `${cal ? "kalibrasi" : "verifikasi"} · ${formatDate(ms)}` : "belum ada sampel";
  }
}

async function load() {
  const { data, error } = await supabase
    .from("calibration")
    .select("titik, aksi, created_at")
    .eq("sensor", "ph")
    .in("aksi", ["kalibrasi", "verifikasi"])
    .order("created_at", { ascending: false })
    .limit(500);
  if (error) {
    console.error("Gagal baca status kalibrasi:", error.message);
    return;
  }
  for (const [titik, id] of POINTS) paint(titik, id, data || []);
}

export function initCalibStatus() {
  load();
  setInterval(load, 60000);
}
