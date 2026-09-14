#include "config.h"

// ---- SENSOR ----
const float I_MIN_MA = 4.0;
const float I_MAX_MA = 20.0;
const float V_MIN_V  = 0.0;
const float V_MAX_V  = 3.0;

const float TANK_HEIGHT_CM = 200.0;

const bool SENSOR_INVERTED = false;

// ---- WIFI ----
const unsigned long WIFI_CONNECT_TIMEOUT    = 20000;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000;

// ---- THRESHOLD ----
float thLow  = 20.0;
float thHigh = 80.0;

const float TH_LOW_LOW   = 5.0;
const float TH_HIGH_HIGH = 90.0;

String lastStatus = "NORMAL";

// ---- TIMER ----
const unsigned long SEND_INTERVAL_MS             = 5000;
const unsigned long THRESHOLD_FETCH_INTERVAL_MS  = 60000;