// Tombol Masuk/Keluar di header + dialog login. Masuk/keluar memuat ulang halaman supaya
// modul yang sesuai peran dimuat dari awal.
import { el } from "./core.js";
import { login, logout } from "./auth.js";

const LABEL = { visitor: "HANYA LIHAT", tamu: "TAMU", operator: "OPERATOR" };

export function initAuthUi(access) {
  const chip = el("acctChip");
  const btnLogin = el("btnLogin");
  const btnLogout = el("btnLogout");
  const dialog = el("loginDialog");
  const form = el("loginForm");
  const err = el("loginError");
  const submit = el("loginSubmit");

  const loggedIn = access.role !== "visitor";
  chip.hidden = false;
  chip.dataset.role = access.role;
  chip.textContent = LABEL[access.role];
  btnLogin.hidden = loggedIn;
  btnLogout.hidden = !loggedIn;
  if (access.checkFailed) chip.textContent = "HANYA LIHAT · SERVER TIDAK MERESPONS";

  // Panel profil di sidebar mengikuti peran yang masuk.
  if (loggedIn) {
    el("profName").textContent = LABEL[access.role];
    el("profRole").textContent = "ONLINE";
  }

  const showError = (msg) => {
    err.textContent = msg;
    err.hidden = !msg;
  };
  if (access.keyRejected) {
    // Kata sandi tersimpan sudah tidak berlaku (diganti): beri tahu sekali saat membuka dialog.
    btnLogin.title = "Kata sandi sebelumnya tidak berlaku lagi; masuk ulang.";
  }

  btnLogin.addEventListener("click", () => {
    showError(access.keyRejected ? "Kata sandi sebelumnya tidak berlaku lagi. Masuk ulang." : "");
    form.reset();
    dialog.showModal();
    el("loginPass").focus();
  });
  el("loginCancel").addEventListener("click", () => dialog.close());
  btnLogout.addEventListener("click", () => {
    logout();
    location.reload();
  });

  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    showError("");
    submit.disabled = true;
    submit.textContent = "Memeriksa...";
    const res = await login(el("loginPass").value, form.elements.role.value);
    submit.disabled = false;
    submit.textContent = "Masuk";
    if (res.error) {
      showError(res.error);
      return;
    }
    location.reload();
  });
}
