// Kartu curah hujan (kolom rain_* di tabel 'Hydrosense table').
import { supabase, numeric, parseTimestamp, formatTime } from "./core.js";
import { hmiSetKpi } from "./dashboard.js?v=20261106f";


// ---------------- Rain gauge (kolom rain_* di "Hydrosense table") ----------------
// Kategori BMKG per jam: <0.5 tidak hujan, 0.5-5 ringan, 5-10 sedang,
// 10-20 lebat, >20 sangat lebat. Kalau tabel belum ada / kosong, kartu
// menampilkan "Belum terpasang" tanpa mengganggu bagian lain.
const RAIN_TABLE = "Hydrosense table";
export function rainKategori(mm) {
  if (mm < 0.5) return ["ok", "TIDAK HUJAN"];
  if (mm < 5) return ["info", "RINGAN"];
  if (mm < 10) return ["warn", "SEDANG"];
  if (mm < 20) return ["alarm", "LEBAT"];
  return ["alarm", "SANGAT LEBAT"];
}
async function refreshRain() {
  const set = (id, v) => {
    const el = document.getElementById(id);
    if (el) el.textContent = v;
  };
  try {
    const { data, error } = await supabase
      .from(RAIN_TABLE)
      .select("timestamp, rain_mm_per_hour, rain_mm_today")
      .not("rain_mm_per_hour", "is", null)
      .order("timestamp", { ascending: false })
      .limit(1);
    if (error) throw error;
    const row = data && data[0];
    const ms = parseTimestamp(row?.timestamp);
    const fresh = ms !== null && Date.now() - ms < 15 * 60000;
    const mm = numeric(row?.rain_mm_per_hour);
    if (!row || mm === null) throw new Error("kosong");
    const [st, txt] = fresh ? rainKategori(mm) : ["offline", "DATA LAMA"];
    hmiSetKpi("kpiRain", "rainPill", st, txt);
    set("rainNow", mm.toFixed(1));
    set("rainToday", (numeric(row.rain_mm_today) ?? 0).toFixed(1));
    set("rainStatusText", fresh ? "Online" : "Tidak update");
    set("rainLastUpdate", formatTime(ms).slice(0, 5));
    document.getElementById("rainFill").style.width = Math.min(100, (mm / 20) * 100) + "%";
  } catch (e) {
    hmiSetKpi("kpiRain", "rainPill", "offline", "NO DATA");
    set("rainNow", "-");
    set("rainToday", "-");
    set("rainStatusText", "Belum ada");
  }
}
refreshRain();
setInterval(refreshRain, 30000);

document.querySelectorAll(".hmi-kpi.is-link").forEach((card) =>
  card.addEventListener("keydown", (e) => {
    if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      card.click();
    }
  }),
);
