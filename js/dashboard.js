// Dashboard Utama: ambil data sensor, kartu KPI, grafik, alarm, polling.
import { supabase, numeric, parseTimestamp, formatTime, state } from "./core.js";
import { registerHover } from "./chart.js";

async function getLatestSensorData() {
  const { data, error } = await supabase
    .from("Hydrosense table")
    .select(
      "id, timestamp, ph, probe_mv, tss, water_temperature, water_level",
    )
    .not("tss", "is", null) // tabel ini juga berisi baris level & hujan
    .order("timestamp", { ascending: false })
    .limit(1)
    .maybeSingle();
  if (error) throw new Error(`Supabase latest error: ${error.message}`);
  return data;
}

async function getSensorHistory() {
  // 120 pembacaan TERAKHIR, tanpa melihat jam (tetap tampil walau alat
  // sedang offline). Sebelumnya order ascending + limit(50) mengambil 50
  // baris paling LAMA di tabel, jadi rata-rata dan grafik tidak pernah
  // ikut data terbaru. Diambil descending lalu dibalik ke urutan waktu.
  const { data, error } = await supabase
    .from("Hydrosense table")
    .select("id, timestamp, ph, tss, water_temperature, water_level")
    .not("tss", "is", null)
    .order("timestamp", { ascending: false })
    .limit(120);
  if (error) throw new Error(`Supabase history error: ${error.message}`);
  return (data || []).reverse();
}

// Level air dikirim ESP32 terpisah tiap 5 detik ke tabel sendiri, supaya
// tetap real-time walau sensor pH/TSS mati. 60 baris terbaru = ~5 menit.
async function getLevelHistory() {
  const { data, error } = await supabase
    .from("Hydrosense table")
    .select("timestamp, water_level, distance_cm")
    .not("distance_cm", "is", null) // baris level-only
    .order("timestamp", { ascending: false })
    .limit(60);
  if (error) throw new Error(`Supabase level error: ${error.message}`);
  return (data || []).reverse().map((row) => ({
    timestamp: row.timestamp,
    wl: numeric(row.water_level),
    dist: numeric(row.distance_cm),
  }));
}

// Riwayat curah hujan (baris rain_* di tabel yang sama) untuk tab "Hujan".
async function getRainHistory() {
  const { data, error } = await supabase
    .from("Hydrosense table")
    .select("timestamp, rain_mm_per_hour")
    .not("rain_mm_per_hour", "is", null)
    .order("timestamp", { ascending: false })
    .limit(120);
  if (error) throw new Error(`Supabase rain error: ${error.message}`);
  return (data || []).reverse().map((row) => ({
    timestamp: row.timestamp,
    rain: numeric(row.rain_mm_per_hour),
  }));
}

// Timpa kartu level (+ jarak) dengan data tabel level, dengan status
// online sendiri -- tidak ikut "-" saat pH/TSS tidak ada data baru.
function updateLevel(levelEntries) {
  const latest = levelEntries[levelEntries.length - 1];
  const latestMs = parseTimestamp(latest?.timestamp);
  const fresh = latestMs !== null && Date.now() - latestMs < 30000;
  const wl = fresh ? latest.wl : null;
  const dist = fresh ? latest.dist : null;

  if (wl !== null) {
    document.querySelector("#wlNow").textContent = wl.toFixed(0);
    document.querySelector("#wlNowSummary").textContent = wl.toFixed(0);
    document.querySelector("#wlStatus").textContent =
      wl < 20 ? "LOW" : wl > 90 ? "HIGH" : "SAFE";
    document.querySelector("#wlStatusSummaryText").innerHTML =
      'LIVE <span class="signal" style="margin-left:5px;">&#9679;</span>';
  } else {
    document.querySelector("#wlNow").textContent = "-";
    document.querySelector("#wlNowSummary").textContent = "-";
    document.querySelector("#wlStatus").textContent = "-";
    document.querySelector("#wlStatusSummaryText").innerHTML = "-";
  }
  const distTxt = dist !== null ? dist.toFixed(1) : "-";
  document.querySelector("#distNow").textContent = distTxt;
  document.querySelector("#distNowSummary").textContent = distTxt;

  paintChart(levelEntries, "#wlChart");

  const vals = levelEntries
    .map((e) => e.wl)
    .filter((v) => v !== null);
  const avg = vals.length
    ? (vals.reduce((a, b) => a + b, 0) / vals.length).toFixed(0) + "%"
    : "0%";
  document.querySelector("#avgWl").textContent = avg;
  document.querySelector("#avgWlSummary").textContent = avg;
  document.querySelector("#avgWlCtrl").textContent = avg;
}

