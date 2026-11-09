// Titik masuk dashboard.
//
// Peran (lihat js/auth.js dan supabase/migrations/0009_auth_roles.sql):
//   visitor  = belum login   : Dashboard Utama saja, hanya baca
//   tamu     = akun tamu     : semua menu detail, tanpa kontrol
//   operator = akun operator : semua menu dan kontrol
// Modul kontrol, kalibrasi, jadwal, dan editor tata letak hanya DIMUAT untuk operator.
// Catatan: pembatasan tampilan ini bukan pengaman utama; yang mengunci penulisan ke
// alat adalah trigger/RLS di database, yang memeriksa peran akun yang sedang masuk.
import { getAccess } from "./auth.js";
import { initAuthUi } from "./auth-ui.js";

const access = await getAccess();
const role = access.role;
document.body.dataset.role = role;
initAuthUi(access);

if (role === "visitor") {
  // Tidak ada halaman lain untuk dibuka: lepas tautan dari kartu KPI sebelum modul lain
  // memasang pendengarnya.
  document.querySelectorAll("[data-goto]").forEach((e) => {
    e.removeAttribute("data-goto");
    e.classList.remove("is-link");
    e.removeAttribute("role");
    e.removeAttribute("tabindex");
  });
}

await import("./nav.js");
await import("./dashboard.js?v=20261109d");
await import("./rain.js?v=20261106g");

// Status kalibrasi pH di Dashboard Utama (baca saja, untuk semua peran).
const { initCalibStatus } = await import("./calib-status.js?v=20261106g");
initCalibStatus();

if (role !== "operator") {
  // Status valve/pompa tetap tampil di Dashboard Utama, tapi hanya dibaca.
  const { initGuestStatus } = await import("./guest-status.js");
  initGuestStatus();
}

if (role === "tamu" || role === "operator") {
  const { initRainPage } = await import("./rain-page.js");
  initRainPage();
  const { initLokasi } = await import("./lokasi.js?v=20261109a");
  initLokasi();
  const { initEkspor } = await import("./ekspor.js?v=20261109a");
  initEkspor();
}

if (role === "operator") {
  const [
    { initControl },
    { initCalibrationTss },
    { initTssFactor },
    { initTssHwWizard },
    { initPhCalibration },
    { initMode },
    { initCountdown },
    { initSchedule },
    { initLayoutEditor },
  ] = await Promise.all([
    import("./control.js"),
    import("./calibration-tss.js"),
    import("./tss-factor.js?v=20261108c"),
    import("./tss-hw-wizard.js?v=20261108c"),
    import("./ph-calibration.js?v=20261106g"),
    import("./mode.js"),
    import("./countdown.js"),
    import("./schedule.js"),
    import("./layout-editor.js"),
  ]);

  initControl();
  initCalibrationTss();
  initTssFactor();
  initTssHwWizard();
  initPhCalibration();
  initMode();
  initCountdown();
  initSchedule();
  initLayoutEditor();
}
