// Editor tata letak: geser kartu dan ubah ukurannya dari sisi/sudut mana pun.
// Tersimpan di browser (localStorage).
//
// Model: tiap halaman yang diedit menjadi grid 60 kolom x baris 8px. Setiap kartu punya
// posisi eksplisit {x, y, w, h} (satuan kolom / baris). Wadah pembungkus kartu
// (data-lc-wrap) dibuat display:contents supaya semua kartu menjadi anak langsung grid.
//
// Aturan saat menggeser atau mengubah ukuran:
//  - kartu yang tertimpa bergeser ke kanan/kiri kalau ada ruang, kalau tidak turun ke bawah;
//  - dua kartu berukuran sama yang saling menimpa bertukar tempat;
//  - kartu lain ikut naik mengisi ruang kosong (kartu yang sedang diedit tidak ikut naik).
//
// Id kartu diberikan dari urutan DOM asli: naikkan VERSION kalau kartu di index.html
// ditambah, dihapus, atau diurutkan ulang. Layout hanya berlaku di layar >= 1101px
// (CSS-nya di edit-layout.css); di bawah itu tata letak bawaan yang responsif dipakai.

const VERSION = 2;
const KEY = "hydrosense.layout";
const CARD_SEL = ".data-panel, .hmi-card";
const COLS = 60; // 60 habis dibagi 2,3,4,5,6: 5 kartu KPI pun bisa dibagi rata
const ROW = 8; // px
const GAP_X = 16; // px, harus sama dengan column-gap di edit-layout.css
const MARGIN_B = 16; // px, harus sama dengan margin-bottom kartu di edit-layout.css
const MIN_W = 8;
const MIN_H = 12;
const MIN_VIEWPORT = 1101;
const DIRS = ["n", "s", "e", "w", "ne", "nw", "se", "sw"];

let editing = false;
const saved = load(); // { version, pages: { [pageId]: { [id]: {x,y,w,h,s?} } } }
const layouts = {}; // layout aktif per halaman (bentuknya sama dengan saved.pages[pageId])
const elById = new Map(); // id -> elemen (kartu atau item tetap)
const overlays = new Map(); // id -> { handle, edges: {dir: el} }
let rafId = 0;

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const clone = (o) => JSON.parse(JSON.stringify(o));
const wide = () => window.innerWidth >= MIN_VIEWPORT;

function load() {
  try {
    const v = JSON.parse(localStorage.getItem(KEY));
    if (v && v.version === VERSION) return v;
  } catch (_) {
    /* abaikan data rusak */
  }
  return { version: VERSION, pages: {} };
}

function persist() {
  try {
    localStorage.setItem(KEY, JSON.stringify(saved));
  } catch (_) {
    /* penyimpanan penuh / diblokir: tata letak hanya berlaku sampai halaman ditutup */
  }
}

// ---------- struktur halaman (dari DOM asli) ----------
function hiddenInChain(el, page) {
  for (let n = el; n && n !== page; n = n.parentElement)
    if (getComputedStyle(n).display === "none") return true;
  return false;
}

function markStructure() {
  document.querySelectorAll("main.page-content").forEach((page) => {
    let ki = 0;
    let fi = 0;
    const cards = [...page.querySelectorAll(CARD_SEL)].filter(
      (c) => !c.parentElement.closest(CARD_SEL),
    );
    for (const card of cards) {
      card.dataset.lcard = `${page.id}:${ki++}`;
      if (hiddenInChain(card, page)) {
        card.dataset.lcHidden = "1"; // kartu di wadah yang disembunyikan CSS: jangan dimunculkan
        continue;
      }
      elById.set(card.dataset.lcard, card);
      for (let n = card.parentElement; n && n !== page; n = n.parentElement)
        n.dataset.lcWrap = "1";
    }
    // Anak langsung halaman yang tidak berisi kartu (mis. judul halaman) = item tetap.
    for (const child of page.children) {
      if (child.dataset.lcWrap || child.matches(CARD_SEL)) continue;
      if (child.querySelector(CARD_SEL)) continue;
      if (getComputedStyle(child).display === "none") continue;
      child.dataset.lcFixed = `${page.id}:f${fi++}`;
      elById.set(child.dataset.lcFixed, child);
    }
  });
}

const pageItems = (pid) => [...elById.keys()].filter((id) => id.startsWith(`${pid}:`));

