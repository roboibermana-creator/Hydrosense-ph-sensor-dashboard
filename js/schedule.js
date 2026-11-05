// ---------- Jadwal siklus mode otomatis (kolom jadwal di tabel devices) ----------
import { supabase, DEVICE_ID, el } from "./core.js";

export function initSchedule() {
  const SCHED_TABLE = "devices"; // kolom jadwal di baris alat
  function setSchedLog(text, isError) {
    const box = el("schedLog");
    if (!box) return;
    box.textContent = "> " + text;
    box.style.color = isError ? "var(--orange)" : "";
  }

  // Durasi disimpan dalam DETIK; di layar dipecah menit + detik.
  const fmtDur = (d) => `${Math.floor(d / 60)} mnt ${d % 60} dtk`;
  function isiDur(idM, idS, detik) {
    el(idM).value = Math.floor(detik / 60);
    el(idS).value = detik % 60;
  }
  function bacaDur(idM, idS) {
    return (
      Math.round(Number(el(idM).value) || 0) * 60 +
      Math.round(Number(el(idS).value) || 0)
    );
  }

  async function muatJadwal() {
    if (!el("schedIntervalM")) return;
    const { data, error } = await supabase
      .from(SCHED_TABLE)
      .select("interval_detik,pompa_detik,valve_detik,baca_sensor_detik,stabil_detik,sampel_ms,trim_persen")
      .eq("project", "hydrosense")
      .eq("name", DEVICE_ID)
      .maybeSingle();
    if (error) {
      setSchedLog("Gagal memuat jadwal: " + error.message, true);
      return;
    }
    if (data && data.interval_detik != null) {
      data.baca_sensor_detik ??= 15;
      data.stabil_detik ??= 10;
      isiDur("schedIntervalM", "schedIntervalS", data.interval_detik);
      isiDur("schedPompaM", "schedPompaS", data.pompa_detik);
      isiDur("schedValveM", "schedValveS", data.valve_detik);
      isiDur("schedBacaM", "schedBacaS", data.baca_sensor_detik);
      el("schedStabil").value = data.stabil_detik;
      el("schedSampel").value = (data.sampel_ms ?? 1000) / 1000;
      el("schedTrim").value = data.trim_persen ?? 15;
      setSchedLog(
        `Jadwal aktif: siklus tiap ${fmtDur(data.interval_detik)}, pompa maks ${fmtDur(data.pompa_detik)}, valve maks ${fmtDur(data.valve_detik)}, baca sensor ${fmtDur(data.baca_sensor_detik)}, stabil ${data.stabil_detik} dtk`,
        false,
      );
    } else {
      setSchedLog("Belum ada jadwal tersimpan (pakai bawaan alat).", false);
    }
  }

  async function simpanJadwal() {
    const interval = bacaDur("schedIntervalM", "schedIntervalS");
    const pompa = bacaDur("schedPompaM", "schedPompaS");
    const valve = bacaDur("schedValveM", "schedValveS");
    const baca = bacaDur("schedBacaM", "schedBacaS");
    const stabil = Math.round(Number(el("schedStabil").value));
    const sampelMs = Math.round(Number(el("schedSampel").value) * 1000);
    const trim = Math.round(Number(el("schedTrim").value));
    if (
      !(interval >= 10 && interval <= 86400) ||
      !(pompa >= 1 && pompa <= 7200) ||
      !(valve >= 1 && valve <= 7200) ||
      !(baca >= 5 && baca <= 3600) ||
      !(stabil >= 1 && stabil <= 600) ||
      !(sampelMs >= 200 && sampelMs <= 10000) ||
      !(trim >= 0 && trim <= 40)
    ) {
      setSchedLog(
        "Nilai tidak valid: siklus 10 dtk-24 jam, pompa & valve 1 dtk-2 jam, lama baca sensor 5 dtk-60 mnt, stabil 1-600 dtk, jeda sampel 0,2-10 dtk, data dibuang 0-40%.",
        true,
      );
      return;
    }
    const btn = el("btnSimpanJadwal");
    btn.disabled = true;
    try {
      // UPDATE baris alat (disemai di migrasi 0005), bukan upsert.
      const { data: baris, error } = await supabase
        .from(SCHED_TABLE)
        .update({
          interval_detik: interval,
          pompa_detik: pompa,
          valve_detik: valve,
          baca_sensor_detik: baca,
          stabil_detik: stabil,
          sampel_ms: sampelMs,
          trim_persen: trim,
        })
        .eq("project", "hydrosense")
        .eq("name", DEVICE_ID)
        .select("name");
      if (error) throw error;
      if (!baris || baris.length === 0)
        throw new Error("baris alat tidak ditemukan di tabel devices");
      setSchedLog(
        `Tersimpan: siklus tiap ${fmtDur(interval)}, pompa maks ${fmtDur(pompa)}, valve maks ${fmtDur(valve)}, baca sensor ${fmtDur(baca)}, stabil ${stabil} dtk, sampel tiap ${sampelMs / 1000} dtk, buang ${trim}%. Alat membaca perubahan dalam sekitar 30 detik.`,
        false,
      );
    } catch (err) {
      setSchedLog("Gagal menyimpan: " + err.message, true);
    } finally {
      btn.disabled = false;
    }
  }

  if (el("btnSimpanJadwal")) {
    el("btnSimpanJadwal").addEventListener("click", simpanJadwal);
    muatJadwal();
  }
}
