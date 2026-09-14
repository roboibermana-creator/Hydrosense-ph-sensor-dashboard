#ifndef VALVE_CONTROLLER_H
#define VALVE_CONTROLLER_H

#include <Arduino.h>
#include "config.h" // Include config untuk pin

// Logika relay berdasarkan tipe trigger
#define RELAY_HIGH_TRIGGER HIGH
#define RELAY_LOW_TRIGGER LOW

// Level ON/OFF untuk Valve 1 (Relay Shield -> High Trigger)
extern const int V1_ON;
extern const int V1_OFF;

// Level ON/OFF untuk Pompa (Relay Eksternal -> Low Trigger)
extern const int PUMP_ON;
extern const int PUMP_OFF;


class Valve_Controller {
private:
  bool valve1Status; // Status Valve 1 (true=OPEN)


public:
  Valve_Controller();
  
  void begin();

  void openValve1();
  void closeValve1();

  bool getValve1Status();

  void printCompactStatus();
  void printStatus();
};

#endif // VALVE_CONTROLLER_H