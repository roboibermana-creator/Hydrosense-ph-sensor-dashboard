// Grafik garis sederhana + tooltip nilai saat kursor / jari di atas grafik.
import { formatTime } from "./core.js";

const hoverState = new WeakMap(); // canvas -> { pts, decimals, unit }

/**
 * Pasang tooltip dan garis bantu pada canvas. `pts` = [{ x, y, v, t }] dengan x,y dalam
 * piksel CSS relatif ke canvas, v = nilai, t = waktu (ms). Aman dipanggil berulang
 * (tiap menggambar ulang): pendengar hanya dipasang sekali per canvas.
 */
export function registerHover(canvas, pts, { decimals = 1, unit = "" } = {}) {
  let s = hoverState.get(canvas);
  if (!s) {
    s = {};
    hoverState.set(canvas, s);
    attachHover(canvas, s);
  }
  s.pts = pts;
  s.decimals = decimals;
  s.unit = unit;
  s.hide?.();
}

function makeEl(cls) {
  const e = document.createElement("div");
  e.className = cls;
  e.hidden = true;
  return e;
}

function attachHover(canvas, s) {
  const wrap = canvas.parentElement;
  const tip = makeEl("chart-tip");
  const cross = makeEl("chart-cross");
  const dot = makeEl("chart-dot");
  wrap.append(cross, dot, tip);

  s.hide = () => {
    tip.hidden = cross.hidden = dot.hidden = true;
  };
  const show = (e) => {
    if (!s.pts?.length) return s.hide();
    const r = canvas.getBoundingClientRect();
    const mx = e.clientX - r.left;
    let best = s.pts[0];
    for (const p of s.pts) if (Math.abs(p.x - mx) < Math.abs(best.x - mx)) best = p;
    tip.innerHTML = `<b>${best.v.toFixed(s.decimals)}${s.unit}</b><span>${formatTime(best.t)} WITA</span>`;
    tip.hidden = cross.hidden = dot.hidden = false;
    const tw = tip.offsetWidth || 120;
    tip.style.left = `${Math.max(4, Math.min(best.x + 12, r.width - tw - 4))}px`;
    tip.style.top = `${Math.max(best.y - 48, 4)}px`;
    cross.style.left = `${best.x}px`;
    dot.style.left = `${best.x}px`;
    dot.style.top = `${best.y}px`;
  };
  canvas.addEventListener("pointermove", show);
  canvas.addEventListener("pointerdown", show);
  canvas.addEventListener("pointerleave", s.hide);
}

/**
 * Gambar grafik garis dengan sumbu bernilai pada canvas, lalu kembalikan titiknya.
 * rows = [{ t (ms), v }] urut waktu naik.
 */
export function drawSeries(canvas, rows, { decimals = 1, unit = "", minZero = true } = {}) {
  const rect = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(rect.width * ratio));
  canvas.height = Math.max(1, Math.floor(rect.height * ratio));
  const ctx = canvas.getContext("2d");
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  const W = rect.width;
  const H = rect.height;
  ctx.clearRect(0, 0, W, H);

  if (!rows.length) {
    ctx.fillStyle = "#5b6670";
    ctx.font = "13px 'Segoe UI', Arial, sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("Belum ada data untuk ditampilkan", W / 2, H / 2);
    registerHover(canvas, [], { decimals, unit });
    return [];
  }

  const L = 46;
  const R = 16;
  const T = 14;
  const B = 28;
  const vals = rows.map((r) => r.v);
  let lo = Math.min(...vals);
  let hi = Math.max(...vals);
  const pad = Math.max((hi - lo) * 0.15, 0.5);
  lo = minZero ? 0 : lo - pad;
  hi = hi + pad;
  const t0 = rows[0].t;
  const t1 = rows[rows.length - 1].t;
  const X = (t) => L + ((t - t0) / Math.max(t1 - t0, 1)) * (W - L - R);
  const Y = (v) => T + ((hi - v) / (hi - lo)) * (H - T - B);

  ctx.font = "11px Consolas, 'Roboto Mono', monospace";
  ctx.strokeStyle = "#eef0f2";
  ctx.lineWidth = 1;
  ctx.fillStyle = "#5b6670";
  ctx.textAlign = "right";
  for (let i = 0; i <= 4; i++) {
    const v = lo + ((hi - lo) * i) / 4;
    const y = Y(v);
    ctx.beginPath();
    ctx.moveTo(L, y);
    ctx.lineTo(W - R, y);
    ctx.stroke();
    ctx.fillText(v.toFixed(decimals), L - 6, y + 4);
  }
  ctx.textAlign = "center";
  for (let i = 0; i <= 4; i++) {
    const t = t0 + ((t1 - t0) * i) / 4;
    const x = X(t);
    ctx.beginPath();
    ctx.moveTo(x, T);
    ctx.lineTo(x, H - B);
    ctx.stroke();
    ctx.fillText(formatTime(t).slice(0, 5), x, H - 8);
  }

  const path = () => {
    ctx.beginPath();
    rows.forEach((r, i) => (i ? ctx.lineTo(X(r.t), Y(r.v)) : ctx.moveTo(X(r.t), Y(r.v))));
  };
  path();
  ctx.lineTo(X(t1), H - B);
  ctx.lineTo(X(t0), H - B);
  ctx.closePath();
  ctx.fillStyle = "rgba(11,92,173,0.08)";
  ctx.fill();
  path();
  ctx.strokeStyle = "#0b5cad";
  ctx.lineWidth = 2.5;
  ctx.lineJoin = "round";
  ctx.stroke();
  const last = rows[rows.length - 1];
  ctx.beginPath();
  ctx.arc(X(last.t), Y(last.v), 5, 0, Math.PI * 2);
  ctx.fillStyle = "#0b5cad";
  ctx.fill();
  ctx.lineWidth = 2;
  ctx.strokeStyle = "#fff";
  ctx.stroke();

  const pts = rows.map((r) => ({ x: X(r.t), y: Y(r.v), v: r.v, t: r.t }));
  registerHover(canvas, pts, { decimals, unit });
  return pts;
}
