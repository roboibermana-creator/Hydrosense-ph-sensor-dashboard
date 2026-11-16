// Indeks Pencemaran (IP) menurut KepMenLH No. 115 Tahun 2003.
// Baku mutu: PermenLH No. 09 Tahun 2006 (penambangan bijih nikel): TSS maks 200 mg/L, pH 6-9.
// Murni perhitungan (tanpa DOM) supaya bisa dipakai kartu, grafik, Excel, dan diuji di Node.

export const TSS_MAKS = 200;
export const PH_MIN = 6;
export const PH_MAKS = 9;
export const PH_RATA = (PH_MIN + PH_MAKS) / 2; // 7,5

// Batas atas tiap status (IP <= batas). Dipakai bersama oleh kartu, grafik, dan Excel.
export const STATUS_IP = [
  { maks: 1, kode: "baik", label: "Memenuhi baku mutu" },
  { maks: 5, kode: "ringan", label: "Cemar ringan" },
  { maks: 10, kode: "sedang", label: "Cemar sedang" },
  { maks: Infinity, kode: "berat", label: "Cemar berat" },
];
export const GARIS_IP = [1, 5, 10];

function angka(v) {
  if (v === null || v === undefined || v === "") return null;
  const n = typeof v === "number" ? v : parseFloat(String(v).replace(",", "."));
  return Number.isFinite(n) ? n : null;
}

// Rasio pH terhadap baku mutu berupa rentang (Ci/Lij)
export function rasioPh(ph) {
  const pembagi = ph <= PH_RATA ? PH_MIN - PH_RATA : PH_MAKS - PH_RATA;
  return (ph - PH_RATA) / pembagi;
}

// Rasio TSS terhadap baku mutu maksimum
export function rasioTss(tss) {
  return tss / TSS_MAKS;
}

// Jika rasio > 1: 1 + 5 log10(rasio)
export function koreksi(r) {
  return r > 1 ? 1 + 5 * Math.log10(r) : r;
}

export function statusIP(ip) {
  return STATUS_IP.find((s) => ip <= s.maks);
}

// Mengembalikan null bila pH atau TSS kosong (alat offline / belum ada data).
export function hitungIP(ph, tss) {
  const p = angka(ph);
  const t = angka(tss);
  if (p === null || t === null || t < 0) return null;
  // Rasio sudah dikoreksi log bila > 1 (itu yang dipakai di rumus IP).
  const cp = koreksi(rasioPh(p));
  const ct = koreksi(rasioTss(t));
  const maks = Math.max(cp, ct);
  const rata = (cp + ct) / 2;
  const ip = Math.sqrt((maks * maks + rata * rata) / 2);
  const st = statusIP(ip);
  return { rPh: cp, rTss: ct, ip, kode: st.kode, status: st.label };
}
