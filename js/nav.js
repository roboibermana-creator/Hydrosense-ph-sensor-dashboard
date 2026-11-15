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
  document.getElementById(link.getAttribute("data-target"))?.classList.add("active");
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
