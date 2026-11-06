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

// Panel Kalibrasi (halaman Kalibrasi + Kontrol): status per titik dari perintah
// ph_cal yang SUKSES di device_commands, hanya yang lebih baru dari "reset" terakhir.
const PANEL = [
  ["kalibrasi_ph4", ["cal4", "cal4Ctrl"]],
  ["kalibrasi_ph7", ["cal7", "cal7Ctrl"]],
  ["kalibrasi_ph10", ["cal10", "cal10Ctrl"]],
];
const setText = (ids, text) =>
  ids.forEach((id) => {
    const e = el(id);
    if (e) e.textContent = text;
  });

async function loadPanel() {
  const { data, error } = await supabase
    .from("device_commands")
    .select("id, perintah, created_at")
    .eq("type", "ph_cal")
    .eq("status", "sukses")
    .order("id", { ascending: false })
    .limit(200);
  if (error) {
    console.error("Gagal baca status panel kalibrasi:", error.message);
    return;
  }
  const rows = data || [];
  const resetId = rows.find((r) => r.perintah === "reset")?.id ?? -1;
  let jumlah = 0;
  for (const [perintah, ids] of PANEL) {
    const r = rows.find((x) => x.perintah === perintah && x.id > resetId);
    if (r) jumlah++;
    setText(ids, r ? "OK " + formatDate(parseTimestamp(r.created_at)) : "--");
  }
  const judul =
    jumlah >= 2 ? "PROBE CALIBRATED" : jumlah === 1 ? "OFFSET ONLY (1 TITIK)" : "NOT CALIBRATED";
  const note =
    jumlah >= 2
      ? jumlah + " titik kalibrasi aktif"
      : jumlah === 1
        ? "Tambah 1 titik lagi untuk koreksi slope"
        : "Belum ada titik kalibrasi";
  const warn =
    jumlah >= 2 ? "> KALIBRASI AKTIF (" + jumlah + " TITIK)" : jumlah === 1 ? "! BARU 1 TITIK: HANYA OFFSET" : "! BELUM DIKALIBRASI";
  setText(["calibrationTitle", "calibrationTitleCtrl"], judul);
  setText(["calibrationNote", "calibrationNoteCtrl"], note);
  setText(["calibrationWarning", "calibrationWarningCtrl"], warn);
}

export function initCalibStatus() {
  load();
  loadPanel();
  setInterval(load, 60000);
  setInterval(loadPanel, 60000);
  window.addEventListener("ph-cal-done", () => {
    load();
    loadPanel();
  });
}