function normalizeSensorRow(row) {
  return {
    id: row?.id ?? null,
    timestamp: row?.timestamp ?? null,
    ph: numeric(row?.ph),
    temp: numeric(row?.water_temperature),
    tss: numeric(row?.tss),
    tssRaw: null, // raw TSS tidak di tabel utama; wizard kalibrasi memakai state.hb.tssRaw (tabel devices)
    wl: numeric(row?.water_level),
    voltage: numeric(row?.probe_mv),
    ntu: null,
    dist: null,
  };
}

function updateClock() {
  const now = new Date();
  document.querySelector("#dateNow").textContent =
    `DATE // ${now.toLocaleDateString("id-ID", { timeZone: "Asia/Makassar" })}`;
  document.querySelector("#timeNow").textContent =
    `${now.toLocaleTimeString("id-ID", { timeZone: "Asia/Makassar", hour12: false })} WITA`;
}

// Data terakhir untuk menggambar ulang grafik halaman pH/TSS/Level. Kanvas yang tersembunyi
// berukuran 0, jadi grafik harus digambar ulang saat halamannya dibuka atau jendela diubah
// (kalau tidak, garisnya baru muncul di refresh 5 detik berikutnya).
let lastChartData = null;
function repaintPageCharts() {
  if (!lastChartData) return;
  const { entries, levelEntries } = lastChartData;
  paintChart(entries, "#teleChart");
  paintChart(entries, "#tssChart");
  paintChart(levelEntries.length ? levelEntries : entries, "#wlChart");
}

function paintChart(entries, canvasId) {
  const canvas = document.querySelector(canvasId);
  if (!canvas) return;
  const rect = canvas.getBoundingClientRect(),
    ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(rect.width * ratio));
  canvas.height = Math.max(1, Math.floor(rect.height * ratio));
  const ctx = canvas.getContext("2d");
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);

  let mapFn;
  if (canvasId === "#teleChart") mapFn = (e) => numeric(e.ph);
  else if (canvasId === "#tssChart") mapFn = (e) => numeric(e.tss);
  else if (canvasId === "#wlChart") mapFn = (e) => numeric(e.wl);

  const points = entries
    .map(mapFn)
    .filter((value) => value !== null && value !== undefined);
  if (!points.length) {
    ctx.fillStyle = "#5b6670";
    ctx.font = "13px 'Segoe UI', Arial, sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("Belum ada data", rect.width / 2, rect.height / 2);
    registerHover(canvas, []);
    return;
  }

  const low = Math.min(...points),
    high = Math.max(...points),
    padding = Math.max(0.05, (high - low) * 0.35);
  let min = low - padding,
    max = high + padding;
  if (canvasId === "#tssChart" || canvasId === "#wlChart") {
    if (max - min < 1) {
      max += 10;
      min = Math.max(0, min - 10);
    }
  }

  const width = rect.width,
    height = rect.height,
    left = 43,
    right = 18,
    top = 12,
    bottom = 31;

  // Label sumbu Y mengikuti skala otomatis di semua grafik
  const wrap = canvas.closest(".chart-wrap");
  const desimal = canvasId === "#teleChart" ? 2 : 1;
  const setAxis = (sel, text) => {
    const el = wrap && wrap.querySelector(sel);
    if (el) el.textContent = text;
  };
  setAxis(".y-top", max.toFixed(desimal));
  setAxis(".y-mid", ((min + max) / 2).toFixed(desimal));
  setAxis(".y-bottom", min.toFixed(desimal));

  if (canvasId === "#teleChart") {
    document.querySelector("#chartStart").textContent = formatTime(
      entries[0].timestamp,
    ).slice(0, 5);
    document.querySelector("#chartMiddle").textContent = formatTime(
      entries[Math.floor(entries.length / 2)].timestamp,
    ).slice(0, 5);
    document.querySelector("#chartEnd").textContent = formatTime(
      entries.at(-1).timestamp,
    ).slice(0, 5);
  }

  ctx.beginPath();
  ctx.strokeStyle = "#0b5cad";
  ctx.lineWidth = 2;
  ctx.shadowBlur = 0;
  const hoverPts = [];
  entries.forEach((entry, index) => {
    const value = mapFn(entry);
    if (value === null || value === undefined) return;
    const x =
      left +
      (index / Math.max(entries.length - 1, 1)) * (width - left - right);
    const y =
      top + ((max - value) / (max - min)) * (height - top - bottom);
    index ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    hoverPts.push({ x, y, v: value, t: parseTimestamp(entry.timestamp) });
  });
  ctx.stroke();
  // Tooltip nilai saat kursor di atas grafik (sama seperti grafik Dashboard Utama).
  registerHover(canvas, hoverPts, {
    decimals: desimal,
    unit: canvasId === "#tssChart" ? " mg/L" : canvasId === "#wlChart" ? " %" : "",
  });

  // Titik data terakhir ditandai supaya nilai terkini mudah dilihat
  const lastIdx = entries.findLastIndex(
    (e) => mapFn(e) !== null && mapFn(e) !== undefined,
  );
  if (lastIdx >= 0) {
    const v = mapFn(entries[lastIdx]);
    const x =
      left +
      (lastIdx / Math.max(entries.length - 1, 1)) *
        (width - left - right);
    const y = top + ((max - v) / (max - min)) * (height - top - bottom);
    ctx.beginPath();
    ctx.fillStyle = "#0b5cad";
    ctx.arc(x, y, 3.5, 0, Math.PI * 2);
    ctx.fill();
  }
}

