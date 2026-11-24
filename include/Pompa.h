#ifndef POMPA_H
#define POMPA_H

#include <Arduino.h>

// PIN
extern const uint8_t PUMP_RELAY_PIN;

// KONFIGURASI RELAY (Active LOW) -- modul relay pompa menyala saat pin LOW
// (terbukti: pompa nyala sendiri saat boot ketika OFF = LOW).
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// VARIABLE GLOBAL
extern bool pumpState;

// FUNGSI
void pumpBegin();
void pumpON();
void pumpOFF();

#endif
