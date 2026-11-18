#pragma once

// Objek & fungsi yang dibagi antar file aplikasi (main, Tasks, Hardware,
// WebSync, SerialMenu). Definisi objek ada di main.cpp.
#include "Config.h"
#include "Level_Sensor.h"
#include "Modes.h"
#include "Network.h"
#include "Pompa.h"
#include "Sensor.h"
#include "TSS_Sensor.h"
#include "Valve_Controller.h"
#include <Arduino.h>

extern Valve_Controller valveController;
extern TSS_Sensor tssSensor;
extern SemaphoreHandle_t modbusMutex;

// Native TSS register calibration is disabled until the RD-SSBWT-01
// register map is verified. Software slope/offset calibration remains enabled.
static constexpr bool ENABLE_TSS_HW_REGISTER_CALIBRATION = false;

// --- WebSync.cpp ---
extern unsigned long timerCekKalibrasiTssTerakhir;
void cekOverrideManualDariWeb();
void cekKalibrasiTSSDariWeb();
void cekPerintahKalibrasiTssHwDariWeb();
void cekPerintahKalibrasiPhDariWeb();
void cekJadwalOtomatisDariWeb();
void cekMinAirDariWeb();
void cekPerintahModbusDariWeb();

// --- SerialMenu.cpp ---
void tampilkanMenuSerial();
void prosesPerintahSerial(char command); // pemanggil memegang mutex Modbus
void kirimDataManualSekarang();

// --- Network.cpp / Hardware.cpp ---
void connectWiFi();
bool supabaseSiap();
bool bacaModeOperasiDariSupabase();
void cobaKirimUlangBuffer();

// --- Tasks.cpp ---
