// Kolom teks "Status Kolam" di Dashboard Utama: memberi tahu bila jadwal otomatis DITOLAK karena air
// kolam kering (atau sensor level kolam rusak). Alat mengabarkannya lewat kolom devices.fase
// (diisi firmware, Kolam.cpp). Dipakai semua peran (baca saja).
import { supabase, DEVICE_ID, el } from "./core.js";

const POLL_MS = 5000;
const SEGAR_MS = 30000; // denyut alat lebih lama dari ini = offline

function tampil(st, teks) {
  const box = el("kolamStatus");
  const t = el("kolamTeks");
  if (!box || !t) return;
  box.dataset.st = st;
  t.textContent = teks;
}

async function poll() {
  try {
    const { data, error } = await supabase
      .from("devices")
      .select("mode, fase, heartbeat_at")
      .eq("project", "hydrosense")
      .eq("name", DEVICE_ID)
      .maybeSingle();
    if (error || !data) return tampil("netral", "Menunggu data alat…");
    const segar = Date.now() - Date.parse(data.heartbeat_at) < SEGAR_MS;
    if (!segar) return tampil("netral", "Alat offline — status kolam tidak diketahui");
    if (data.mode !== "otomatis")
      return tampil("netral", "Mode manual — pemeriksaan air kolam hanya berlaku di jadwal otomatis");
    const fase = String(data.fase || "");
    if (fase.startsWith("air kolam kering"))
      return tampil("alarm", "AIR KOLAM KERING — jadwal otomatis ditolak sampai air naik");
    if (fase.startsWith("sensor level kolam"))
      return tampil("alarm", "SENSOR LEVEL KOLAM TIDAK TERBACA — jadwal otomatis ditolak");
    tampil("ok", "Air kolam cukup — jadwal otomatis berjalan normal");
  } catch (e) {
    console.error("Gagal baca status kolam:", e);
  }
}

export function initKolam() {
  poll();
  setInterval(poll, POLL_MS);
}
