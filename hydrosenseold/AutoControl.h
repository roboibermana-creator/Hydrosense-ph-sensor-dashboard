#ifndef AUTOCONTROL_H
#define AUTOCONTROL_H

#include <Arduino.h>
#include "Valve_Controller.h"
#include "pH_Sensor.h"
#include "TSS_Sensor.h"
// Level_Sensor DIHAPUS
#include "Flow_Sensor.h"
#include "Firebase_Handler.h"
#include "config.h"

enum ControlState {
  STATE_IDLE,
  STATE_FILLING,
  STATE_SAMPLING,
  STATE_DOSING,
  STATE_DRAINING,
  STATE_SENSOR_ERROR
};

class AutoControl {
private:
  Valve_Controller* valve;
  pH_Sensor* phSensor;
  TSS_Sensor* tssSensor;
  // Level_Sensor DIHAPUS
  Flow_Sensor* flowSensor;
  Firebase_Handler* firebase;

  ControlState currentState = STATE_IDLE;
  unsigned long stateStartTime = 0;
  bool systemRunning = false;

  float phSum = 0, tssSum = 0;
  int sampleCount = 0;
  float phAverage = 0, tssAverage = 0;
  float currentFlowRate = 0;
  unsigned long lastSampleTime = 0;

  uint8_t dosingPercent = 0;
  bool dosingActive = false;
  unsigned long valveOnDurationMs = 0;

  int cycleNumber = 0;

  // Helper untuk kirim data cepat
  void sendUpdateToFirebase() {
    // Menggunakan nilai rata-rata (phAverage/tssAverage) karena di fase dosing/draining sensor tidak dibaca ulang
    // Jika masih di fase sampling, variabel ini mungkin belum valid, tapi logic pemanggilan di bawah sudah disesuaikan
    float phSend = (currentState == STATE_SAMPLING) ? phSum/sampleCount : phAverage;
    float tssSend = (currentState == STATE_SAMPLING) ? tssSum/sampleCount : tssAverage;
    
    // Jika pembagi 0 (belum ada sample), pakai nilai -1
    if (currentState == STATE_SAMPLING && sampleCount == 0) { phSend = -1; tssSend = -1; }

    firebase->sendData(phSend, tssSend, currentFlowRate,
                       valve->getValve1Status(),
                       valve->getValve2Status(),
                       valve->getPumpStatus(),
                       true,           // Auto Mode = TRUE
                       dosingPercent);
  }

public:
  // HAPUS parameter Level_Sensor
  void begin(Valve_Controller* v, pH_Sensor* ph, TSS_Sensor* tss, Flow_Sensor* flow, Firebase_Handler* fb) {
    valve = v;
    phSensor = ph;
    tssSensor = tss;
    flowSensor = flow;
    firebase = fb;
    Serial.println("✓ Auto Control Ready (No Pond Level Check)");
  }

  void start() {
    systemRunning = true;
    stateStartTime = millis();

    Serial.println("\nChecking start conditions...");

    // --- LOGIKA LEVEL KOLAM DIHAPUS ---
    // Hanya cek Flow Inlet
    float inletLevel = flowSensor->readLevelMeter();
    float inletFlow = flowSensor->calculateFlowRate(inletLevel);
    bool inletFlowOK = (inletFlow > 0.001);
    
    Serial.print("  - Inlet Flow: ");
    if (inletLevel >= 0) {
      Serial.print(inletFlow * 86400, 2); Serial.print(" m3/d");
      Serial.println(inletFlowOK ? " (✓ OK)" : " (✗ NO FLOW)");
    } else {
      Serial.println("ERROR (Flow Sensor)");
      inletFlowOK = false;
    }

    if (inletFlowOK) {
      currentState = STATE_FILLING;
      phSum = tssSum = 0;
      sampleCount = 0;
      cycleNumber++;

      Serial.println("\n╔════════════════════════════════════════╗");
      Serial.println("║ AUTO CYCLE STARTED (Flow Detected)     ║");
      Serial.println("╚════════════════════════════════════════╝");
      Serial.print("[1/4] FILLING PIPE (");
      Serial.print(PUMP_FILL_TIME / 1000);
      Serial.println("s)");
      valve->pumpOn();
      // Update status pompa nyala ke App
      sendUpdateToFirebase(); 
    } else {
      Serial.println("✗ Start conditions not met!");
      Serial.println("  Reason: No inlet flow detected.");
      Serial.println("  Auto-cycle paused. Will re-check in 1 minute.");
      currentState = STATE_IDLE;
      valve->pumpOff();
      valve->closeValve1();
      valve->closeValve2();
    }
  }

  void stop() {
    systemRunning = false;
    currentState = STATE_IDLE;
    valve->pumpOff();
    valve->closeValve1();
    valve->closeValve2();
    dosingPercent = 0; 
    Serial.println("\n✓ AUTO CONTROL STOPPED\n");
    // HAPUS parameter levelPond
    firebase->sendData(phAverage, tssAverage, currentFlowRate,
                       valve->getValve1Status(), valve->getValve2Status(), valve->getPumpStatus(),
                       false, dosingPercent);
  }

