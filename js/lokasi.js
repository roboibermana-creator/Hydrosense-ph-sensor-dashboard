// Kartu "Lokasi Alat": posisi GPS dari modem SIM7600 (devices.latitude/longitude, diisi node).
// Peta = OpenStreetMap embed (tanpa API key). Hanya dimuat untuk peran tamu/operator.
import { supabase, DEVICE_ID, el } from "./core.js";

const POLL_MS = 5 * 60 * 1000; // alat diam; posisi diperbarui firmware tiap beberapa jam

function waktuRelatif(iso) {
  const t = Date.parse(iso);
  if (!Number.isFinite(t)) return "";
  const m = Math.round((Date.now() - t) / 60000);
  if (m < 2) return "baru saja";
  if (m < 120) return m + " menit lalu";
  const j = Math.round(m / 60);
  return j < 48 ? j + " jam lalu" : Math.round(j / 24) + " hari lalu";
}

async function poll() {
  const teks = el("lokasiTeks");
  const peta = el("lokasiPeta");
  const link = el("lokasiLink");
  if (!teks || !peta || !link) return;
  try {
    const { data, error } = await supabase
      .from("devices")
      .select("latitude, longitude, altitude_m, location_at")
      .eq("project", "hydrosense")
      .eq("name", DEVICE_ID)
      .maybeSingle();
    // error = kolom belum ada (migrasi 0013 belum dijalankan): tampilkan kondisi "belum ada data"
    if (error || !data || data.latitude == null || data.longitude == null) {
      peta.hidden = true;
      link.hidden = true;
      teks.textContent = "Belum ada data lokasi dari GPS alat.";
      return;
    }
    const lat = Number(data.latitude);
    const lon = Number(data.longitude);
    const dLat = 0.004,
      dLon = 0.006;
    peta.src =
      "https://www.openstreetmap.org/export/embed.html?bbox=" +
      [lon - dLon, lat - dLat, lon + dLon, lat + dLat].join(",") +
      "&layer=mapnik&marker=" +
      lat +
      "," +
      lon;
    peta.hidden = false;
    link.href = "https://www.google.com/maps?q=" + lat + "," + lon;
    link.hidden = false;
    teks.textContent =
      lat.toFixed(6) +
      ", " +
      lon.toFixed(6) +
      (data.altitude_m != null ? " · " + Math.round(data.altitude_m) + " m" : "") +
      (data.location_at ? " · diperbarui " + waktuRelatif(data.location_at) : "");
  } catch (e) {
    console.error("Gagal baca lokasi alat:", e);
  }
}

export function initLokasi() {
  poll();
  setInterval(poll, POLL_MS);
}