function updateLatest(latest, isConnected) {
  if (!isConnected) {
    const els = [
      "#phNow",
      "#phStatus",
      "#voltNow",
      "#tempNow",
      "#tempStatus",
      "#phNowSummary",
      "#phStatusSummary",
      "#tssNow",
      "#tssStatus",
      "#tssNowSummary",
      "#tssStatusSummary",
      "#ntuNow",
      "#wlNow",
      "#wlStatus",
      "#wlNowSummary",
      "#wlStatusSummary",
      "#distNow",
    ];
    els.forEach((selector) => {
      const el = document.querySelector(selector);
      if (el) el.textContent = "-";
    });
    document.querySelector("#phStatusSummaryText").innerHTML = "-";
    document.querySelector("#tssStatusSummaryText").innerHTML = "-";
    document.querySelector("#wlStatusSummaryText").innerHTML = "-";
    document.querySelector("#voltNowSummary").textContent = "-";
    document.querySelector("#tempNowSummary").textContent = "-";
    document.querySelector("#ntuNowSummary").textContent = "-";
    document.querySelector("#distNowSummary").textContent = "-";

    document.querySelector("#lastUpdate").textContent = formatTime(
      latest?.timestamp,
    );
    document
      .querySelectorAll(".lastUpdateGeneric")
      .forEach((el) => (el.textContent = formatTime(latest?.timestamp)));
    return;
  }

  const ph = numeric(latest?.ph);
  const temp = numeric(latest?.temp);
  const tss = numeric(latest?.tss);
  const wl = numeric(latest?.wl);

  // pH Sensor
  if (ph !== null) {
    const txt = ph.toFixed(2);
    const st = ph < 6.5 ? "ACIDIC" : ph > 7.5 ? "ALKALINE" : "NEUTRAL";
    document.querySelector("#phNow").textContent = txt;
    document.querySelector("#phStatus").textContent = st;
    document.querySelector("#phNowSummary").textContent = txt;
    document.querySelector("#phStatusSummaryText").innerHTML =
      'LIVE <span class="signal" style="margin-left:5px;">&#9679;</span>';
  } else {
    document.querySelector("#phNow").textContent = "-";
    document.querySelector("#phStatus").textContent = "-";
    document.querySelector("#phNowSummary").textContent = "-";
    document.querySelector("#phStatusSummaryText").innerHTML = "-";
  }
  if (temp !== null) {
    const tempSt = temp < 20 ? "COLD" : temp > 32 ? "HOT" : "NORMAL";
    document.querySelector("#tempNow").textContent = temp.toFixed(1);
    document.querySelector("#tempStatus").textContent = tempSt;
    document.querySelector("#tempNowSummary").textContent =
      temp.toFixed(1) + " \u00B0C";
  } else {
    document.querySelector("#tempNow").textContent = "-";
    document.querySelector("#tempStatus").textContent = "-";
    document.querySelector("#tempNowSummary").textContent = "-";
  }

  // TSS Sensor
  if (tss !== null) {
    document.querySelector("#tssNow").textContent = tss.toFixed(1);
    document.querySelector("#tssNowSummary").textContent = tss.toFixed(1);
    const tssSt = tss < 50 ? "NORMAL" : "HIGH";
    document.querySelector("#tssStatus").textContent = tssSt;
    document.querySelector("#tssStatusSummaryText").innerHTML =
      'LIVE <span class="signal" style="margin-left:5px;">&#9679;</span>';
  } else {
    document.querySelector("#tssNow").textContent = "-";
    document.querySelector("#tssNowSummary").textContent = "-";
    document.querySelector("#tssStatus").textContent = "-";
    document.querySelector("#tssStatusSummaryText").innerHTML = "-";
  }

  // Water Level Sensor
  if (wl !== null) {
    document.querySelector("#wlNow").textContent = wl.toFixed(0);
    document.querySelector("#wlNowSummary").textContent = wl.toFixed(0);
    const wlSt = wl < 20 ? "LOW" : wl > 90 ? "HIGH" : "SAFE";
    document.querySelector("#wlStatus").textContent = wlSt;
    document.querySelector("#wlStatusSummaryText").innerHTML =
      'LIVE <span class="signal" style="margin-left:5px;">&#9679;</span>';
  } else {
    document.querySelector("#wlNow").textContent = "-";
    document.querySelector("#wlNowSummary").textContent = "-";
    document.querySelector("#wlStatus").textContent = "-";
    document.querySelector("#wlStatusSummaryText").innerHTML = "-";
  }

  // NTU & jarak belum tersedia di database
  const mv = numeric(latest?.voltage);
  document.querySelector("#voltNow").textContent =
    mv !== null ? mv.toFixed(1) : "-";
  document.querySelector("#voltNowSummary").textContent =
    mv !== null ? mv.toFixed(1) + " mV" : "-";
  document.querySelector("#ntuNow").textContent = "-";
  document.querySelector("#ntuNowSummary").textContent = "-";
  document.querySelector("#distNow").textContent = "-";
  document.querySelector("#distNowSummary").textContent = "-";

  document.querySelector("#lastUpdate").textContent = formatTime(
    latest?.timestamp,
  );
  document
    .querySelectorAll(".lastUpdateGeneric")
    .forEach((el) => (el.textContent = formatTime(latest?.timestamp)));
}

