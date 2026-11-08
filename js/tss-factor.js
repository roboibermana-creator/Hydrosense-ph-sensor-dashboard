// ---------------- Faktor kalibrasi di dalam sensor TSS (alamat 4-5) ----------------
// ESP32 mengeksekusi perintah baca/tulis/uji lewat tabel
// device_commands (type=tss_hw) (tanpa USB/Serial Monitor).
import { supabase, DEVICE_ID, el, numeric } from "./core.js";

export function initTssFactor() {
  if (!el("btnBacaFaktor")) return;
  const CMD_TABLE = "device_commands";
  const TOMBOL = ["btnBacaFaktor", "btnTulisFaktor", "btnHitungFaktor", "btnUjiFaktor"];
  let faktorSekarang = null;

  function setLog(text, isError) {
    const box = el("tssCalLog");
    if (!box) return;
    box.textContent = "> " + text;
    box.style.color = isError ? "var(--orange)" : "";
  }
  function kunci(disabled) {
    TOMBOL.forEach((id) => {
      if (el(id)) el(id).disabled = disabled;
    });
  }
  function tampil(pesan) {
    const f = /faktor=([-+0-9.eE]+)/.exec(pesan || "");
    if (f) {
      faktorSekarang = Number(f[1]);
      el("faktorNow").textContent = faktorSekarang.toFixed(4);
    }
    const t =
      /tss_raw=([-+0-9.eE]+)/.exec(pesan || "") ||
      /tss_sesudah=([-+0-9.eE]+)/.exec(pesan || "");
    if (t) el("faktorTssNow").textContent = Number(t[1]).toFixed(2) + " mg/L";
  }

  async function kirim(perintah, nilai, maksDetik, aktual) {
    setLog(`Perintah ${perintah} terkirim. Alat membalas dalam ~30-60 detik, JANGAN kirim perintah lain dulu...`, false);
    kunci(true);
    try {
      const params = {};
      if (nilai !== undefined) params.nilai_target = nilai;
      if (aktual !== undefined) params.nilai_aktual = aktual;
      const payload = { device_id: DEVICE_ID, type: "tss_hw", perintah, params };
      const { data, error } = await supabase
        .from(CMD_TABLE)
        .insert(payload)
        .select("id")
        .single();
      if (error) throw error;
      const tries = Math.ceil(Math.max(maksDetik || 30, 120) / 1.5); // min. 2 menit: lewat LoRa lebih lambat
      for (let i = 0; i < tries; i++) {
        await new Promise((r) => setTimeout(r, 1500));
        const { data: r, error: e2 } = await supabase
          .from(CMD_TABLE)
          .select("status, pesan")
          .eq("id", data.id)
          .maybeSingle();
        if (e2) {
          setLog("Gagal cek hasil: " + e2.message, true);
          return null;
        }
        if (r && r.status !== "pending") {
          setLog(
            (r.status === "sukses" ? "OK: " : "GAGAL: ") + (r.pesan || ""),
            r.status !== "sukses",
          );
          tampil(r.pesan);
          return r;
        }
      }
      setLog("Timeout: alat belum membalas (offline, mode otomatis, atau sedang sibuk). Cek lagi nanti di tabel device_commands.", true);
      return null;
    } catch (err) {
      setLog("Gagal kirim perintah: " + err.message, true);
      return null;
    } finally {
      kunci(false);
    }
  }

  el("btnBacaFaktor").addEventListener("click", () =>
    kirim("baca_faktor", undefined, 100),
  );

  el("btnTulisFaktor").addEventListener("click", async () => {
    const v = Number(el("inputFaktorBaru").value);
    if (!Number.isFinite(v) || v < 0.11 || v > 9.99) {
      setLog("Isi faktor baru 0,11 - 9,99.", true);
      return;
    }
    if (
      !confirm(
        `Tulis faktor ${v} ke sensor (alamat 4-5)?\nFaktor lama akan tertimpa.`,
      )
    )
      return;
    await kirim("tulis_faktor", v, 130);
  });

  el("btnUjiFaktor").addEventListener("click", async () => {
    if (
      !confirm(
        "Uji faktor: alat menulis faktor 2x lipat, membandingkan TSS, lalu mengembalikan faktor awal (~25 detik, alat harus mode MANUAL). Lanjut?",
      )
    )
      return;
    await kirim("uji_faktor", undefined, 150);
  });

  // Faktor baru = faktor sekarang x (jumlah raw*gravimetri / jumlah raw^2):
  // raw di sampel sudah memuat faktor yang aktif saat sampel diambil.
  el("btnHitungFaktor").addEventListener("click", async () => {
    if (faktorSekarang === null) {
      setLog('Tekan "BACA FAKTOR SENSOR" dulu supaya faktor sekarang diketahui.', true);
      return;
    }
    kunci(true);
    try {
      const { data, error } = await supabase
        .from("calibration")
        .select("tss:nilai, raw_value")
        .eq("sensor", "tss")
        .eq("aksi", "kalibrasi");
      if (error) throw error;
      let sxy = 0,
        sxx = 0,
        n = 0;
      for (const row of data || []) {
        const raw = numeric(row.raw_value);
        const act = numeric(row.tss);
        if (raw === null || act === null || raw <= 0) continue;
        sxy += raw * act;
        sxx += raw * raw;
        n++;
      }
      if (n < 1 || sxx === 0) {
        setLog("Belum ada sampel kalibrasi (raw & gravimetri) yang valid.", true);
        return;
      }
      const baru = faktorSekarang * (sxy / sxx);
      el("inputFaktorBaru").value = baru.toFixed(4);
      setLog(
        `Dari ${n} sampel: faktor ${faktorSekarang.toFixed(4)} -> ${baru.toFixed(4)}` +
          (baru < 0.11 || baru > 9.99
            ? " (DI LUAR rentang 0,11-9,99, tidak bisa ditulis ke sensor)"
            : ". Periksa angkanya lalu tekan TERAPKAN KE SENSOR."),
        baru < 0.11 || baru > 9.99,
      );
    } catch (err) {
      setLog("Gagal hitung dari sampel: " + err.message, true);
    } finally {
      kunci(false);
    }
  });
}
