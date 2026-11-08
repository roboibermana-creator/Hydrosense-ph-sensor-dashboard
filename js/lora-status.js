// Indikator "LoRa" di header: dibaca dari kolom lora_* baris alat di tabel `devices`
// (diisi GATEWAY Groundtank tiap 30 detik, lihat supabase/migrations/0012_lora_status.sql).
// Berlaku untuk semua peran (baca saja).
import { supabase, DEVICE_ID, el } from "./core.js";

const STATUS_BASI_MS = 90000; // laporan lebih tua dari ini = gateway/internetnya mati
const ACK_SEGAR_S = 180; // node ping tiap 60 dtk; paket terakhir lebih tua dari ini = node tidak terdengar

function tampil(st, teks, tooltip) {
  const box = el("loraStatus");
  const txt = el("loraStatusText");
  if (!box || !txt) return;
  box.dataset.st = st;
  txt.textContent = teks;
  box.title = tooltip;
}

async function poll() {
  try {
    const { data, error } = await supabase
      .from("devices")
      .select("lora_status, lora_rssi, lora_ack_age_s, lora_status_at")
      .eq("project", "hydrosense")
      .eq("name", DEVICE_ID)
      .maybeSingle();
    if (error) throw error;
    if (!data || !data.lora_status) {
      tampil("offline", "BELUM TERPASANG", "Belum ada laporan LoRa dari gateway (firmware lama atau migrasi 0012 belum dijalankan)");
      return;
    }
    const umur = Date.now() - Date.parse(data.lora_status_at);
    if (!(umur < STATUS_BASI_MS)) {
      tampil("offline", "GATEWAY OFFLINE", "Laporan status LoRa terakhir dari gateway sudah lama (gateway atau internetnya mati)");
      return;
    }
    if (data.lora_status === "nonaktif") {
      tampil("offline", "NONAKTIF", "LoRa gateway dimatikan di firmware (LORA_ENABLED=0)");
    } else if (data.lora_status === "mati") {
      tampil("alarm", "MODUL TIDAK SIAP", "Modul LoRa gateway tidak merespons saat boot (cek kabel, catu 5 V, antena)");
    } else if (data.lora_ack_age_s !== null && data.lora_ack_age_s !== undefined && data.lora_ack_age_s < ACK_SEGAR_S) {
      const rssi = data.lora_rssi !== null && data.lora_rssi !== undefined ? ` · ${data.lora_rssi} dBm` : "";
      tampil("ok", "TERHUBUNG" + rssi, `Paket terakhir dari node ${data.lora_ack_age_s} detik yang lalu`);
    } else {
      tampil("warn", "NODE TIDAK TERDENGAR", "Gateway aktif, tetapi tidak ada paket dari node (node mati, jauh, atau antena)");
    }
  } catch (e) {
    console.error("Gagal baca status LoRa:", e);
  }
}

export function initLoraStatus() {
  poll();
  setInterval(poll, 5000);
}
