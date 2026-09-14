#pragma once

#include <Arduino.h>

// POST ke tabel sensor_logs
void insertSensorLog(float distanceCm, float levelPercent);

// POST ke tabel alert_logs (di-skip kalau status NORMAL)
void insertAlertLog(String alertType, float levelAtTrigger);

// PATCH kolom is_online di tabel devices
void updateDeviceOnline(bool online);

// GET threshold dari tabel threshold_configs -> update thLow / thHigh
void fetchThresholdConfig();