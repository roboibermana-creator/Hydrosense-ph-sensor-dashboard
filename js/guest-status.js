// Status valve & pompa untuk kartu Aktuator di Dashboard Utama (hanya baca).
// Di mode operator ini diisi oleh control.js; di mode tamu control.js tidak dimuat.
import { supabase, DEVICE_ID, el } from "./core.js";

const FRESH_MS = 20000; // denyut alat lebih lama dari ini = offline

function setState(id, on, textOn, textOff) {
  const e = el(id);
  if (!e) return;
  e.textContent = on === null ? "--" : on ? textOn : textOff;
  e.className = "hmi-state " + (on === null ? "" : on ? "on" : "off");
}

async function poll() {
  try {
    const { data } = await supabase
      .from("devices")
      .select("pompa_nyala, valve_buka, heartbeat_at")
      .eq("project", "hydrosense")
      .eq("name", DEVICE_ID)
      .maybeSingle();
    const fresh = data && Date.now() - Date.parse(data.heartbeat_at) < FRESH_MS;
    setState("dashValveState", fresh ? !!data.valve_buka : null, "BUKA", "TUTUP");
    setState("dashPumpState", fresh ? !!data.pompa_nyala : null, "JALAN", "BERHENTI");
  } catch (e) {
    console.error("Gagal baca status aktuator:", e);
  }
}

export function initGuestStatus() {
  poll();
  setInterval(poll, 4000);
}
