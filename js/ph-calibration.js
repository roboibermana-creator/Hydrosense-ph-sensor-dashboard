// ---------- Kalibrasi / verifikasi pH dari web (tabel device_commands, type=ph_cal) ----------
// ESP32 membaca baris baru, menjalankan proses yang sama seperti
// perintah Serial (blocking sampai pembacaan stabil), lalu mengisi
// status + pesan. Karena bisa lama, polling hasil dibatasi 3 menit.
import { supabase, DEVICE_ID, el } from "./core.js";

export function initPhCalibration() {
  const PH_CMD_TABLE = "device_commands";
  const phButtons = () => [...document.querySelectorAll(".ph-cal-btn")];

  function setPhCalLog(text, isError) {
    const box = el("phCalLog");
    if (!box) return;
    box.textContent = "> " + text;
    box.style.color = isError ? "var(--orange)" : "";
  }

  async function kirimPerintahPh(perintah) {
    phButtons().forEach((b) => (b.disabled = true));
    setPhCalLog("Mengirim perintah: " + perintah + "...", false);
    try {
      const { data, error } = await supabase
        .from(PH_CMD_TABLE)
        .insert({ device_id: DEVICE_ID, type: "ph_cal", perintah })
        .select("id")
        .single();
      if (error) throw error;
      setPhCalLog("Menunggu alat (stabilisasi pembacaan, bisa beberapa menit)...", false);
      // Warm-up 3 mnt + maks 30 sampel (~1 mnt) + waktu kirim; beri ruang lega.
      const MAKS_TUNGGU_MS = 600000;
      const mulai = Date.now();
      while (Date.now() - mulai < MAKS_TUNGGU_MS) {
        await new Promise((r) => setTimeout(r, 2000));
        const { data: hasil, error: errCek } = await supabase
          .from(PH_CMD_TABLE)
          .select("status,pesan")
          .eq("id", data.id)
          .maybeSingle();
        if (errCek) {
          setPhCalLog("Gagal cek hasil: " + errCek.message, true);
          return;
        }
        if (hasil && hasil.status === "pending" && hasil.pesan) {
          // Progres langsung dari alat (warm-up / sampel / sd / drift).
          setPhCalLog("ALAT: " + hasil.pesan, false);
        }
        if (hasil && hasil.status !== "pending") {
          setPhCalLog(
            (hasil.status === "sukses" ? "OK: " : "GAGAL: ") + (hasil.pesan || ""),
            hasil.status !== "sukses",
          );
          return;
        }
      }
      setPhCalLog("Timeout: tidak ada hasil dalam 10 menit (alat offline?). Cek Serial Monitor.", true);
    } catch (err) {
      setPhCalLog("Gagal kirim perintah: " + err.message, true);
    } finally {
      phButtons().forEach((b) => (b.disabled = false));
    }
  }

  phButtons().forEach((btn) =>
    btn.addEventListener("click", () => {
      let cmd = btn.dataset.phCmd;
      if (cmd === "offset_ph") {
        const v = parseFloat(el("phOffsetInput")?.value);
        if (!Number.isFinite(v) || v < -2 || v > 2) {
          setPhCalLog("Offset harus angka antara -2.00 dan +2.00 pH.", true);
          return;
        }
        cmd = "offset_ph:" + v.toFixed(2);
      }
      if (
        cmd === "reset" &&
        !confirm("Hapus data kalibrasi pH yang tersimpan di alat?")
      )
        return;
      kirimPerintahPh(cmd);
    }),
  );
}