// ---------- konversi tata letak bawaan -> posisi grid ----------
function gridMetrics(page) {
  const cs = getComputedStyle(page);
  const r = page.getBoundingClientRect();
  const padL = parseFloat(cs.paddingLeft) + parseFloat(cs.borderLeftWidth);
  const padR = parseFloat(cs.paddingRight) + parseFloat(cs.borderRightWidth);
  const padT = parseFloat(cs.paddingTop) + parseFloat(cs.borderTopWidth);
  const width = r.width - padL - padR;
  const colW = (width - (COLS - 1) * GAP_X) / COLS;
  return { left: r.left + padL, top: r.top + padT, step: colW + GAP_X };
}

// Halaman harus sedang tampil (ukuran kartu diukur dari tata letak bawaan).
function snapshot(page) {
  const m = gridMetrics(page);
  const L = {};
  for (const id of pageItems(page.id)) {
    const el = elById.get(id);
    const r = el.getBoundingClientRect();
    if (r.width === 0 || r.height === 0) continue;
    const fixed = el.dataset.lcFixed === id;
    const w = fixed ? COLS : clamp(Math.round((r.width + GAP_X) / m.step), MIN_W, COLS);
    const x = fixed ? 0 : clamp(Math.round((r.left - m.left) / m.step), 0, COLS - w);
    const y = Math.max(0, Math.round((r.top - m.top) / ROW));
    const h = Math.max(fixed ? 1 : MIN_H, Math.ceil((r.height + MARGIN_B) / ROW));
    L[id] = { x, y, w, h };
    if (fixed) L[id].s = 1;
  }
  settle(L, null);
  return L;
}

// ---------- mesin tata letak (fungsi murni) ----------
const overlaps = (a, b) =>
  a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;

const overlapArea = (a, b) =>
  Math.max(0, Math.min(a.x + a.w, b.x + b.w) - Math.max(a.x, b.x)) *
  Math.max(0, Math.min(a.y + a.h, b.y + b.h) - Math.max(a.y, b.y));

// Kartu tidak boleh menimpa item tetap (judul halaman).
function clearStatics(L) {
  for (const it of Object.values(L)) {
    if (it.s) continue;
    for (const s of Object.values(L)) {
      if (s.s && overlaps(it, s)) it.y = s.y + s.h;
    }
  }
}

// Dorong kartu yang tertimpa `movedId`: ke samping kalau sebaris dan ada ruang,
// kalau tidak turun ke bawah. Berantai ke kartu berikutnya.
function pushOthers(L, movedId) {
  const ids = Object.keys(L);
  const stack = [movedId];
  let guard = 0;
  while (stack.length && guard++ < 1000) {
    const m = L[stack.pop()];
    for (const id of ids) {
      const c = L[id];
      if (c === m || c.s || !overlaps(m, c)) continue;
      const sameRow =
        Math.min(m.y + m.h, c.y + c.h) - Math.max(m.y, c.y) > 0.5 * Math.min(m.h, c.h);
      const mOnLeft = m.x + m.w / 2 <= c.x + c.w / 2;
      if (sameRow && mOnLeft && m.x + m.w + c.w <= COLS) c.x = m.x + m.w;
      else if (sameRow && !mOnLeft && m.x - c.w >= 0) c.x = m.x - c.w;
      else c.y = m.y + m.h;
      stack.push(id);
    }
  }
}

// Naikkan kartu ke atas selama ruangnya kosong. Kartu `keepId` (yang sedang diedit)
// diperlakukan sebagai penghalang tetap supaya tidak ikut melompat.
function compact(L, keepId) {
  const placed = Object.entries(L)
    .filter(([id, it]) => it.s || id === keepId)
    .map(([, it]) => it);
  const rest = Object.entries(L)
    .filter(([id, it]) => !it.s && id !== keepId)
    .sort((a, b) => a[1].y - b[1].y || a[1].x - b[1].x);
  for (const [, it] of rest) {
    while (it.y > 0) {
      it.y--;
      if (placed.some((p) => overlaps(it, p))) {
        it.y++;
        break;
      }
    }
    placed.push(it);
  }
}