async function refreshDashboard() {
  try {
    const [latestRow, historyRows, levelEntries, rainEntries] = await Promise.all([
      getLatestSensorData(),
      getSensorHistory(),
      getLevelHistory().catch((err) => {
        console.error(err);
        return [];
      }),
      getRainHistory().catch((err) => {
        console.error(err);
        return [];
      }),
    ]);
    hmiState.rainEntries = rainEntries;
    const latest = normalizeSensorRow(latestRow);
    state.latestSensorSnapshot = latest;
    const entries = historyRows
      .map(normalizeSensorRow)
      .filter((entry) => entry.timestamp)
      .sort(
        (a, b) =>
          parseTimestamp(a.timestamp) - parseTimestamp(b.timestamp),
      );

    const latestMs = parseTimestamp(latest?.timestamp);
    const isConnected =
      latestMs !== null && Date.now() - latestMs < 60000;

    // Tampilkan pembacaan TERAKHIR walau alat offline (status koneksi
    // tetap ditandai terpisah di indikator atas).
    updateLatest(latest || {}, !!latest);

    lastChartData = { entries, levelEntries };
    paintChart(entries, "#teleChart");
    paintChart(entries, "#tssChart");
    paintChart(entries, "#wlChart");

    document.querySelector("#readingCount").textContent = String(
      entries.length,
    ).padStart(6, "0");
    document.querySelector("#sessionReadings").textContent =
      entries.length;
    document.querySelector("#sessionReadingsSummary").textContent =
      entries.length;
    document.querySelector("#sessionReadingsTssSummary").textContent =
      entries.length;
    document.querySelector("#sessionReadingsCtrl").textContent =
      entries.length;
    document.querySelector("#sessionReadingsTssCtrl").textContent =
      entries.length;

    const phEntries = entries.filter((e) => numeric(e.ph) !== null);
    document.querySelector("#avgPh").textContent = phEntries.length
      ? (
          phEntries.reduce(
            (total, entry) => total + Number(entry.ph),
            0,
          ) / phEntries.length
        ).toFixed(2)
      : "--";
    document.querySelector("#avgPhSummary").textContent =
      document.querySelector("#avgPh").textContent;
    document.querySelector("#avgPhCtrl").textContent =
      document.querySelector("#avgPh").textContent;

    const tempEntries = entries.filter(
      (e) => numeric(e.temp ?? e.temperature) !== null,
    );
    document.querySelector("#avgTemp").textContent = tempEntries.length
      ? (
          tempEntries.reduce(
            (total, entry) =>
              total + Number(entry.temp ?? entry.temperature),
            0,
          ) / tempEntries.length
        ).toFixed(1) + " \u00B0C"
      : "--";

    const tssEntries = entries.filter((e) => numeric(e.tss) !== null);
    document.querySelector("#avgTss").textContent = tssEntries.length
      ? (
          tssEntries.reduce(
            (total, entry) => total + Number(entry.tss),
            0,
          ) / tssEntries.length
        ).toFixed(1)
      : "0";
    document.querySelector("#avgTssSummary").textContent =
      document.querySelector("#avgTss").textContent + " mg/L";
    document.querySelector("#avgTssCtrl").textContent =
      document.querySelector("#avgTssSummary").textContent;

    const wlEntries = entries.filter((e) => numeric(e.wl) !== null);
    document.querySelector("#avgWl").textContent = wlEntries.length
      ? (
          wlEntries.reduce(
            (total, entry) => total + Number(entry.wl),
            0,
          ) / wlEntries.length
        ).toFixed(0) + "%"
      : "0%";
    document.querySelector("#avgWlSummary").textContent =
      document.querySelector("#avgWl").textContent;
    document.querySelector("#avgWlCtrl").textContent =
      document.querySelector("#avgWl").textContent;

    // Ada data di tabel level baru -> pakai itu untuk kartu/grafik level
    if (levelEntries.length) updateLevel(levelEntries);

    updateDashboardHmi(latest, entries, levelEntries, isConnected);

    if (isConnected) {
      document.querySelector("#connectionStatus").textContent =
        "SUPABASE ONLINE";
      document.querySelector("#wifiIcon").className =
        "pixel-icon wifi-icon is-online";
    } else {
      document.querySelector("#connectionStatus").textContent =
        "NO RECENT DATA";
      document.querySelector("#wifiIcon").className =
        "pixel-icon wifi-icon is-offline";
    }
    document
      .querySelectorAll(".chart-line")
      .forEach((el) => (el.hidden = true));
  } catch (error) {
    document.querySelector("#connectionStatus").textContent =
      "SUPABASE OFFLINE";
    document.querySelector("#wifiIcon").className =
      "pixel-icon wifi-icon is-offline";
    console.error("Supabase refresh failed:", error);
  }
}

