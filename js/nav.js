// Navigasi antar-halaman (SPA) + animasi "Morph" ala PowerPoint.
// Semua jalur pindah menu (klik sidebar, kartu KPI/data-goto, tombol back) lewat navigateTo().
// Teknik: View Transitions API. Elemen yang punya view-transition-name sama di halaman
// lama & baru dianimasikan otomatis oleh browser (posisi, ukuran, isi).
// Matikan efek: set MORPH = false.

const MORPH = true;
const navLinks = [...document.querySelectorAll(".nav-link")];
const pages = document.querySelectorAll(".page-content");
const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

// Kartu KPI di Dashboard Utama yang "berubah wujud" jadi kartu utama halaman detail.
const KPI_OF = {
  "page-ph-sensor": "kpiPh",
  "page-tss-sensor": "kpiTss",
  "page-water-level": "kpiWl",
  "page-rain-gauge": "kpiRain",
};
const HOME = "page-dashboard-utama";

let running = null; // transisi yang sedang jalan (untuk klik beruntun)

const currentId = () => document.querySelector(".page-content.active")?.id;
const linkOf = (id) => navLinks.find((l) => l.dataset.target === id);

// Elemen "hero" di halaman `id` yang dipasangkan dengan halaman `other`.
function heroOf(id, other) {
  if (id === HOME) return document.getElementById(KPI_OF[other]);
  if (!KPI_OF[id]) return null;
  if (other !== HOME && !KPI_OF[other]) return null; // kalibrasi/kontrol: tanpa pasangan
  return document.querySelector(`#${id} > .data-panel.ph-panel`);
}

function swap(id) {
  navLinks.forEach((l) => l.classList.toggle("active", l.dataset.target === id));
  pages.forEach((p) => p.classList.toggle("active", p.id === id));
}

// Fokus ke judul halaman baru agar pembaca layar mengumumkan konteksnya.
function focusHeading(id) {
  const h = document.querySelector(`#${id} h1, #${id} .panel-heading span`);
  if (!h) return;
  h.setAttribute("tabindex", "-1");
  h.focus({ preventScroll: true });
}

function navigateTo(id, { push = true } = {}) {
  // Panggilan ganda ke tujuan yang sama selama transisi berjalan diabaikan
  // (mis. pendengar data-goto terpasang dua kali).
  if (running && running.target === id) return;
  const from = currentId();
  if (!id || id === from || !document.getElementById(id)) return;
  if (push) history.pushState({ page: id }, "", "#" + id.replace(/^page-/, ""));

  const animate = MORPH && !reduceMotion.matches;
  const fromIdx = navLinks.indexOf(linkOf(from));
  const toIdx = navLinks.indexOf(linkOf(id));
  const dir = toIdx < fromIdx ? "back" : "fwd";

  if (!animate) {
    swap(id);
    focusHeading(id);
    return;
  }

  // Tanpa View Transitions (Firefox/Safari lama): slide+fade sederhana pakai Web Animations.
  if (!document.startViewTransition) {
    swap(id);
    const dx = dir === "fwd" ? 40 : -40;
    document.getElementById(id).animate(
      [
        { opacity: 0, transform: `translateX(${dx}px)` },
        { opacity: 1, transform: "none" },
      ],
      { duration: 380, easing: "cubic-bezier(0.22, 1, 0.36, 1)" },
    );
    focusHeading(id);
    return;
  }

  running?.skipTransition(); // klik beruntun: selesaikan transisi lama dulu

  // Pasang nama morph pada pasangan hero (hanya satu yang tampil di tiap sisi -> tetap unik).
  const heroes = [heroOf(from, id), heroOf(id, from)].filter(Boolean);
  if (heroes.length === 2) heroes.forEach((el) => (el.style.viewTransitionName = "hero"));
  document.documentElement.dataset.navDir = dir;

  const t = document.startViewTransition(() => swap(id));
  running = t;
  t.target = id;
  t.ready.catch(() => {}); // dilewati karena klik beruntun: wajar, bukan error
  t.finished.finally(() => {
    heroes.forEach((el) => (el.style.viewTransitionName = ""));
    if (running === t) running = null;
    focusHeading(id);
  });
}

// Klik menu sidebar
navLinks.forEach((link) =>
  link.addEventListener("click", (e) => {
    e.preventDefault();
    navigateTo(link.dataset.target);
  }),
);

// Tombol back/forward browser
history.replaceState({ page: currentId() }, "", location.href);
window.addEventListener("popstate", (e) => {
  const id = e.state?.page;
  // Hanya ke menu yang terlihat (menghormati pembatasan peran).
  if (id && linkOf(id) && linkOf(id).offsetParent !== null) navigateTo(id, { push: false });
});

// Tombol CALIBRATE PROBE di sidebar -> buka halaman KALIBRASI
document
  .querySelector(".calibrate-button[href='#calibration']")
  ?.addEventListener("click", (e) => {
    e.preventDefault();
    navigateTo("page-kalibrasi");
  });

window.navigateTo = navigateTo;
