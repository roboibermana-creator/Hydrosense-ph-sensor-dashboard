#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include "Sensor.h"

extern bool wifiReady;

// Deklarasi Fungsi Jaringan
void setupWiFi();
void setupTime();
void firebaseLogReading(const PhReading &r);   // nilai bersih + metadata mutu (bagian 7)
void firebasePushCalStatus(bool running, const char *label, unsigned long elapsedSec, int sampleNum, double avg, double range, const char *message);
void firebasePushCalPoints();
String firebaseGetCommand();
void firebaseClearCommand();
void checkFirebaseCommand();

#endif