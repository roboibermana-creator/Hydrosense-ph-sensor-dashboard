// Control Valve & Pompa — kirim perintah manual ke Supabase (dibaca ESP32 dari device_commands, type=control)
import { supabase, DEVICE_ID } from "./core.js";

export function initControl() {
  const TOGGLE_ON = "gambarpixel/controls/toggle_switch_on_green.png";
  const TOGGLE_OFF = "gambarpixel/controls/toggle_switch_off_red.png";
  const VALVE_OPEN = "gambarpixel/valve/valve-open.png";
  const VALVE_CLOSED = "gambarpixel/valve/valve-closed.png";
  const PUMP_RUN = "gambarpixel/valve/pump-running.png";
  const PUMP_STOP = "gambarpixel/valve/pump-stopped.png";
  // Animasi tangki: penuh 100% dalam 3 mnt 30 dtk saat pompa jalan,
  // kosong dalam 1 mnt saat valve terbuka.
  const TICK_MS = 1500,
    FILL_DETIK = 210,
    DRAIN_DETIK = 60,
    FILL_STEP = 100 / (FILL_DETIK * 1000 / TICK_MS),
    DRAIN_STEP = 100 / (DRAIN_DETIK * 1000 / TICK_MS);
  const CONTROL_TABLE = "device_commands"; // type = "control"

  let valveOpen = false,
    pumpOn = false,
    level = 100,
    sending = false;

  const byId = (id) => document.getElementById(id);
  const panel = byId("page-control-valve");
  if (!panel) return;

  function setControlStatus(text, isError) {
    const el = byId("controlStatusText");
    if (!el) return;
    el.textContent = "> " + text;
    el.style.color = isError ? "var(--orange)" : "";
  }

  function renderControls() {
    byId("valveToggleImg").src = valveOpen ? TOGGLE_ON : TOGGLE_OFF;
    byId("valveToggle").setAttribute("aria-checked", String(valveOpen));
    byId("valveStateLabel").textContent = valveOpen ? "BUKA" : "TUTUP";
    byId("valveStateLabel").className =
      "state-label " + (valveOpen ? "on" : "off");

    byId("pumpToggleImg").src = pumpOn ? TOGGLE_ON : TOGGLE_OFF;
    byId("pumpToggle").setAttribute("aria-checked", String(pumpOn));
    byId("pumpStateLabel").textContent = pumpOn ? "JALAN" : "BERHENTI";
    byId("pumpStateLabel").className =
      "state-label " + (pumpOn ? "on" : "off");

    byId("valveWheelImg").src = valveOpen ? VALVE_OPEN : VALVE_CLOSED;
    byId("valveWheelImg").className =
      "scene-valve " + (valveOpen ? "is-open" : "is-closed");
    byId("pipeLiquid").className =
      "pipe-liquid" + (valveOpen ? " is-flowing" : "");
    byId("pumpMotorImg").src = pumpOn ? PUMP_RUN : PUMP_STOP;
    byId("pumpMotorImg").className =
      "scene-pump " + (pumpOn ? "is-running" : "is-stopped");

    const dv = byId("dashValveState"), dp = byId("dashPumpState");
    if (dv) {
      dv.textContent = valveOpen ? "BUKA" : "TUTUP";
      dv.className = "hmi-state " + (valveOpen ? "on" : "off");
    }
    if (dp) {
      dp.textContent = pumpOn ? "JALAN" : "BERHENTI";
      dp.className = "hmi-state " + (pumpOn ? "on" : "off");
    }
  }

  function renderLevel() {
    level = Math.max(0, Math.min(100, level));
    byId("tankFill").style.height = level + "%";
    byId("tankLevelLabel").textContent = Math.round(level) + "%";
    byId("tankLevelLabel").style.bottom = Math.min(level, 86) + "%";

    const caption = pumpOn
      ? "Mengisi"
      : valveOpen
        ? "Mengosong"
        : level >= 100
          ? "Penuh"
          : "Diam";
    byId("tankCaption").textContent = caption;

    const desc =
      level <= 0
        ? "Kosong"
        : level <= 15
          ? "Rendah"
          : level >= 100
            ? "Penuh"
            : "Normal";
    byId("chamberLevelText").textContent =
      `${Math.round(level)}% (${desc})`;

    const parts = [];
    if (pumpOn) parts.push("Isi 0,4 L/min (400 mL/mnt)");
    if (valveOpen) parts.push("Buang 18 L/min");
    byId("flowRateText").textContent = parts.length
      ? parts.join(" / ")
      : "0 L/min (Mati)";
  }

  function tick() {
    if (valveOpen && level > 0) level -= DRAIN_STEP;
    if (pumpOn && level < 100) level += FILL_STEP;
    renderLevel();
  }

  // Kirim perintah manual ke Supabase. ESP32 membaca baris TERBARU dari
  // tabel device_commands type=control (order by id desc) lalu mengeksekusi ke pin
  // valve/pompa fisik saat mode = "manual".
  async function sendControlCommand() {
    sending = true;
    setControlStatus("MENGIRIM PERINTAH...", false);
    try {
      const payload = {
        device_id: DEVICE_ID,
        type: "control",
        status: "aktif",
        params: {
          mode: "manual",
          valve_cmd: valveOpen ? "buka" : "tutup",
          pump_cmd: pumpOn ? "jalan" : "berhenti",
        },
      };
      const { error } = await supabase
        .from(CONTROL_TABLE)
        .insert(payload);
      if (error) throw error;
      setControlStatus("PERINTAH TERKIRIM KE SUPABASE", false);
    } catch (error) {
      console.error("Gagal kirim perintah kontrol:", error);
      setControlStatus("GAGAL KIRIM: " + error.message, true);
      throw error;
    } finally {
      sending = false;
    }
  }

  if (byId("valveToggle")) {
    byId("valveToggle").addEventListener("click", () => {
      if (sending) return;
      valveOpen = !valveOpen;
      renderControls();
      sendControlCommand();
    });
  }

  // Status ASLI pompa dari alat (kolom pump_* di tabel devices, ditulis firmware).
  // Dipoll tiap 3 detik; kalau alat menolak (interlock chamber penuh)
  // toggle dikembalikan ke BERHENTI dan muncul peringatan.
  let pumpClickAt = 0;
  const PUMP_SYNC_HOLD_MS = 20000; // maks. tunggu alat melapor setelah klik
  let autoAktif = false; // true = alat mode otomatis & denyutnya segar
  let faseSebelumnya = null; // untuk mendeteksi awal siklus

  // Kondisi NYATA alat (denyut tiap 5 dtk di tabel devices): pompa, valve,
  // float switch. Di mode otomatis panel tangki mengikuti kondisi ini, dan
  // float switch HIGH (chamber penuh) langsung melompatkan level ke 100%.
  async function syncKondisiAlat() {
    if (!byId("pumpToggle")) return;
    try {
      const { data, error } = await supabase
        .from("devices")
        .select("mode,fase,heartbeat_at,pompa_nyala,valve_buka,float_high")
        .eq("project", "hydrosense")
        .eq("name", DEVICE_ID)
        .maybeSingle();
      if (error || !data) return;
      const segar = Date.now() - Date.parse(data.heartbeat_at) < 20000;
      autoAktif = segar && data.mode === "otomatis";
      if (!segar) return;
      if (autoAktif) {
        // Awal siklus (pompa mulai mengisi): tangki dianggap kosong, karena
        // siklus sebelumnya berakhir dengan pembuangan. Float switch HIGH
        // di bawah tetap menang.
        if (
          data.fase === "siklus: pompa mengisi" &&
          faseSebelumnya !== "siklus: pompa mengisi"
        )
          level = 0;
        faseSebelumnya = data.fase;
        if (data.pompa_nyala !== null) pumpOn = data.pompa_nyala;
        if (data.valve_buka !== null) valveOpen = data.valve_buka;
        setControlStatus(
          "MODE OTOMATIS: tampilan mengikuti kondisi alat",
          false,
        );
      }
      if (data.float_high === true) level = 100;
      renderControls();
      renderLevel();
    } catch (e) {
      console.error("Gagal baca kondisi alat:", e);
    }
  }
  setInterval(syncKondisiAlat, 3000);
  syncKondisiAlat();
  async function syncPumpStatus() {
    if (sending || !byId("pumpToggle")) return;
    try {
      const { data, error } = await supabase
        .from("devices")
        .select("pump_on,pump_note,pump_updated_at,ip_address,mac_address")
        .eq("project", "hydrosense")
        .eq("name", DEVICE_ID)
        .maybeSingle();
      if (error || !data) return;
      const netEl = byId("dashDeviceNet");
      if (netEl)
        netEl.textContent =
          "IP " + (data.ip_address || "--") +
          " / MAC " + (data.mac_address || "--");
      // Mode otomatis: tampilan pompa/valve/level diatur syncKondisiAlat()
      // dari kondisi nyata alat, bukan dari status manual yang basi.
      if (autoAktif) return;
      // Heartbeat alat (mode manual tidak mengirim data pH/TSS): kalau
      // status pompa segar (<60 dtk), tandai alat online.
      const hbMs = Date.parse(data.pump_updated_at);
      const statusEl = document.querySelector("#connectionStatus");
      if (
        statusEl &&
        Number.isFinite(hbMs) &&
        Date.now() - hbMs < 60000 &&
        statusEl.textContent === "NO RECENT DATA"
      ) {
        statusEl.textContent = "ALAT ONLINE (MANUAL)";
        document.querySelector("#wifiIcon").className =
          "pixel-icon wifi-icon is-online";
      }
      // Setelah toggle diklik, alat butuh beberapa detik (poll perintah +
      // lapor status) sebelum devices.pump_updated_at ikut berubah. Selama masa
      // tunggu itu, status lama dari alat TIDAK boleh menimpa pilihan
      // user -- kalau tidak, toggle balik sendiri.
      const sejakKlik = Date.now() - pumpClickAt;
      const statusBasi =
        Number.isFinite(hbMs) && hbMs < pumpClickAt - 1000;
      if (sejakKlik < PUMP_SYNC_HOLD_MS && statusBasi) return;
      if (data.pump_note === "ditolak_chamber_penuh") {
        pumpOn = false;
        setControlStatus(
          "POMPA DITOLAK ALAT - CHAMBER PENUH (INTERLOCK)",
          true,
        );
      } else if (data.pump_on !== null && pumpOn !== data.pump_on) {
        // pump_on null = alat belum pernah melapor status pompa; jangan timpa tampilan.
        pumpOn = data.pump_on;
        setControlStatus("STATUS POMPA DARI ALAT: " + data.pump_note, false);
      }
      renderControls();
      renderLevel();
    } catch (e) {
      console.error("Gagal baca status pompa:", e);
    }
  }
  setInterval(syncPumpStatus, 3000);

  if (byId("pumpToggle")) {
    byId("pumpToggle").addEventListener("click", () => {
      if (sending) return;
      pumpOn = !pumpOn;
      pumpClickAt = Date.now();
      renderControls();
      sendControlCommand();
    });
  }

  if (panel) {
    renderControls();
    renderLevel();
    setInterval(tick, TICK_MS);
  }
}
