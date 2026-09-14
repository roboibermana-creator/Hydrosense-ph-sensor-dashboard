#ifndef POMPA_H
#define POMPA_H

#include <Arduino.h>

// PIN
extern const uint8_t PUMP_RELAY_PIN;

// KONFIGURASI RELAY (Active HIGH)
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

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

