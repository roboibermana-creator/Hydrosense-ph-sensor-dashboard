// Update firmware alat lewat Supabase Storage (bucket publik "firmware").
// Operator mengunggah firmware dengan tools/publish_firmware.py (menulis latest.json), lalu
// tombol di sini mengirim device_commands type='ota' {file, sha256, versi}. Alat mengunduh,
// memeriksa SHA-256, memasang, dan reboot (lihat src/OtaWeb.cpp). Lewat WiFi atau GSM (GSM belum teruji).
import { supabase, DEVICE_ID, el } from "./core.js";

const BASE = "https://vupbropaxsqireeullbm.supabase.co/storage/v1/object/public/firmware/";

export function initFirmware() {
  const info = el("fwInfo");
  const btnCek = el("fwCek");
  const btnKirim = el("fwKirim");
  const log = el("fwLog");
  if (!info || !btnCek || !btnKirim || !log) return;

  let meta = null;
  let idPerintah = null;
  let timer = null;
  const tulis = (t, err) => {
    log.textContent = "> " + t;
    log.style.color = err ? "var(--orange)" : "";
  };

  async function cek() {
    btnKirim.disabled = true;
    meta = null;
    tulis("MEMBACA latest.json...");
    try {
      const r = await fetch(BASE + "latest.json?t=" + Date.now(), { cache: "no-store" });
      if (!r.ok) throw new Error("HTTP " + r.status + " (belum ada firmware di bucket?)");
      const m = await r.json();
      if (!m.file || !m.sha256 || !m.versi) throw new Error("latest.json tidak lengkap");
      meta = m;
      info.textContent = m.versi + " — " + (m.size ? (m.size / 1024).toFixed(0) + " KB" : "?");
      btnKirim.disabled = false;
      tulis("FIRMWARE SIAP: " + m.file);
    } catch (e) {
      info.textContent = "--";
      tulis("GAGAL: " + e.message, true);
    }
  }

  async function pantau() {
    if (idPerintah === null) return;
    const { data } = await supabase
      .from("device_commands")
      .select("status,pesan")
      .eq("id", idPerintah)
      .maybeSingle();
    if (!data) return;
    const label = { pending: "MENUNGGU ALAT", proses: "ALAT MENGUNDUH", sukses: "BERHASIL", gagal: "GAGAL" }[data.status] || data.status;
    tulis(label + (data.pesan ? ": " + data.pesan : ""), data.status === "gagal");
    if (data.status === "sukses" || data.status === "gagal") {
      clearInterval(timer);
      timer = null;
      idPerintah = null;
      btnKirim.disabled = !meta;
    }
  }

  async function kirim() {
    if (!meta) return;
    if (!confirm("Pasang firmware " + meta.versi + " ke alat? Pompa dimatikan dan alat reboot. Lewat WiFi atau GSM (GSM belum teruji).")) return;
    btnKirim.disabled = true;
    try {
      const { data, error } = await supabase
        .from("device_commands")
        .insert({
          device_id: DEVICE_ID,
          type: "ota",
          status: "pending",
          params: { file: meta.file, sha256: meta.sha256, versi: meta.versi },
        })
        .select("id")
        .single();
      if (error) throw error;
      idPerintah = data.id;
      tulis("PERINTAH TERKIRIM, MENUNGGU ALAT (maks ~20 dtk)");
      if (timer) clearInterval(timer);
      timer = setInterval(pantau, 3000);
    } catch (e) {
      tulis("GAGAL KIRIM: " + e.message, true);
      btnKirim.disabled = false;
    }
  }

  btnCek.addEventListener("click", cek);
  btnKirim.addEventListener("click", kirim);
  cek();
}