updateClock();
// ===================== Dashboard Utama (HMI) =====================
// Batas baku mutu PermenLH 9/2006 (nikel) + setpoint level.
const HMI_LIMIT = {
  ph: { min: 6, max: 9, label: "pH terukur", unit: "", dec: 2 },
  tss: { max: 200, label: "TSS (mg/L)", unit: " mg/L", dec: 1 },
  wl: { min: 20, max: 90, label: "Level (%)", unit: " %", dec: 0 },
  // Tidak ada baku mutu hujan; garis batas = awal kategori "lebat" BMKG (10 mm/jam).
  rain: { max: 10, label: "Curah hujan (mm/jam)", unit: " mm/jam", dec: 1 },
};
const hmiState = { metric: "ph", entries: [], levelEntries: [], rainEntries: [], pts: [] };

export function hmiSetKpi(cardId, pillId, status, text) {
  const card = document.getElementById(cardId);
  const pill = document.getElementById(pillId);
  if (card) card.dataset.st = status;
  if (pill) {
    pill.textContent = text;
    pill.className = "hmi-pill " + status;
  }
}

function hmiStatusPh(v) {
  if (v === null) return ["offline", "NO DATA"];
  if (v < HMI_LIMIT.ph.min || v > HMI_LIMIT.ph.max) return ["alarm", "DI LUAR BAKU MUTU"];
  if (v < 6.3 || v > 8.7) return ["warn", "WASPADA"];
  return ["ok", "NORMAL"];
}
function hmiStatusTss(v) {
  if (v === null) return ["offline", "NO DATA"];
  if (v > HMI_LIMIT.tss.max) return ["alarm", "DI LUAR BAKU MUTU"];
  if (v >= HMI_LIMIT.tss.max * 0.8) return ["warn", "WASPADA"];
  return ["ok", "NORMAL"];
}
function hmiStatusWl(v) {
  if (v === null) return ["offline", "NO DATA"];
  if (v >= HMI_LIMIT.wl.max) return ["alarm", "HIGH"];
  if (v <= HMI_LIMIT.wl.min) return ["warn", "LOW"];
  return ["ok", "AMAN"];
}