function settle(L, activeId) {
  clearStatics(L);
  if (activeId) pushOthers(L, activeId);
  else {
    // Tata letak awal hasil pengukuran: selesaikan tumpang tindih kartu demi kartu.
    const order = Object.keys(L).sort((a, b) => L[a].y - L[b].y || L[a].x - L[b].x);
    for (const id of order) if (!L[id].s) pushOthers(L, id);
  }
  clearStatics(L);
  compact(L, activeId);
}

function layoutForMove(base, id, nx, ny) {
  const L = clone(base);
  const me = L[id];
  const start = base[id];
  nx = clamp(nx, 0, COLS - me.w);
  ny = Math.max(0, ny);
  const cand = { x: nx, y: ny, w: me.w, h: me.h };
  // Ukuran sama dan tertimpa >= 50%: tukar tempat.
  for (const [tid, t] of Object.entries(base)) {
    if (tid === id || t.s || t.w !== start.w || Math.abs(t.h - start.h) > 2) continue;
    if (overlapArea(cand, t) >= 0.5 * t.w * t.h) {
      L[id].x = t.x;
      L[id].y = t.y;
      L[tid].x = start.x;
      L[tid].y = start.y;
      settle(L, id);
      return L;
    }
  }
  me.x = nx;
  me.y = ny;
  settle(L, id);
  return L;
}

function layoutForResize(base, id, dir, dxCols, dyRows) {
  const L = clone(base);
  const s = base[id];
  const me = L[id];
  if (dir.includes("e")) me.w = clamp(s.w + dxCols, MIN_W, COLS - s.x);
  if (dir.includes("w")) {
    const nx = clamp(s.x + dxCols, 0, s.x + s.w - MIN_W);
    me.x = nx;
    me.w = s.w + (s.x - nx);
  }
  if (dir.includes("s")) me.h = Math.max(MIN_H, s.h + dyRows);
  if (dir.includes("n")) {
    const ny = clamp(s.y + dyRows, 0, s.y + s.h - MIN_H);
    me.y = ny;
    me.h = s.h + (s.y - ny);
  }
  settle(L, id);
  return L;
}

// ---------- menerapkan ke DOM ----------
function applyLayout(pid) {
  const page = document.getElementById(pid);
  const L = layouts[pid];
  if (!page || !L) return;
  page.dataset.lcFree = "1";
  for (const [id, it] of Object.entries(L)) {
    const el = elById.get(id);
    if (!el) continue;
    el.style.setProperty("--lc-x", it.x + 1);
    el.style.setProperty("--lc-y", it.y + 1);
    el.style.setProperty("--lc-w", it.w);
    el.style.setProperty("--lc-h", it.h);
  }
}

function releasePage(pid) {
  const page = document.getElementById(pid);
  if (!page) return;
  delete page.dataset.lcFree;
  for (const id of pageItems(pid)) {
    const el = elById.get(id);
    ["--lc-x", "--lc-y", "--lc-w", "--lc-h"].forEach((p) => el.style.removeProperty(p));
  }
  delete layouts[pid];
}

const activePage = () => document.querySelector("main.page-content.active");

// Saat mode edit menyala, halaman yang tampil dikonversi ke grid (tampak hampir sama).
function ensureFree() {
  const page = activePage();
  if (!editing || !page || !wide() || layouts[page.id]) return;
  layouts[page.id] = snapshot(page);
  applyLayout(page.id);
  requestAnimationFrame(repaint);
}

function commit(pid) {
  saved.pages[pid] = clone(layouts[pid]);
  persist();
}

// Grafik dan kartu lain menggambar ulang sesuai ukuran baru.
const repaint = () => window.dispatchEvent(new Event("resize"));

// ---------- geser & ubah ukuran ----------
function currentStep(page) {
  const cs = getComputedStyle(page);
  const trackW = parseFloat(cs.gridTemplateColumns.split(" ")[0]);
  return (Number.isFinite(trackW) ? trackW : 40) + (parseFloat(cs.columnGap) || GAP_X);
}

