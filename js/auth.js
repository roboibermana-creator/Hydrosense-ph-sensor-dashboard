// Login kata sandi per peran. Lihat supabase/migrations/0009_auth_roles.sql.
// Peran: 'visitor' (belum login) | 'tamu' | 'operator'. Kata sandi tidak ada di kode ini:
// dikirim ke database (header x-access-key), yang membandingkannya dengan hash tersimpan.
// Peran di sini hanya menentukan TAMPILAN; yang mengunci penulisan adalah trigger di database.
import { supabase, ACCESS_KEY_STORE } from "./core.js";

const NAME = { operator: "Operator", tamu: "Tamu" };

async function roleForStoredKey() {
  const { data, error } = await supabase.rpc("login_role");
  if (error) throw error;
  return data;
}

export async function getAccess() {
  if (!localStorage.getItem(ACCESS_KEY_STORE)) return { role: "visitor", username: null };
  try {
    const role = await roleForStoredKey();
    if (role === "operator" || role === "tamu") return { role, username: NAME[role] };
    // Kata sandi sudah diganti / tidak dikenal lagi: kembali ke belum login.
    localStorage.removeItem(ACCESS_KEY_STORE);
    return { role: "visitor", username: null, keyRejected: true };
  } catch (e) {
    console.error("Gagal memeriksa kata sandi akses:", e.message || e);
    return { role: "visitor", username: null, checkFailed: true };
  }
}

/**
 * Coba masuk sebagai `expectedRole` ('tamu' | 'operator'). Kata sandi harus cocok dengan
 * peran yang dipilih. Kembalikan { role } kalau berhasil atau { error } kalau gagal.
 */
export async function login(password, expectedRole) {
  localStorage.setItem(ACCESS_KEY_STORE, password);
  try {
    const role = await roleForStoredKey();
    if ((role === "operator" || role === "tamu") && (!expectedRole || role === expectedRole)) {
      return { role };
    }
    localStorage.removeItem(ACCESS_KEY_STORE);
    if (role === "operator" || role === "tamu") {
      return { error: `Kata sandi itu bukan untuk ${NAME[expectedRole]}. Pilih peran yang sesuai.` };
    }
    return { error: "Kata sandi salah." };
  } catch (e) {
    localStorage.removeItem(ACCESS_KEY_STORE);
    return { error: `Gagal memeriksa kata sandi: ${e.message || e}` };
  }
}

export function logout() {
  localStorage.removeItem(ACCESS_KEY_STORE);
}