function hmiNum(sel) {
  const el = document.querySelector(sel);
  if (!el) return null;
  const n = parseFloat(String(el.textContent).replace(",", "."));
  return Number.isFinite(n) ? n : null;
}

function updateDashboardHmi(latest, entries, levelEntries, isConnected) {
  hmiState.entries = entries;
  hmiState.levelEntries = levelEntries;

  // Nilai diambil dari elemen yang sudah diisi updateLatest()/updateLevel()
  // supaya aturan "data basi = -" tetap satu sumber.
  const ph = hmiNum("#phNowSummary");
  const tss = hmiNum("#tssNowSummary");
  const wl = hmiNum("#wlNowSummary");

  const [phSt, phTxt] = hmiStatusPh(ph);
  const [tssSt, tssTxt] = hmiStatusTss(tss);
  const [wlSt, wlTxt] = hmiStatusWl(wl);
  hmiSetKpi("kpiPh", "phPill", phSt, phTxt);
  hmiSetKpi("kpiTss", "tssPill", tssSt, tssTxt);
  hmiSetKpi("kpiWl", "wlPill", wlSt, wlTxt);

  const phMark = document.getElementById("phMark");
  if (phMark) {
    phMark.hidden = ph === null;
    if (ph !== null) phMark.style.left = Math.min(100, Math.max(0, (ph / 14) * 100)) + "%";
  }
  const tssFill = document.getElementById("tssFill");
  if (tssFill) {
    const pct = tss === null ? 0 : Math.min(100, (tss / HMI_LIMIT.tss.max) * 100);
    tssFill.style.width = pct + "%";
    document.getElementById("tssScaleText").textContent =
      tss === null ? "batas 200 mg/L" : `${pct.toFixed(0)}% dari batas 200 mg/L`;
  }
  const wlFill = document.getElementById("wlFill");
  if (wlFill) wlFill.style.width = (wl === null ? 0 : Math.min(100, wl)) + "%";

  const lastMs = parseTimestamp(latest?.timestamp);
  document.getElementById("dashLastUpdate").textContent =
    lastMs === null ? "--:--:--" : formatTime(latest.timestamp);

  hmiRenderAlarms({ ph, tss, wl, phSt, tssSt, wlSt, isConnected, lastMs });
  hmiPaintChart();
}

function hmiRenderAlarms(s) {
  const list = document.getElementById("alarmList");
  if (!list) return;
  const items = [];
  if (!s.isConnected)
    items.push(["alarm", "Tidak ada data baru dari ESP32",
      s.lastMs ? "Data terakhir " + formatTime(s.lastMs) + " WITA" : "Belum pernah menerima data"]);
  if (s.phSt === "alarm") items.push(["alarm", `pH di luar baku mutu (${s.ph.toFixed(2)})`, "Batas 6.0 – 9.0"]);
  else if (s.phSt === "warn") items.push(["warn", `pH mendekati batas (${s.ph.toFixed(2)})`, "Batas 6.0 – 9.0"]);
  if (s.tssSt === "alarm") items.push(["alarm", `TSS melebihi baku mutu (${s.tss.toFixed(0)} mg/L)`, "Batas 200 mg/L"]);
  else if (s.tssSt === "warn") items.push(["warn", `TSS mendekati batas (${s.tss.toFixed(0)} mg/L)`, "≥ 80% dari 200 mg/L"]);
  if (s.wlSt === "alarm") items.push(["alarm", `Level tangki tinggi (${s.wl.toFixed(0)}%)`, "Setpoint HIGH 90%"]);
  else if (s.wlSt === "warn") items.push(["warn", `Level tangki rendah (${s.wl.toFixed(0)}%)`, "Setpoint LOW 20%"]);

  const active = items.filter((i) => i[0] !== "info").length;
  const badge = document.getElementById("alarmCount");
  badge.textContent = active ? `${active} AKTIF` : "NORMAL";
  badge.className = "hmi-pill " + (items.some((i) => i[0] === "alarm") ? "alarm" : active ? "warn" : "ok");

  if (!items.length) items.push(["ok", "Semua parameter dalam batas normal", "Diperbarui otomatis tiap 5 detik"]);
  const jam = formatTime(Date.now()).slice(0, 5);
  list.innerHTML = items
    .map(([sev, title, sub]) =>
      `<li class="al"><i class="sev ${sev}"></i><div class="txt"><b>${title}</b><span>${sub}</span></div><time>${jam}</time></li>`)
    .join("");
}

