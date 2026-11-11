// ---------- Tinggi air kolam: batas minimal & tinggi maksimum (devices.min_air_cm / max_air_cm) ----------
// Alat membaca kolom ini tiap ~30 dtk (firmware Kolam.cpp). Maksimum = jarak sensor ke dasar kolam;
// minimal = di bawah ini siklus otomatis ditolak ("air kolam kering").
// Hanya operator yang boleh menyimpan (penjaga DB, migrasi 0014).
import { supabase, DEVICE_ID, el } from "./core.js";

const MAKS_MIN_CM = 10; // tinggi maksimum minimal yang diterima alat
const MAKS_MAKS_CM = 500; // = RADAR_RANGE_CM di firmware

export function initMinAir() {
  if (!el("inputMinAir")) return;
  const log = (teks, galat) => {
    const box = el("minAirLog");
    box.textContent = "> " + teks;
    box.style.color = galat ? "var(--orange)" : "";
  };
  const tampil = (v, def) => (v === null ? def : v + " cm");

  async function muat() {
    const { data, error } = await supabase
      .from("devices")
      .select("min_air_cm, max_air_cm")
      .eq("project", "hydrosense")
      .eq("name", DEVICE_ID)
      .maybeSingle();
    if (error) {
      el("minAirNow").textContent = "--";
      el("maxAirNow").textContent = "--";
      log(
        /min_air_cm|max_air_cm/.test(error.message)
          ? "Kolom belum ada: jalankan migrasi 0014 di Supabase."
          : "Gagal memuat: " + error.message,
        true,
      );
      return;
    }
    const mn = data && data.min_air_cm != null ? Number(data.min_air_cm) : null;
    const mx = data && data.max_air_cm != null ? Number(data.max_air_cm) : null;
    el("minAirNow").textContent = tampil(mn, "bawaan alat (20 cm)");
    el("maxAirNow").textContent = tampil(mx, "bawaan alat (500 cm)");
    if (mn !== null) el("inputMinAir").value = mn;
    if (mx !== null) el("inputMaxAir").value = mx;
    log(
      mn === null && mx === null
        ? "Belum diatur dari web: alat memakai nilai bawaannya."
        : "Tersimpan di server.",
      false,
    );
  }

  el("btnSimpanMinAir").addEventListener("click", async () => {
    const rawMn = el("inputMinAir").value;
    const rawMx = el("inputMaxAir").value;
    const mn = Number(rawMn);
    const mx = Number(rawMx);
    if (rawMn === "" || rawMx === "" || !Number.isFinite(mn) || !Number.isFinite(mx)) {
      log("Isi kedua kolom: tinggi maksimum dan batas minimal.", true);
      return;
    }
    if (mx < MAKS_MIN_CM || mx > MAKS_MAKS_CM) {
      log(`Tinggi maksimum harus ${MAKS_MIN_CM} - ${MAKS_MAKS_CM} cm.`, true);
      return;
    }
    if (mn < 0 || mn > mx) {
      log(`Batas minimal harus 0 - ${mx} cm (tidak boleh melebihi tinggi maksimum).`, true);
      return;
    }
    const btn = el("btnSimpanMinAir");
    btn.disabled = true;
    try {
      const { data, error } = await supabase
        .from("devices")
        .update({ min_air_cm: mn, max_air_cm: mx })
        .eq("project", "hydrosense")
        .eq("name", DEVICE_ID)
        .select("name");
      if (error) throw error;
      if (!data || data.length === 0) throw new Error("baris alat tidak ditemukan di tabel devices");
      el("minAirNow").textContent = mn + " cm";
      el("maxAirNow").textContent = mx + " cm";
      log(`Tersimpan: maksimum ${mx} cm, minimal ${mn} cm. Alat membaca perubahan dalam sekitar 30 detik.`, false);
    } catch (err) {
      log("Gagal menyimpan: " + err.message, true);
    } finally {
      btn.disabled = false;
    }
  });

  muat();
}