function track(card, e, onMove) {
  const page = card.closest("main.page-content");
  const pid = page.id;
  const id = card.dataset.lcard;
  const base = clone(layouts[pid]);
  const step = currentStep(page);
  const sx = e.clientX;
  const sy = e.clientY;
  const scrollY0 = window.scrollY;
  let lastKey = "";
  let px = sx;
  let py = sy;
  let scrollRaf = 0;
  document.body.classList.add("lc-dragging-now");
  card.classList.add("lc-active");

  const update = () => {
    const dxCols = Math.round((px - sx) / step);
    const dyRows = Math.round((py - sy + (window.scrollY - scrollY0)) / ROW);
    const key = `${dxCols},${dyRows}`;
    if (key === lastKey) return;
    lastKey = key;
    layouts[pid] = onMove(base, id, dxCols, dyRows);
    applyLayout(pid);
  };
  // Gulir otomatis saat penunjuk dekat tepi atas/bawah layar.
  const autoScroll = () => {
    const edge = 70;
    const d = py < edge ? -14 : py > window.innerHeight - edge ? 14 : 0;
    if (d) {
      window.scrollBy(0, d);
      update();
    }
    scrollRaf = requestAnimationFrame(autoScroll);
  };
  const move = (ev) => {
    px = ev.clientX;
    py = ev.clientY;
    update();
  };
  const up = () => {
    cancelAnimationFrame(scrollRaf);
    document.body.classList.remove("lc-dragging-now");
    card.classList.remove("lc-active");
    document.removeEventListener("pointermove", move);
    document.removeEventListener("pointerup", up);
    document.removeEventListener("pointercancel", up);
    commit(pid);
    repaint();
  };
  document.addEventListener("pointermove", move);
  document.addEventListener("pointerup", up);
  document.addEventListener("pointercancel", up);
  scrollRaf = requestAnimationFrame(autoScroll);
}

function startDrag(card, e) {
  e.preventDefault();
  const start = layouts[card.closest("main.page-content").id][card.dataset.lcard];
  track(card, e, (base, id, dxCols, dyRows) =>
    layoutForMove(base, id, start.x + dxCols, start.y + dyRows),
  );
}

function startResize(card, dir, e) {
  e.preventDefault();
  e.stopPropagation();
  track(card, e, (base, id, dxCols, dyRows) =>
    layoutForResize(base, id, dir, dxCols, dyRows),
  );
}

// ---------- antarmuka mode edit ----------
function cardTitle(card) {
  // Teks pertama pada judul kartu saja (judul bisa memuat tab/tautan sesudahnya).
  const h = card.querySelector(".panel-heading, .panel-h, .kpi-title, h2, h3, h4");
  if (!h) return "Kartu";
  const walker = document.createTreeWalker(h, NodeFilter.SHOW_TEXT);
  for (let n = walker.nextNode(); n; n = walker.nextNode()) {
    const s = n.textContent.trim().replace(/\s+/g, " ");
    if (s) return s.slice(0, 40);
  }
  return "Kartu";
}

// Gagang & pegangan ukuran TIDAK dimasukkan ke dalam kartu: menambah anak ke kartu
// mengubah :last-child / :first-child dan menggeser tata letak walau mode edit mati.
// Semuanya ada di lapisan terpisah (#lcLayer) yang posisinya mengikuti kotak kartu.
function addOverlays() {
  const layer = document.createElement("div");
  layer.id = "lcLayer";
  for (const [id, el] of elById) {
    if (el.dataset.lcFixed === id) continue;
    const handle = document.createElement("div");
    handle.className = "lc-handle";
    handle.dataset.for = id;
    handle.textContent = `⠿ ${cardTitle(el)}`;
    handle.title = "Tahan lalu geser untuk memindahkan kartu";
    handle.addEventListener("pointerdown", (e) => startDrag(el, e));
    layer.append(handle);

    const edges = {};
    for (const dir of DIRS) {
      const z = document.createElement("div");
      z.className = `lc-edge lc-${dir}`;
      z.dataset.for = id;
      z.dataset.dir = dir;
      z.addEventListener("pointerdown", (e) => startResize(el, dir, e));
      layer.append(z);
      edges[dir] = z;
    }
    overlays.set(id, { handle, edges });
  }
  document.body.append(layer);
}

const EDGE = 8; // tebal zona tarik (px), terpusat di garis tepi kartu
const CORNER = 16;

function place(node, l, t, w, h) {
  node.style.left = `${l}px`;
  node.style.top = `${t}px`;
  if (w !== undefined) node.style.width = `${Math.max(0, w)}px`;
  if (h !== undefined) node.style.height = `${Math.max(0, h)}px`;
}

