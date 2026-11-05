// ---------------- Kalibrasi TSS (slope & offset) ----------------
// Faktor Y = slope*X + offset dihitung dari tabel calibration (sensor=tss, aksi=kalibrasi)
// (regresi linear least squares, sama seperti calibrateLinearRegression()
// di firmware) atau diisi manual, lalu disimpan ke calibration (aksi=faktor).
// ESP32 membaca baris terbaru tabel itu secara berkala dan menerapkannya.
import { supabase, DEVICE_ID, el, numeric, state } from "./core.js";

export function initCalibrationTss() {

  function setTssCalLog(text, isError) {
    const box = el("tssCalLog");
    if (!box) return;
    box.textContent = "> " + text;
    box.style.color = isError ? "var(--orange)" : "";
  }

  async function muatKalibrasiTssAktif() {
    const { data, error } = await supabase
      .from("calibration")
      .select("slope, cal_offset, source, created_at")
      .eq("sensor", "tss")
      .eq("aksi", "faktor")
      .order("id", { ascending: false })
      .limit(1);

    const slopeEl = el("tssCalSlopeNow");
    const offsetEl = el("tssCalOffsetNow");
    const metaEl = el("tssCalMetaNow");

    if (error || !data || data.length === 0) {
      if (slopeEl) slopeEl.textContent = "1.000000";
      if (offsetEl) offsetEl.textContent = "0.000000";
      if (metaEl)
        metaEl.textContent =
          "! BELUM ADA KALIBRASI (default slope=1, offset=0)";
      return;
    }

    const row = data[0];
    if (slopeEl) slopeEl.textContent = Number(row.slope).toFixed(6);
    if (offsetEl)
      offsetEl.textContent = Number(row.cal_offset).toFixed(6);
    if (metaEl) {
      const waktu = row.created_at
        ? new Date(row.created_at).toLocaleString("id-ID", {
            timeZone: "Asia/Makassar",
          })
        : "-";
      metaEl.textContent =
        (row.source === "manual" ? "MANUAL" : "REGRESI OTOMATIS") +
        " · " +
        waktu;
    }
  }

  function leastSquares(points) {
    // Y = slope*X + offset -- rumus sama dgn calibrateLinearRegression()
    // di TSS_Sensor.cpp (firmware)
    const n = points.length;
    let sumX = 0,
      sumY = 0,
      sumXY = 0,
      sumX2 = 0;
    for (const p of points) {
      sumX += p.x;
      sumY += p.y;
      sumXY += p.x * p.y;
      sumX2 += p.x * p.x;
    }
    const denom = n * sumX2 - sumX * sumX;
    if (denom === 0) return null;
    const slope = (n * sumXY - sumX * sumY) / denom;
    const offset = (sumY - slope * sumX) / n;
    return { slope, offset };
  }

  async function hitungKalibrasiTssOtomatis() {
    const btn = el("btnHitungKalibrasiTss");
    if (btn) btn.disabled = true;
    setTssCalLog("Mengambil sample kalibrasi...", false);
    try {
      const { data, error } = await supabase
        .from("calibration")
        .select("standard:titik, tss:nilai, raw_value")
        .eq("sensor", "tss")
        .eq("aksi", "kalibrasi");
      if (error) throw error;

      // Rata-ratakan tiap titik standard (std_a/b/c) dulu supaya noise
      // antar-sample tidak ikut merusak garis regresi, baru fit garis
      // lurus lewat titik rata-rata tsb.
      const grup = {};
      for (const row of data || []) {
        const raw = numeric(row.raw_value);
        const actual = numeric(row.tss);
        if (raw === null || actual === null) continue;
        if (!grup[row.standard])
          grup[row.standard] = { sumRaw: 0, sumActual: 0, n: 0 };
        grup[row.standard].sumRaw += raw;
        grup[row.standard].sumActual += actual;
        grup[row.standard].n += 1;
      }

      const titik = Object.values(grup)
        .filter((g) => g.n > 0)
        .map((g) => ({ x: g.sumRaw / g.n, y: g.sumActual / g.n }));

      if (titik.length < 2) {
        setTssCalLog(
          "Gagal: butuh minimal 2 titik standard dengan sample lengkap (raw_value & tss terisi).",
          true,
        );
        return;
      }

      const hasil = leastSquares(titik);
      if (!hasil) {
        setTssCalLog(
          "Gagal: raw_value pada semua titik sama (tidak ada variasi).",
          true,
        );
        return;
      }

      const { error: insertError } = await supabase
        .from("calibration")
        .insert({
          device_id: DEVICE_ID,
          sensor: "tss",
          aksi: "faktor",
          slope: hasil.slope,
          cal_offset: hasil.offset,
          source: "regression",
          points_count: titik.length,
        });
      if (insertError) throw insertError;

      setTssCalLog(
        `Berhasil: Y = ${hasil.slope.toFixed(6)}X + ${hasil.offset.toFixed(6)} (dari ${titik.length} titik standard)`,
        false,
      );
      await muatKalibrasiTssAktif();
    } catch (err) {
      setTssCalLog("Gagal hitung kalibrasi: " + err.message, true);
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function simpanKalibrasiTssManual() {
    const btn = el("btnSimpanManualTss");
    const slope = Number(el("inputManualSlope")?.value);
    const offset = Number(el("inputManualOffset")?.value);
    if (!Number.isFinite(slope) || !Number.isFinite(offset)) {
      setTssCalLog("Isi slope & offset dengan angka yang valid.", true);
      return;
    }
    if (btn) btn.disabled = true;
    try {
      const { error } = await supabase
        .from("calibration")
        .insert({
          device_id: DEVICE_ID,
          sensor: "tss",
          aksi: "faktor",
          slope,
          cal_offset: offset,
          source: "manual",
        });
      if (error) throw error;
      setTssCalLog(
        `Kalibrasi manual disimpan: Y = ${slope}X + ${offset}`,
        false,
      );
      await muatKalibrasiTssAktif();
    } catch (err) {
      setTssCalLog("Gagal simpan manual: " + err.message, true);
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  if (el("btnHitungKalibrasiTss")) {
    el("btnHitungKalibrasiTss").addEventListener(
      "click",
      hitungKalibrasiTssOtomatis,
    );
  }
  if (el("btnSimpanManualTss")) {
    el("btnSimpanManualTss").addEventListener(
      "click",
      simpanKalibrasiTssManual,
    );
  }

  // -------- Input sample kalibrasi (raw sensor + gravimetri manual) --------
  function setTssSampleLog(text, isError) {
    const box = el("tssSampleLog");
    if (!box) return;
    box.textContent = "> " + text;
    box.style.color = isError ? "var(--orange)" : "";
  }

  function tampilkanRawTssTerkini() {
    const rawEl = el("tssCalRawNow");
    if (!rawEl) return;
    const raw = state.hb?.tssRaw;
    rawEl.textContent = raw === null || raw === undefined
      ? "--"
      : raw.toFixed(2) + " mg/L";
  }
  // Bacaan raw datang dari refreshDashboard() (siklus lain) -- cukup
  // dipoll pelan di sini supaya panel ini selalu ikut update.
  setInterval(tampilkanRawTssTerkini, 2000);
  tampilkanRawTssTerkini();

  async function simpanSampleKalibrasiTss() {
    const btn = el("btnSimpanSampleTss");
    const standard = el("selectStandardTss")?.value;
    const gravimetri = Number(el("inputGravimetriTss")?.value);

    // Prioritaskan input manual (dibaca dari Serial Monitor 'T') --
    // tidak tergantung ESP32 harus online/berhasil kirim ke
    // Hydrosense table dulu, yang sering gagal kalau WiFi tidak stabil.
    const rawManualStr = el("inputRawManualTss")?.value;
    const rawManual =
      rawManualStr !== "" && rawManualStr !== undefined
        ? Number(rawManualStr)
        : null;
    const raw =
      rawManual !== null && Number.isFinite(rawManual)
        ? rawManual
        : state.hb?.tssRaw;

    if (raw === null || raw === undefined) {
      setTssSampleLog(
        "Raw TSS belum tersedia -- isi 'Raw TSS Manual' (baca dari Serial Monitor 'T'), atau pastikan alat online & sudah kirim data.",
        true,
      );
      return;
    }
    if (!Number.isFinite(gravimetri)) {
      setTssSampleLog("Isi nilai gravimetri dengan angka yang valid.", true);
      return;
    }

    if (btn) btn.disabled = true;
    try {
      // sample_index harus 1-20 (constraint tabel) -- pakai jumlah
      // sample "kalibrasi" yang sudah ada untuk standard ini + 1.
      const { count, error: countError } = await supabase
        .from("calibration")
        .select("id", { count: "exact", head: true })
        .eq("sensor", "tss")
        .eq("titik", standard)
        .eq("aksi", "kalibrasi");
      if (countError) throw countError;

      const sampleIndex = (count || 0) + 1;
      if (sampleIndex > 20) {
        setTssSampleLog(
          `Sudah ada 20 sample untuk ${standard}. Hapus sample lama dulu di Supabase kalau mau ulangi titik ini.`,
          true,
        );
        return;
      }

      const { error } = await supabase
        .from("calibration")
        .insert({
          device_id: DEVICE_ID,
          sensor: "tss",
          titik: standard,
          aksi: "kalibrasi",
          sample_index: sampleIndex,
          nilai: gravimetri,
          suhu: state.latestSensorSnapshot?.temp ?? 0,
          raw_value: raw,
        });
      if (error) throw error;

      setTssSampleLog(
        `Sample #${sampleIndex} untuk ${standard} disimpan (raw=${raw.toFixed(2)}, gravimetri=${gravimetri}).`,
        false,
      );
      if (el("inputGravimetriTss")) el("inputGravimetriTss").value = "";
      if (el("inputRawManualTss")) el("inputRawManualTss").value = "";
    } catch (err) {
      setTssSampleLog("Gagal simpan sample: " + err.message, true);
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  if (el("btnSimpanSampleTss")) {
    el("btnSimpanSampleTss").addEventListener(
      "click",
      simpanSampleKalibrasiTss,
    );
  }

  muatKalibrasiTssAktif();
}
