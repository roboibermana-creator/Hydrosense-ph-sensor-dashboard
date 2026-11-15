// Navigasi antar-halaman (SPA) di sidebar.
// Transisi pakai View Transitions API (morph): halaman lama memudar+blur,
// halaman baru naik halus, dan sorotan menu aktif "meluncur" ke item baru.
// Browser tanpa dukungan -> ganti halaman langsung (tanpa animasi).

const navLinks = document.querySelectorAll(".nav-link");
const pages = document.querySelectorAll(".page-content");
const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

function showPage(link) {
  navLinks.forEach((l) => l.classList.remove("active"));
  pages.forEach((p) => p.classList.remove("active"));
  link.classList.add("active");
  const page = document.getElementById(link.getAttribute("data-target"));
  if (!page) return;
  page.classList.add("active");
  playEnter(page);
}

// Kartu halaman baru muncul dari bawah berurutan (urutan baca: atas->bawah, kiri->kanan).
function playEnter(page) {
  if (reduceMotion.matches) return;
  const cards = [...page.querySelectorAll(".data-panel, .hmi-card")].filter(
    (c) => !c.parentElement.closest(".data-panel, .hmi-card"),
  );
  const pos = cards.map((c) => ({ c, r: c.getBoundingClientRect() }));
  pos.sort((a, b) => Math.round(a.r.top / 40) - Math.round(b.r.top / 40) || a.r.left - b.r.left);
  pos.forEach(({ c }, i) => c.style.setProperty("--i", Math.min(i, 10)));
  page.classList.remove("page-enter");
  void page.offsetWidth; // restart animasi
  page.classList.add("page-enter");
  clearTimeout(page._enterT);
  page._enterT = setTimeout(() => page.classList.remove("page-enter"), 1700);
}

navLinks.forEach((link) => {
  link.addEventListener("click", (e) => {
    e.preventDefault();
    if (link.classList.contains("active")) return;
    if (!document.startViewTransition || reduceMotion.matches) {
      showPage(link);
      return;
    }
    document.startViewTransition(() => showPage(link));
  });
});

// Tombol CALIBRATE PROBE di sidebar -> buka halaman KALIBRASI
document
  .querySelector(".calibrate-button[href='#calibration']")
  ?.addEventListener("click", (e) => {
    e.preventDefault();
    document.querySelector("[data-target='page-kalibrasi']").click();
  });
