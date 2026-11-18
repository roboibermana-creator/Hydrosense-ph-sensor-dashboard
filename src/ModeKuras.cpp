// Lane "Kuras Otomatis" (state_machine.workflow.json):
//   Kuras (valve sudah dibuka di Kirim Data) -> tunggu float switch LOW ->
//   tutup valve -> kembali ke Cek Stop
#include "Modes.h"
#include "Pompa.h"

void handleKurasOtomatis() {
  switch (state) {
  case STATE_AUTO_TUNGGU_KOSONG:
    // Selama valve membuang, pompa harus mati (float switch LOW membuka
    // kunci pompa, jadi tidak cukup mengandalkan float).
    if (pumpState)
      pompaOff();
    // Selesai kalau float switch LOW (chamber kosong) ATAU waktu valve dari
    // jadwal habis. Pindah state hanya kalau valve benar-benar menutup
    // (valveTutup false = masih debounce, coba lagi iterasi berikutnya).
    if (!bacaFloatSwitch() || (millis() - timerKurasMulai >= valveMaksMs)) {
      if (valveTutup()) {
        // Siklus selesai: jeda ke siklus berikutnya dihitung dari SINI.
        waktuSiklusMulai = millis();
        state = STATE_A_CEK_STOP;
      }
    }
    break;

  default:
    break;
  }
}
