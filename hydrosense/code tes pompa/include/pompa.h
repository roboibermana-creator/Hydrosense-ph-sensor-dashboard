#ifndef POMPA_H
#define POMPA_H

#include <Arduino.h>

// PIN
extern const uint8_t PUMP_RELAY_PIN;

// KONFIGURASI RELAY (Active LOW)
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// PARAMETER MODE OTOMATIS
extern const unsigned long PUMP_ON_TIME;
extern const unsigned long PUMP_OFF_TIME;

// VARIABLE GLOBAL
extern bool automaticMode;
extern bool pumpState;
extern unsigned long previousMillis;

// FUNGSI
void pumpON();
void pumpOFF();
void showStatus();
void showMenu();

#endif

