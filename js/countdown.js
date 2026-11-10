// ---------- Countdown siklus berikutnya (denyut alat di tabel devices) ----------
// Alat mengirim mode/fase/next_cycle_s + heartbeat_at tiap 5 detik. Sisa
// detik dihitung turun di browser antar poll; denyut >20 dtk = alat offline.
// (state.hb dipakai juga oleh calibration-tss.js)
import { supabase, DEVICE_ID, el, state } from "./core.js";

export function initCountdown() {
  const fmtHitung = (s) =>
    `${String(Math.floor(s / 60)).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}`;
  function renderCountdown() {
    const c = el("cycleCountdown");
    if (!c) return;
    const segar = state.hb && Number.isFinite(state.hb.hbMs) && Date.now() - state.hb.hbMs < 20000;
    const st = document.querySelector("#connectionStatus");
    if (segar && st && st.textContent === "NO RECENT DATA") {
      st.textContent = "ALAT ONLINE";
      document.querySelector("#wifiIcon").className = "pixel-icon wifi-icon is-online";
    }
    if (!state.hb) c.textContent = "--";
    else if (!segar) c.textContent = "alat offline";
    else if (state.hb.mode !== "otomatis") c.textContent = "mode manual";
    else if (state.hb.fase && /^(air kolam kering|sensor level kolam)/.test(state.hb.fase)) {
      c.textContent = "ditolak: " + state.hb.fase.replace(" - siklus ditolak", "");
    } else if (state.hb.fase && state.hb.fase.startsWith("siklus")) {
      c.textContent = state.hb.fase + " (hitung mundur mulai setelah valve menutup)";
    } else {
      const s = Math.max(0, state.hb.sisa - Math.floor((Date.now() - state.hb.diterimaMs) / 1000));
      c.textContent = s === 0 ? "segera mulai" : fmtHitung(s);
    }
  }
  async function pollDenyut() {
    try {
      const { data } = await supabase
        .from("devices")
        .select("mode,fase,next_cycle_s,heartbeat_at,tss_raw")
        .eq("project", "hydrosense")
        .eq("name", DEVICE_ID)
        .maybeSingle();
      if (data)
        state.hb = {
          sisa: data.next_cycle_s ?? 0,
          fase: data.fase,
          mode: data.mode,
          diterimaMs: Date.now(),
          hbMs: Date.parse(data.heartbeat_at),
          tssRaw: data.tss_raw === null || data.tss_raw === undefined ? null : Number(data.tss_raw),
        };
    } catch (e) {
      console.error("Gagal baca denyut:", e);
    }
    renderCountdown();
  }
  if (el("cycleCountdown")) {
    pollDenyut();
    setInterval(pollDenyut, 4000);
    setInterval(renderCountdown, 1000);
  }
}