  bool isRunning() { return systemRunning; }

  void update() {
    if (!systemRunning) return;

    unsigned long elapsed = millis() - stateStartTime;

    if (currentState == STATE_IDLE || currentState == STATE_SENSOR_ERROR) {
      if (elapsed >= 60000) {
         Serial.println("\nRetrying auto-cycle start...");
         start();
      }
      return;
    }

    switch (currentState) {
      case STATE_FILLING:
        if (elapsed >= PUMP_FILL_TIME) {
          valve->pumpOff();
          // Kirim status pompa mati
          firebase->sendData(-1, -1, currentFlowRate, valve->getValve1Status(), valve->getValve2Status(), valve->getPumpStatus(), true, 0);
          
          currentState = STATE_SAMPLING;
          stateStartTime = millis();
          lastSampleTime = millis();
          Serial.print("\n[2/4] SAMPLING (");
          Serial.print(SAMPLING_DURATION / 60000);
          Serial.println(" min)");
        }
        break;

      case STATE_SAMPLING:
        if (millis() - lastSampleTime >= 5000) {
          float ph = phSensor->readPH(); delay(200);
          float tss = tssSensor->readTSS(); delay(200);
          float inletLevel = flowSensor->readLevelMeter();
          currentFlowRate = flowSensor->calculateFlowRate(inletLevel);

          if (ph >= 0 && tss >= 0) {
            phSum += ph;
            tssSum += tss;
            sampleCount++;
            Serial.print("Sample "); Serial.print(sampleCount);
            Serial.print(": pH="); Serial.print(ph, 2);
            Serial.print(", TSS="); Serial.print(tss, 1);
            Serial.print(", Flow="); Serial.print(currentFlowRate * 86400, 1);
            Serial.println(" m3/d");
          } else {
             Serial.print("Sample "); Serial.print(sampleCount + 1); Serial.println(": pH/TSS Read Error");
          }
          
          // === UPDATE APLIKASI SAAT SAMPLING BERJALAN ===
          firebase->sendData(ph, tss, currentFlowRate,
                             valve->getValve1Status(),
                             valve->getValve2Status(),
                             valve->getPumpStatus(),
                             true,
                             dosingPercent);
          // ==============================================

          lastSampleTime = millis();
        }

        if (elapsed >= SAMPLING_DURATION) {
          if (sampleCount > 0) {
            phAverage = phSum / sampleCount;
            tssAverage = tssSum / sampleCount;
          } else {
            phAverage = -1.0;
            tssAverage = -1.0;
            Serial.println("\nWARNING: No valid pH/TSS samples collected!");
          }

          float inletLevel_final = flowSensor->readLevelMeter();
          if (inletLevel_final >= 0) {
              currentFlowRate = flowSensor->calculateFlowRate(inletLevel_final);
          } else {
              currentFlowRate = -1.0;
          }

          Serial.println("\n✓ Sampling done");
          Serial.print("Avg pH: "); Serial.println(phAverage, 2);
          Serial.print("Avg TSS: "); Serial.println(tssAverage, 1);
          Serial.print("Inlet Flow (Final): "); Serial.print(currentFlowRate * 86400, 2); Serial.println(" m3/d");

          bool tssError = (tssAverage < 0);
          bool flowError = (currentFlowRate < 0);

          if (tssError && flowError) {
              Serial.println("\nCRITICAL ERROR: TSS and Flow sensors failed!");
              currentState = STATE_SENSOR_ERROR;
              stateStartTime = millis();
              valve->pumpOff();
              valve->closeValve1();
              valve->closeValve2();
              return;
          } else if (tssError) {
              Serial.println("\nWARNING: TSS sensor failed! Dosing calculation will ignore TSS.");
          } else if (flowError) {
              Serial.println("\nWARNING: Flow sensor failed! Dosing calculation will ignore flow rate.");
          }
          
          currentState = STATE_DOSING;
          stateStartTime = millis();
          calculateDosing(); 
        }
        break;

      case STATE_DOSING:
        // Cek jika waktu dosing (Tawas) selesai
        if (dosingActive && elapsed >= valveOnDurationMs) {
          Serial.println("✓ Dosing time complete, closing tawas valve.");
          valve->closeValve2();
          dosingActive = false;
          
          // === TAMBAHAN 1: Update App saat Valve Tawas TUTUP ===
          sendUpdateToFirebase();
          // ====================================================
        }

        // Transisi ke Draining (Buka Valve Buang)
        if (elapsed >= (VALVE2_FULL_OPEN_TIME_MS + 5000)) {
          valve->closeValve2(); // Pastikan tertutup
          currentState = STATE_DRAINING;
          stateStartTime = millis();
          Serial.print("\n[4/4] DRAINING (");
          Serial.print(DRAIN_TIME / 1000);
          Serial.println("s)");
          valve->openValve1();
          
          // === TAMBAHAN 2: Update App saat Valve Drain BUKA ===
          sendUpdateToFirebase();
          // ====================================================
        }
        break;

      case STATE_DRAINING:
        if (elapsed >= DRAIN_TIME) {
          valve->closeValve1();
          
          // === TAMBAHAN 3: Update App saat Valve Drain TUTUP (Siklus Selesai) ===
          sendUpdateToFirebase();
          // ======================================================================

          Serial.println("\n✓ CYCLE COMPLETE\n");
          delay(3000);
          start();
        }
        break;
    } 
  } 

