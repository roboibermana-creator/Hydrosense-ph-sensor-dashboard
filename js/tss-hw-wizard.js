// ---------------- Kalibrasi Hardware TSS (native, two-point) ----------------
// Wizard step-by-step yang mengirim perintah ke tabel
// device_commands (type=tss_hw) -- ESP32 yang eksekusi baca/tulis register
// fisik sensor (lihat Panduan Kalibrasi Sensor TSS), menggantikan
// Modbus Poll + USB-RS485 manual.
import { supabase, DEVICE_ID, el } from "./core.js";

export function initTssHwWizard() {
  const CMD_TABLE = "device_commands";

  const semuaTombolHw = () =>
    [
      "btnHwCekStatus",
      "btnHwSiapkan",
      "btnHwTitik1Aktif",
      "btnHwTitik1Simpan",
      "btnHwTitik2Aktif",
      "btnHwTitik2Simpan",
      "btnHwVerifikasiKeluar",
      "btnHwResetFactor",
    ]
      .map(el)
      .filter(Boolean);

  function setHwButtonsDisabled(disabled) {
    semuaTombolHw().forEach((btn) => (btn.disabled = disabled));
  }

  function setHwCalLog(text, isError) {
    const box = el("hwCalLog");
    if (!box) return;
    box.textContent = "> " + text;
    box.style.color = isError ? "var(--orange)" : "";
  }

  async function pollHasilPerintahHw(id) {
    const MAX_TRIES = 20; // device poll tiap 2s -- 20x1.5s cukup untuk timeout 30s
    for (let i = 0; i < MAX_TRIES; i++) {
      await new Promise((r) => setTimeout(r, 1500));
      const { data, error } = await supabase
        .from(CMD_TABLE)
        .select("status, pesan, hasil")
        .eq("id", id)
        .maybeSingle();
      if (error) {
        setHwCalLog("Gagal cek hasil: " + error.message, true);
        return;
      }
      if (data && data.status !== "pending") {
        setHwCalLog(
          (data.status === "sukses" ? "OK: " : "GAGAL: ") +
            (data.pesan || ""),
          data.status !== "sukses",
        );
        const reg13 = data.hasil?.reg13;
        const reg14 = data.hasil?.reg14;
        if (reg13 != null && el("hwCalReg13"))
          el("hwCalReg13").textContent = reg13;
        if (reg14 != null && el("hwCalReg14"))
          el("hwCalReg14").textContent = reg14;
        return;
      }
    }
    setHwCalLog(
      "Timeout: device tidak merespons 30 detik (alat offline?).",
      true,
    );
  }

  async function kirimPerintahHw(perintah, nilaiTarget, nilaiAktual) {
    setHwCalLog(`Mengirim perintah: ${perintah}...`, false);
    setHwButtonsDisabled(true);
    try {
      const params = {};
      if (nilaiTarget !== undefined && nilaiTarget !== null)
        params.nilai_target = nilaiTarget;
      if (nilaiAktual !== undefined && nilaiAktual !== null)
        params.nilai_aktual = nilaiAktual;
      const payload = { device_id: DEVICE_ID, type: "tss_hw", perintah, params };

      const { data, error } = await supabase
        .from(CMD_TABLE)
        .insert(payload)
        .select("id")
        .single();
      if (error) throw error;

      await pollHasilPerintahHw(data.id);
    } catch (err) {
      setHwCalLog("Gagal kirim perintah: " + err.message, true);
    } finally {
      setHwButtonsDisabled(false);
    }
  }

  function bacaAngka(id) {
    const v = Number(el(id)?.value);
    return Number.isFinite(v) && el(id)?.value !== "" ? v : null;
  }

  if (el("btnHwCekStatus"))
    el("btnHwCekStatus").addEventListener("click", () =>
      kirimPerintahHw("cek_status"),
    );

  if (el("btnHwSiapkan"))
    el("btnHwSiapkan").addEventListener("click", () =>
      kirimPerintahHw("siapkan"),
    );

  if (el("btnHwTitik1Aktif"))
    el("btnHwTitik1Aktif").addEventListener("click", () =>
      kirimPerintahHw("set_titik1"),
    );

  if (el("btnHwTitik1Simpan"))
    el("btnHwTitik1Simpan").addEventListener("click", () => {
      const p1 = bacaAngka("hwP1");
      if (p1 === null) {
        setHwCalLog("Isi nilai Partech P1 dulu.", true);
        return;
      }
      kirimPerintahHw("tulis_titik1", p1, bacaAngka("hwS1"));
    });

  if (el("btnHwTitik2Aktif"))
    el("btnHwTitik2Aktif").addEventListener("click", () =>
      kirimPerintahHw("set_titik2"),
    );

  if (el("btnHwTitik2Simpan"))
    el("btnHwTitik2Simpan").addEventListener("click", () => {
      const p2 = bacaAngka("hwP2");
      if (p2 === null) {
        setHwCalLog("Isi nilai Partech P2 dulu.", true);
        return;
      }
      kirimPerintahHw("tulis_titik2", p2, bacaAngka("hwS2"));
    });

  if (el("btnHwVerifikasiKeluar"))
    el("btnHwVerifikasiKeluar").addEventListener("click", () =>
      kirimPerintahHw("verifikasi_keluar"),
    );

  if (el("btnHwResetFactor"))
    el("btnHwResetFactor").addEventListener("click", () => {
      if (
        !confirm(
          "Batalkan kalibrasi & reset factor sensor ke 1.0 (normal)?",
        )
      )
        return;
      kirimPerintahHw("reset_factor");
    });
}
