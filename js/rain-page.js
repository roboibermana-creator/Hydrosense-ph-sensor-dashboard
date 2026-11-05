// Halaman Rain Gauge: intensitas & akumulasi hujan, ringkasan, dan grafik riwayat.
// Data: kolom rain_* di tabel 'Hydrosense table' (baris yang rain_mm_per_hour-nya terisi).
import { supabase, el, numeric, parseTimestamp, formatTime } from "./core.js";
import { drawSeries } from "./chart.js";
import { rainKategori } from "./rain.js";

const LIMIT = 120;
const FRESH_MS = 15 * 60000; // sensor dianggap online kalau data < 15 menit
let rows = []; // [{ t, mmHour, today }] urut waktu naik

const set = (id, text) => {
  const e = el(id);
  if (e) e.textContent = text;
};

function paintChart() {
  const canvas = el("rainChart");
  if (!canvas || !canvas.offsetParent) return; // halaman sedang disembunyikan
  drawSeries(
    canvas,
    rows.map((r) => ({ t: r.t, v: r.mmHour })),
    { decimals: 1, unit: " mm/jam" },
  );
}

function render() {
  const last = rows[rows.length - 1];
  if (!last) {
    ["rainNowP", "rainTodayP", "rainMaxP", "rainAvgP"].forEach((id) => set(id, "-"));
    set("rainCatP", "-");
    set("rainLiveP", "NO DATA");
    set("rainUpdP", "--:--:--");
    set("rainOnlineP", "Belum ada data");
    set("rainCountP", "0");
    paintChart();
    return;
  }
  const fresh = Date.now() - last.t < FRESH_MS;
  const [, kategori] = rainKategori(last.mmHour);
  set("rainNowP", last.mmHour.toFixed(1));
  set("rainCatP", fresh ? kategori : "DATA LAMA");
  set("rainLiveP", fresh ? "LIVE" : "OFFLINE");
  set("rainTodayP", (last.today ?? 0).toFixed(1));
  set("rainUpdP", formatTime(last.t));
  set("rainOnlineP", fresh ? "Online" : "Tidak update");
  const vals = rows.map((r) => r.mmHour);
  set("rainMaxP", `${Math.max(...vals).toFixed(1)} mm/jam`);
  set("rainAvgP", `${(vals.reduce((a, b) => a + b, 0) / vals.length).toFixed(1)} mm/jam`);
  set("rainCountP", String(rows.length));
  paintChart();
}

async function load() {
  const { data, error } = await supabase
    .from("Hydrosense table")
    .select("timestamp, rain_mm_per_hour, rain_mm_today")
    .not("rain_mm_per_hour", "is", null)
    .order("timestamp", { ascending: false })
    .limit(LIMIT);
  if (error) {
    console.error("Gagal baca data hujan:", error.message);
    return;
  }
  rows = (data || [])
    .map((r) => ({
      t: parseTimestamp(r.timestamp),
      mmHour: numeric(r.rain_mm_per_hour),
      today: numeric(r.rain_mm_today),
    }))
    .filter((r) => r.t !== null && r.mmHour !== null)
    .reverse();
  render();
}

export function initRainPage() {
  if (!el("page-rain-gauge")) return;
  load();
  setInterval(load, 30000);
  // Canvas hanya punya ukuran saat halamannya tampil: gambar ulang setelah pindah halaman / resize.
  document
    .querySelectorAll(".nav-link")
    .forEach((l) => l.addEventListener("click", () => requestAnimationFrame(paintChart)));
  window.addEventListener("resize", paintChart);
}