  void calculateDosing() {
    Serial.println("\n[3/4] CALCULATING DOSING");

    bool tssError = (tssAverage < 0);

    if (phAverage < 0 && tssError) {
      Serial.println("✗ pH and TSS sensor read error, skipping dosing calculation.");
      dosingPercent = 0;
    } else {
      bool phOK = (phAverage >= PH_MIN && phAverage <= PH_MAX);
      bool tssOK = tssError ? true : (tssAverage <= TSS_MAX);

      Serial.print("Avg pH: ");
      if(phAverage >= 0) { Serial.print(phAverage, 2); Serial.println(phOK ? " (✓ OK)" : " (✗)"); }
      else { Serial.println("ERROR"); }

      Serial.print("Avg TSS: ");
      if(!tssError) { Serial.print(tssAverage, 1); Serial.println(tssOK ? " (✓ OK)" : " (✗)"); }
      else { Serial.println("ERROR"); }

      if (phOK && tssOK) {
        dosingPercent = 0;
        Serial.println("→ Base Dosing Needed: 0%");
      } else {
        if (!tssError) {
            if (tssAverage > 500) dosingPercent = 100;
            else if (tssAverage > 300) dosingPercent = 75;
            else if (tssAverage > 200) dosingPercent = 50;
            else if (tssAverage > TSS_MAX) dosingPercent = 25;
            else dosingPercent = 0;
        } else {
            dosingPercent = 0;
        }

        if (phAverage >= 0) {
            if (phAverage < PH_MIN) {
               if (tssOK || tssError) dosingPercent = max(dosingPercent, (uint8_t)10);
               else dosingPercent *= 0.8;
            } else if (phAverage > PH_MAX) {
               if (tssOK || tssError) dosingPercent = max(dosingPercent, (uint8_t)10);
               else dosingPercent = min(100, (int)(dosingPercent * 1.2));
            }
        }
        Serial.print("→ Base Dosing Needed (pH/TSS): ");
        Serial.print(dosingPercent);
        Serial.println("%");
      }
    }

    bool flowError = (currentFlowRate < 0);
    float flowAdjustmentFactor = 1.0;

    if (!flowError && NOMINAL_FLOW_RATE > 0) {
       flowAdjustmentFactor = currentFlowRate / NOMINAL_FLOW_RATE;
       Serial.print("→ Flow Rate Adjustment Factor: ");
       Serial.print(flowAdjustmentFactor, 2);
       Serial.print(" (Current: "); Serial.print(currentFlowRate * 86400, 1);
       Serial.print(" m3/d / Nominal: "); Serial.print(NOMINAL_FLOW_RATE * 86400, 1);
       Serial.println(" m3/d)");
    } else {
       Serial.println("→ Flow Rate data invalid, using default adjustment (1.0).");
    }

    dosingPercent = min(100, (int)round(dosingPercent * flowAdjustmentFactor));

    Serial.print("→ Final Dosing Percent: ");
    Serial.print(dosingPercent);
    Serial.println("%");

    valveOnDurationMs = 0;
    dosingActive = false;

    if (dosingPercent > 0) {
      valveOnDurationMs = VALVE2_START_DELAY_MS +
                          (unsigned long)((VALVE2_FULL_OPEN_TIME_MS - VALVE2_START_DELAY_MS) * (dosingPercent / 100.0));

      float durationSec = valveOnDurationMs / 1000.0;
      Serial.print("→ RESULT: OPENING TAWAS VALVE for ");
      Serial.print(durationSec, 1);
      Serial.println(" seconds");
      valve->openValve2();
      dosingActive = true;
    } else {
      Serial.println("→ RESULT: TAWAS VALVE REMAINS CLOSED");
      valve->closeValve2();
      dosingActive = false;
    }
    
    // Update App saat keputusan Dosing diambil (Valve Tawas Buka)
    sendUpdateToFirebase();
  }

  void printStatus() {
    Serial.println("--- Auto Control ---");
    Serial.print("Status: ");
    Serial.println(systemRunning ? "RUNNING" : "STOPPED");
    if (systemRunning) {
      Serial.print("Cycle: #");
      Serial.println(cycleNumber);
      Serial.print("State: ");
      switch(currentState) {
        case STATE_IDLE:
          if(systemRunning) Serial.println("IDLE (Flow low, retrying...)");
          else Serial.println("IDLE (Stopped)");
          break;
        case STATE_FILLING: Serial.println("FILLING"); break;
        case STATE_SAMPLING: Serial.println("SAMPLING"); break;
        case STATE_DOSING: Serial.println("DOSING"); break;
        case STATE_DRAINING: Serial.println("DRAINING"); break;
        case STATE_SENSOR_ERROR: Serial.println("ERROR"); break;
      }
    }
  }
  
  ControlState getCurrentState() { return currentState; }
};

#endif // AUTOCONTROL_H