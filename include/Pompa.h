#ifndef POMPA_H
#define POMPA_H

#include <Arduino.h>

// PIN
extern const uint8_t PUMP_RELAY_PIN;

// KONFIGURASI RELAY (Active LOW) -- modul relay pompa menyala saat pin LOW
// (terbukti: pompa nyala sendiri saat boot ketika OFF = LOW).
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// PARAMETER MODE OTOMATIS
extern const unsigned long PUMP_ON_TIME;
extern const unsigned long PUMP_OFF_TIME;

// VARIABLE GLOBAL
extern bool pumpAutoMode;
extern bool pumpState;
extern unsigned long pumpPreviousMillis;

// FUNGSI
void pumpBegin();
void pumpON();
void pumpOFF();
void pumpShowStatus();
void pumpShowMenu();
void pumpUpdateAuto();

#endif