function syncOverlay() {
  const half = EDGE / 2;
  for (const [id, { handle, edges }] of overlays) {
    const r = elById.get(id).getBoundingClientRect();
    const show = r.width > 0 && r.height > 0 && wide();
    handle.style.display = show ? "block" : "none";
    for (const z of Object.values(edges)) z.style.display = show ? "block" : "none";
    if (!show) continue;
    place(handle, r.left, r.top, r.width);
    place(edges.n, r.left + CORNER, r.top - half, r.width - 2 * CORNER, EDGE);
    place(edges.s, r.left + CORNER, r.bottom - half, r.width - 2 * CORNER, EDGE);
    place(edges.w, r.left - half, r.top + CORNER, EDGE, r.height - 2 * CORNER);
    place(edges.e, r.right - half, r.top + CORNER, EDGE, r.height - 2 * CORNER);
    place(edges.nw, r.left - half, r.top - half, CORNER, CORNER);
    place(edges.ne, r.right - CORNER + half, r.top - half, CORNER, CORNER);
    place(edges.sw, r.left - half, r.bottom - CORNER + half, CORNER, CORNER);
    place(edges.se, r.right - CORNER + half, r.bottom - CORNER + half, CORNER, CORNER);
  }
  rafId = editing ? requestAnimationFrame(syncOverlay) : 0;
}

function setEditing(on) {
  if (on && !wide()) return;
  editing = on;
  document.body.classList.toggle("lc-editing", on);
  document.getElementById("lcToolbar").hidden = !on;
  document.getElementById("btnEditLayout")?.setAttribute("aria-pressed", String(on));
  if (on) {
    ensureFree();
    if (!rafId) rafId = requestAnimationFrame(syncOverlay);
  } else {
    // Halaman yang tidak pernah diubah kembali ke tata letak bawaan yang responsif.
    for (const pid of Object.keys(layouts)) if (!saved.pages[pid]) releasePage(pid);
    repaint();
  }
}

function resetPage() {
  const page = activePage();
  if (!page) return;
  delete saved.pages[page.id];
  persist();
  location.reload();
}

function resetAll() {
  localStorage.removeItem(KEY);
  location.reload();
}

function buildToolbar() {
  const bar = document.createElement("div");
  bar.id = "lcToolbar";
  bar.hidden = true;
  bar.innerHTML = `
    <span class="lc-hint"><b>MODE EDIT</b> — geser kartu lewat gagang <b>⠿</b>; tarik tepi atau sudut
      kartu untuk mengubah ukuran. Kartu yang tertimpa bergeser, kartu berukuran sama bertukar
      tempat. Tombol kontrol alat dimatikan selama mode edit.</span>
    <button type="button" data-act="reset-page">Reset halaman ini</button>
    <button type="button" data-act="reset-all">Reset semua</button>
    <button type="button" data-act="done" class="lc-done">Selesai</button>`;
  bar.addEventListener("click", (e) => {
    const act = e.target.closest("button")?.dataset.act;
    if (act === "done") setEditing(false);
    else if (act === "reset-page" && confirm("Kembalikan tata letak halaman ini ke bawaan?")) resetPage();
    else if (act === "reset-all" && confirm("Kembalikan tata letak SEMUA halaman ke bawaan?")) resetAll();
  });
  document.body.append(bar);
}

export function initLayoutEditor() {
  markStructure();
  // Terapkan tata letak tersimpan (tanpa pengukuran: posisi sudah dalam satuan grid).
  for (const [pid, L] of Object.entries(saved.pages)) {
    if (document.getElementById(pid)) {
      layouts[pid] = L;
      applyLayout(pid);
    }
  }
  addOverlays();
  buildToolbar();

  document.getElementById("btnEditLayout")?.addEventListener("click", () => setEditing(!editing));
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && editing) setEditing(false);
  });
  // Pindah halaman saat mode edit menyala: halaman baru ikut dikonversi ke grid.
  document
    .querySelectorAll(".nav-link")
    .forEach((l) => l.addEventListener("click", () => setTimeout(ensureFree, 0)));
  // Selama mode edit, klik di dalam kartu (mis. kartu KPI yang bisa diklik) tidak boleh
  // menjalankan aksinya; hanya gagang dan pegangan ukuran yang aktif.
  document.addEventListener(
    "click",
    (e) => {
      if (editing && e.target.closest("[data-lcard]")) {
        e.preventDefault();
        e.stopPropagation();
      }
    },
    true,
  );
  window.addEventListener("resize", () => {
    if (editing && !wide()) setEditing(false);
  });
}