// Grafik tren dengan batas baku mutu, rentang aman, dan tooltip hover.
function hmiPaintChart() {
  const canvas = document.getElementById("dashChart");
  if (!canvas || !canvas.offsetParent) return;
  const m = hmiState.metric;
  const lim = HMI_LIMIT[m];
  const src =
    m === "rain" ? hmiState.rainEntries : m === "wl" && hmiState.levelEntries.length ? hmiState.levelEntries : hmiState.entries;
  const pts = src
    .map((e) => ({ t: parseTimestamp(e.timestamp), v: e[m] }))
    .filter((p) => p.t !== null && p.v !== null && p.v !== undefined);

  const rect = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(rect.width * ratio));
  canvas.height = Math.max(1, Math.floor(rect.height * ratio));
  const ctx = canvas.getContext("2d");
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  const W = rect.width, H = rect.height;
  const L = 46, R = 16, T = 14, B = 28;
  ctx.clearRect(0, 0, W, H);
  ctx.font = "11px Consolas, 'Roboto Mono', monospace";

  if (!pts.length) {
    ctx.fillStyle = "#5b6670";
    ctx.font = "13px 'Segoe UI', Roboto, sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("Belum ada data untuk ditampilkan", W / 2, H / 2);
    hmiState.pts = [];
    return;
  }

  const vals = pts.map((p) => p.v);
  let lo = Math.min(...vals), hi = Math.max(...vals);
  if (lim.min !== undefined) lo = Math.min(lo, lim.min);
  if (lim.max !== undefined) hi = Math.max(hi, lim.max);
  const pad = Math.max((hi - lo) * 0.12, m === "ph" ? 0.2 : 2);
  lo = Math.max(0, lo - pad); hi = hi + pad;
  const t0 = pts[0].t, t1 = pts[pts.length - 1].t || t0 + 1;
  const X = (t) => L + ((t - t0) / Math.max(t1 - t0, 1)) * (W - L - R);
  const Y = (v) => T + ((hi - v) / (hi - lo)) * (H - T - B);

  // grid + label sumbu
  ctx.strokeStyle = "#eef0f2"; ctx.lineWidth = 1; ctx.fillStyle = "#5b6670"; ctx.textAlign = "right";
  for (let i = 0; i <= 4; i++) {
    const v = lo + ((hi - lo) * i) / 4, y = Y(v);
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(W - R, y); ctx.stroke();
    ctx.fillText(v.toFixed(m === "ph" || m === "rain" ? 1 : 0), L - 6, y + 4);
  }
  ctx.textAlign = "center";
  for (let i = 0; i <= 4; i++) {
    const t = t0 + ((t1 - t0) * i) / 4, x = X(t);
    ctx.beginPath(); ctx.moveTo(x, T); ctx.lineTo(x, H - B); ctx.stroke();
    ctx.fillText(formatTime(t).slice(0, 5), x, H - 8);
  }

  // rentang aman + garis batas
  const yMax = lim.max !== undefined ? Y(lim.max) : T;
  const yMin = lim.min !== undefined ? Y(lim.min) : Y(lo);
  ctx.fillStyle = "rgba(30,125,50,0.06)";
  ctx.fillRect(L, yMax, W - L - R, yMin - yMax);
  ctx.setLineDash([6, 4]); ctx.strokeStyle = "#c62828"; ctx.lineWidth = 1.5;
  ctx.fillStyle = "#c62828"; ctx.textAlign = "right"; ctx.font = "10px 'Segoe UI', Roboto, sans-serif";
  [["max", "Batas atas"], ["min", "Batas bawah"]].forEach(([k, name]) => {
    if (lim[k] === undefined) return;
    const y = Y(lim[k]);
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(W - R, y); ctx.stroke();
    ctx.fillText(`${name} ${lim[k]}`, W - R - 4, k === "max" ? y - 5 : y + 13);
  });
  ctx.setLineDash([]);

  // area + garis data
  ctx.beginPath();
  pts.forEach((p, i) => (i ? ctx.lineTo(X(p.t), Y(p.v)) : ctx.moveTo(X(p.t), Y(p.v))));
  ctx.lineTo(X(pts[pts.length - 1].t), H - B); ctx.lineTo(X(pts[0].t), H - B); ctx.closePath();
  ctx.fillStyle = "rgba(11,92,173,0.08)"; ctx.fill();
  ctx.beginPath();
  pts.forEach((p, i) => (i ? ctx.lineTo(X(p.t), Y(p.v)) : ctx.moveTo(X(p.t), Y(p.v))));
  ctx.strokeStyle = "#0b5cad"; ctx.lineWidth = 2.5; ctx.lineJoin = "round"; ctx.stroke();
  const last = pts[pts.length - 1];
  ctx.beginPath(); ctx.arc(X(last.t), Y(last.v), 5, 0, Math.PI * 2);
  ctx.fillStyle = "#0b5cad"; ctx.fill(); ctx.lineWidth = 2; ctx.strokeStyle = "#fff"; ctx.stroke();

  hmiState.pts = pts.map((p) => ({ x: X(p.t), y: Y(p.v), t: p.t, v: p.v }));
  hmiState.chartBox = { L, R, T, B, W, H };
}

