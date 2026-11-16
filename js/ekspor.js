// Ekspor data pH + TSS ke Excel (.xlsx) untuk rentang tanggal pilihan.
// Tanggal dibaca dalam WITA (UTC+8, sama dengan jam di dashboard). Pustaka SheetJS dimuat
// hanya saat tombol Unduh ditekan, jadi tidak memperlambat halaman.
import { supabase, el, numeric } from "./core.js";

const TABEL = "Hydrosense table";
const ZONA = "Asia/Makassar"; // WITA
const HALAMAN = 1000; // batas baris per permintaan Supabase
const MAKS_BARIS = 200000;
const MAKS_HARI = 366;
const SHEETJS = "https://cdn.sheetjs.com/xlsx-0.20.3/package/xlsx.mjs";

// "2026-11-09 11:07:57" (format sv-SE) dalam WITA
const fmtWaktu = new Intl.DateTimeFormat("sv-SE", {
  timeZone: ZONA,
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
  hour: "2-digit",
  minute: "2-digit",
  second: "2-digit",
  hour12: false,
});

function tanggalWita(ms) {
  return fmtWaktu.format(new Date(ms)).slice(0, 10);
}

// Awal hari (00:00 WITA) dari "YYYY-MM-DD" -> epoch ms
function awalHari(tgl) {
  return Date.parse(tgl + "T00:00:00+08:00");
}

function setStatus(teks, galat) {
  const s = el("eksporStatus");
  if (!s) return;
  s.textContent = teks;
  s.style.color = galat ? "var(--orange, #c0392b)" : "";
}

async function ambilData(mulaiMs, akhirMs) {
  const baris = [];
  for (let dari = 0; dari < MAKS_BARIS; dari += HALAMAN) {
    const { data, error } = await supabase
      .from(TABEL)
      .select("timestamp, ph, tss")
      .gte("timestamp", new Date(mulaiMs).toISOString())
      .lt("timestamp", new Date(akhirMs).toISOString())
      .or("ph.not.is.null,tss.not.is.null") // buang baris level/hujan saja
      .order("timestamp", { ascending: true })
      .range(dari, dari + HALAMAN - 1);
    if (error) throw error;
    baris.push(...data);
    setStatus("Mengambil data… " + baris.length + " baris");
    if (data.length < HALAMAN) break;
  }
  return baris;
}

async function unduh() {
  const mulai = el("eksporMulai").value;
  const akhir = el("eksporAkhir").value;
  if (!mulai || !akhir) return setStatus("Isi tanggal mulai dan tanggal akhir.", true);
  const mulaiMs = awalHari(mulai);
  const akhirMs = awalHari(akhir) + 86400000; // tanggal akhir ikut (inklusif)
  if (!(akhirMs > mulaiMs)) return setStatus("Tanggal akhir harus sama atau setelah tanggal mulai.", true);
  if ((akhirMs - mulaiMs) / 86400000 > MAKS_HARI)
    return setStatus("Rentang maksimal " + MAKS_HARI + " hari.", true);

  const tombol = el("eksporUnduh");
  tombol.disabled = true;
  try {
    setStatus("Mengambil data…");
    const data = await ambilData(mulaiMs, akhirMs);
    if (!data.length) return setStatus("Tidak ada data pH/TSS pada rentang itu.", true);

    setStatus("Membuat file Excel…");
    const XLSX = await import(SHEETJS);
    const lembar = [["Tanggal", "Jam (WITA)", "pH", "TSS (mg/L)"]];
    for (const r of data) {
      const [tgl, jam] = fmtWaktu.format(new Date(r.timestamp)).split(" ");
      lembar.push([tgl, jam, numeric(r.ph), numeric(r.tss)]);
    }
    const ws = XLSX.utils.aoa_to_sheet(lembar);
    ws["!cols"] = [{ wch: 12 }, { wch: 12 }, { wch: 8 }, { wch: 12 }];
    const wb = XLSX.utils.book_new();
    XLSX.utils.book_append_sheet(wb, ws, "pH & TSS");
    XLSX.writeFile(wb, "hydrosense_ph-tss_" + mulai + "_sd_" + akhir + ".xlsx");
    setStatus("Selesai: " + data.length + " baris diunduh.");
  } catch (e) {
    console.error("Ekspor gagal:", e);
    setStatus("Gagal: " + (e && e.message ? e.message : e), true);
  } finally {
    tombol.disabled = false;
  }
}

function isiRentang(hari) {
  const sekarang = Date.now();
  el("eksporAkhir").value = tanggalWita(sekarang);
  el("eksporMulai").value = tanggalWita(sekarang - (hari - 1) * 86400000);
}

export function initEkspor() {
  const dlg = el("eksporDialog");
  const buka = el("eksporBuka");
  if (!dlg || !buka) return;
  buka.addEventListener("click", () => {
    if (!el("eksporMulai").value) isiRentang(7);
    setStatus("");
    dlg.showModal();
  });
  el("eksporBatal").addEventListener("click", () => dlg.close());
  el("eksporUnduh").addEventListener("click", unduh);
  dlg.querySelectorAll("[data-hari]").forEach((b) =>
    b.addEventListener("click", () => isiRentang(Number(b.dataset.hari))),
  );
}
