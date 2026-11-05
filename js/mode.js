// ---------- Pilih mode operasi (params.mode di device_commands type=control) ----------
// Alat membaca baris control TERBARU. Pindah mode = insert baris
// baru dengan mode yang dipilih. valve_cmd dibawa dari baris terakhir
// (supaya valve tidak bergerak karena pindah mode) dan pump_cmd
// "berhenti" (aman).
import { supabase, DEVICE_ID, el } from "./core.js";

export function initMode() {
  const CONTROL_TABLE = "device_commands"; // type = "control"
  function tampilkanMode(mode) {
    const teks = mode === "otomatis" ? "OTOMATIS" : mode === "manual" ? "MANUAL" : "--";
    if (el("modeNowText")) el("modeNowText").textContent = teks;
    if (el("btnModeManual"))
      el("btnModeManual").style.opacity = mode === "manual" ? "1" : "0.55";
    if (el("btnModeOtomatis"))
      el("btnModeOtomatis").style.opacity = mode === "otomatis" ? "1" : "0.55";
    document.querySelectorAll("#dashModeBadge [data-mode]").forEach((s) => {
      s.classList.toggle("on", s.dataset.mode === mode);
    });
  }

  async function bacaModeTerakhir() {
    const { data, error } = await supabase
      .from(CONTROL_TABLE)
      .select("id,params")
      .eq("type", "control")
      .eq("device_id", DEVICE_ID)
      .order("id", { ascending: false })
      .limit(1)
      .maybeSingle();
    if (error) throw error;
    return data;
  }

  async function refreshMode() {
    if (!el("modeNowText")) return;
    try {
      const d = await bacaModeTerakhir();
      tampilkanMode(d ? d.params?.mode : null);
    } catch (e) {
      console.error("Gagal baca mode:", e);
    }
  }

  async function setMode(modeBaru) {
    if (
      modeBaru === "otomatis" &&
      !confirm(
        "Pindah ke mode OTOMATIS? Alat akan menjalankan pompa dan valve sendiri sesuai jadwal.",
      )
    )
      return;
    const log = el("modeLog");
    try {
      const terakhir = await bacaModeTerakhir();
      const { error } = await supabase.from(CONTROL_TABLE).insert({
        device_id: DEVICE_ID,
        type: "control",
        status: "aktif",
        params: {
          mode: modeBaru,
          valve_cmd: terakhir?.params?.valve_cmd || "tutup",
          pump_cmd: "berhenti",
        },
      });
      if (error) throw error;
      tampilkanMode(modeBaru);
      if (log) {
        log.textContent = "> MODE " + modeBaru.toUpperCase() + " DIKIRIM KE ALAT";
        log.style.color = "";
      }
    } catch (err) {
      if (log) {
        log.textContent = "> GAGAL PINDAH MODE: " + err.message;
        log.style.color = "var(--orange)";
      }
    }
  }

  if (el("btnModeManual")) {
    el("btnModeManual").addEventListener("click", () => setMode("manual"));
    el("btnModeOtomatis").addEventListener("click", () => setMode("otomatis"));
    refreshMode();
    setInterval(refreshMode, 5000);
  }
}