function hmiInitDashboard() {
  const tabs = document.getElementById("dashChartTabs");
  tabs?.addEventListener("click", (e) => {
    const btn = e.target.closest("button[data-metric]");
    if (!btn) return;
    tabs.querySelectorAll("button").forEach((b) => b.classList.toggle("on", b === btn));
    hmiState.metric = btn.dataset.metric;
    document.getElementById("dashLegendName").textContent = HMI_LIMIT[hmiState.metric].label;
    hmiPaintChart();
  });

  const canvas = document.getElementById("dashChart");
  const tip = document.getElementById("dashChartTip");
  canvas?.addEventListener("mousemove", (e) => {
    if (!hmiState.pts.length) return;
    const r = canvas.getBoundingClientRect();
    const mx = e.clientX - r.left;
    let best = hmiState.pts[0];
    for (const p of hmiState.pts) if (Math.abs(p.x - mx) < Math.abs(best.x - mx)) best = p;
    const lim = HMI_LIMIT[hmiState.metric];
    tip.hidden = false;
    tip.innerHTML = `<b>${best.v.toFixed(lim.dec)}${lim.unit}</b><span>${formatTime(best.t) + " WITA"}</span>`;
    tip.style.left = Math.min(best.x + 12, r.width - 150) + "px";
    tip.style.top = Math.max(best.y - 44, 4) + "px";
    canvas.style.setProperty("--cx", best.x + "px");
    canvas.parentElement.style.setProperty("--cx", best.x + "px");
    canvas.parentElement.classList.add("hovering");
  });
  canvas?.addEventListener("mouseleave", () => {
    tip.hidden = true;
    canvas.parentElement.classList.remove("hovering");
  });

  // Link "Buka kontrol / kalibrasi" -> pindah halaman lewat menu sidebar
  document.querySelectorAll("[data-goto]").forEach((a) =>
    a.addEventListener("click", (e) => {
      e.preventDefault();
      document.querySelector(`.nav-link[data-target='${a.dataset.goto}']`)?.click();
    }),
  );
  // Grafik digambar ulang saat halaman dashboard dibuka lagi / jendela diubah
  document.querySelectorAll(".nav-link").forEach((l) =>
    l.addEventListener("click", () => requestAnimationFrame(hmiPaintChart)),
  );
  window.addEventListener("resize", hmiPaintChart);
  document.querySelectorAll(".nav-link").forEach((l) =>
    l.addEventListener("click", () => requestAnimationFrame(repaintPageCharts)),
  );
  window.addEventListener("resize", repaintPageCharts);
}
hmiInitDashboard();
refreshDashboard();
setInterval(updateClock, 1000);
setInterval(refreshDashboard, 5000);
window.addEventListener("resize", refreshDashboard);
