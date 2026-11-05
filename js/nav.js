// Navigasi antar-halaman (SPA) di sidebar.

// Navigation Logic (SPA)
const navLinks = document.querySelectorAll(".nav-link");
const pages = document.querySelectorAll(".page-content");

navLinks.forEach((link) => {
  link.addEventListener("click", (e) => {
    e.preventDefault();
    navLinks.forEach((l) => l.classList.remove("active"));
    pages.forEach((p) => p.classList.remove("active"));

    link.classList.add("active");
    const target = link.getAttribute("data-target");
    document.getElementById(target).classList.add("active");
  });
});

// Tombol CALIBRATE PROBE di sidebar -> buka halaman KALIBRASI
document
  .querySelector(".calibrate-button[href='#calibration']")
  ?.addEventListener("click", (e) => {
    e.preventDefault();
    document.querySelector("[data-target='page-kalibrasi']").click();
  });
