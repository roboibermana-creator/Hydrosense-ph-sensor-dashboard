#ifndef VALVE_CONTROLLER_H
#define VALVE_CONTROLLER_H

#include <Arduino.h>

// =====================================================================
//  Valve_Controller
//  Mengendalikan pompa sampling, valve 1 (drain), dan valve 2 (dosis
//  tawas, motorized). Antarmuka mengikuti pemakaian di main HYDROSENSE
//  v3.1 supaya nanti bisa dipasang kembali ke sistem penuh tanpa ubah.
// =====================================================================
class Valve_Controller {
public:
  void begin();

  // Pompa sampling
  void pumpOn();
  void pumpOff();

  // Valve 1: drain pipa sampling
  void openValve1();
  void closeValve1();

  // Valve 2: dosis tawas (buka/tutup ditahan, atau dosis berdasarkan %)
  void openValve2();
  void closeValve2();
  void doseValve2(int percent);            // blocking: buka -> tunggu -> tutup -> tunggu
  unsigned long doseDurationMs(int percent) const;

  bool getPumpStatus()   const { return _pumpOn; }
  bool getValve1Status() const { return _valve1Open; }
  bool getValve2Status() const { return _valve2Open; }

  void printStatus();          // tabel lengkap
  void printCompactStatus();   // satu baris

private:
  void relayWrite(uint8_t pin, bool on);

  bool _pumpOn     = false;
  bool _valve1Open = false;
  bool _valve2Open = false;
};

#endif
