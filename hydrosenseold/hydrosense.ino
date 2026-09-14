// ========================== INCLUDES ==========================
#include "config.h"          
#include "pH_Sensor.h"       
#include "TSS_Sensor.h"      
// Level_Sensor DIHAPUS  
#include "Flow_Sensor.h"     
#include "Valve_Controller.h"
#include "Firebase_Handler.h"
#include "AutoControl.h"     

// ======================= OBJEK GLOBAL =======================
pH_Sensor phSensor;
TSS_Sensor tssSensor;
// Level_Sensor DIHAPUS
Flow_Sensor flowSensor;
Valve_Controller valveController;
Firebase_Handler fbHandler;   
AutoControl autoController;

// ====================== VARIABEL GLOBAL =====================
bool manualSamplingActive = false;
unsigned long manualSamplingStartTime = 0;
unsigned long lastScrapingTime = 0;

// ==================== FUNGSI CALLBACK FIREBASE ====================
void firebaseStreamCallback(StreamData data) {
  Serial.println("\n--- Firebase Command Received ---");
  Serial.print("Path: "); Serial.println(data.dataPath());
  Serial.print("Type: ");
  Serial.println(data.dataType());

  if (data.dataType() == "json" || data.dataType() == "json_string") {
      FirebaseJson *json = data.jsonObjectPtr();
      if (json && json->iteratorBegin() > 0) {
          FirebaseJsonData result;
          size_t count = json->iteratorBegin();
          String key, value;
          int type;
          Serial.println("Commands received (JSON):");
          for (size_t i = 0; i < count; i++) {
              json->iteratorGet(i, type, key, value);
              if (key == "command_timestamp") continue; 
              Serial.print("  -> Key: '"); Serial.print(key);
              Serial.print("', Value: '"); Serial.print(value); Serial.println("'");
              processCommand(key, value.toInt());
          } 
          json->iteratorEnd();
      } else { 
          Serial.println("  (Empty JSON or null data received, likely deletion)");
      }
      Serial.println("--- End Firebase Command ---");
      return;
  }
  
  else if (data.dataPath() != "/" && data.dataPath().length() > 1) {
      String key = data.dataPath().substring(1);
      if (key == "command_timestamp") return;

      if (data.dataType() == "null") return;

      int intValue = -1;
      String value = "";
      if (data.dataType() == "int") {
          intValue = data.intData();
          value = String(intValue);
      } else if (data.dataType() == "string") { 
          value = data.stringData();
          intValue = value.toInt();
      } else {
          Serial.println("  ! Unknown data type for child update.");
          return;
      }
      
      Serial.print("  -> Key: '"); Serial.print(key);
      Serial.print("', Value: '"); Serial.print(value); Serial.println("'");
      
      processCommand(key, intValue);
      Serial.println("--- End Firebase Command ---");
      return;
  }
  
  else {
      Serial.print("! Ignoring stream data type/path: ");
      Serial.println(data.dataType());
  }
  Serial.println("--- End Firebase Command ---");
}

// --- FUNGSI UNTUK MENGATUR FLAG ---
volatile bool pendingCommand_AutoStop = false;
volatile bool pendingCommand_AutoStart = false;
volatile bool pendingCommand_Pump = false;
volatile int  pendingCommand_PumpValue = 0;
volatile bool pendingCommand_Valve1 = false;
volatile int  pendingCommand_Valve1Value = 0;
volatile bool pendingCommand_Valve2Open = false;
volatile bool pendingCommand_Valve2Close = false;
volatile bool pendingCommand_Valve2Dose = false;
volatile int  pendingCommand_Valve2DoseValue = 0;
String nodeToDelete = "";

void processCommand(String key, int intValue) {
    if (key == "AUTO_STOP") {
        Serial.println(">>> Flag set: AUTO_STOP");
        pendingCommand_AutoStop = true;
        nodeToDelete = key; 
    }
    else if (key == "AUTO_START") {
        Serial.println(">>> Flag set: AUTO_START");
        pendingCommand_AutoStart = true;
        nodeToDelete = key; 
    }
    else if (!autoController.isRunning())
    {
        if (key == "pump") {
            pendingCommand_Pump = true;
            pendingCommand_PumpValue = intValue;
            nodeToDelete = key;
        } else if (key == "valve1") {
            pendingCommand_Valve1 = true;
            pendingCommand_Valve1Value = intValue;
            nodeToDelete = key;
        } else if (key == "V2_OPEN" || (key == "valve2" && intValue == 1)) {
            pendingCommand_Valve2Open = true;
            nodeToDelete = key;
        } else if (key == "V2_CLOSE" || (key == "valve2" && intValue == 0)) {
            pendingCommand_Valve2Close = true;
            nodeToDelete = key;
        } else if (key == "valve2_dose") {
            if (intValue > 0 && intValue <= 100) {
                pendingCommand_Valve2Dose = true;
                pendingCommand_Valve2DoseValue = intValue;
                nodeToDelete = key;
            } else { Serial.println("✗ Invalid dose value."); }
        } else { Serial.println("  -> Unknown manual command key."); }
    }
    else {
        Serial.println("✗ Command '"+ key + "' ignored, AUTO mode cycle active.");
        nodeToDelete = key; 
    }
}


// ========================== SETUP ===========================
void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("\n\n╔════════════════════════════════════════╗");
  Serial.println("║   HYDROSENSE IoT SYSTEM v3.1          ║");
  Serial.println("║   (No Pond Level Sensor Version)      ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  phSensor.begin();
  delay(500);
  tssSensor.begin(); delay(500);
  Serial.println("→ Setting TSS auto scraping: 60 minutes");
  tssSensor.setScrapingTime(60); delay(500);
  
  // Level Sensor DIHAPUS dari setup
  
  flowSensor.begin(); delay(200);
  valveController.begin(); delay(200);
  
  // HAPUS parameter &levelSensor
  autoController.begin(&valveController, &phSensor, &tssSensor, &flowSensor, &fbHandler); 
  fbHandler.begin();

  if (Firebase.ready()) { fbHandler.beginControlStream(firebaseStreamCallback); }
  else { Serial.println("✗ Cannot start Firebase stream, Firebase not ready."); }

  printMenu();
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║          SYSTEM READY!                 ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║ • TSS Auto Scraping: Every 60 minutes ║");
  Serial.println("║ • Type AUTO_START to begin cycle      ║");
  Serial.println("║ • Type MENU for all commands          ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  lastScrapingTime = millis();
}

// ========================= LOOP UTAMA =========================
void loop() {
  
  // --- BAGIAN EKSEKUSI PERINTAH ---
  if (pendingCommand_AutoStop) {
    pendingCommand_AutoStop = false;
    Serial.println(">>> Executing: AUTO_STOP");
    autoController.stop(); 
    manualSamplingActive = false;
  }
  if (pendingCommand_AutoStart) {
    pendingCommand_AutoStart = false;
    Serial.println(">>> Executing: AUTO_START");
    if (!autoController.isRunning()) {
      manualSamplingActive = false;
      autoController.start();
      Firebase.setBool(fbHandler.getFirebaseDataObject(), String(FIREBASE_LATEST_DATA_PATH) + "/auto_mode", true);
      Firebase.setInt(fbHandler.getFirebaseDataObject(), String(FIREBASE_LATEST_DATA_PATH) + "/valve2_percent", 0); 
      Firebase.setTimestamp(fbHandler.getFirebaseDataObject(), String(FIREBASE_LATEST_DATA_PATH) + "/last_update");
    } else {
      Serial.println("  (Already running/pending, command ignored)");
    }
  }
  if (pendingCommand_Pump) {
    pendingCommand_Pump = false;
    Serial.println(">>> Executing: Pump");
    if (pendingCommand_PumpValue == 1) valveController.pumpOn();
    else valveController.pumpOff(); delay(250);
    if (pendingCommand_PumpValue == 1) { 
      manualSamplingActive = true;
      manualSamplingStartTime = millis();
      Serial.println("✓ Manual Sampling Window ACTIVATED via App");
    }
  }
  if (pendingCommand_Valve1) {
    pendingCommand_Valve1 = false;
    Serial.println(">>> Executing: Valve1");
    if (pendingCommand_Valve1Value == 1) valveController.openValve1();
    else valveController.closeValve1(); delay(250);
    manualSamplingActive = false;
    if(pendingCommand_Valve1Value == 1) Serial.println("! Manual Sampling Window DEACTIVATED via App (Drain)");
  }
  if (pendingCommand_Valve2Open) {
    pendingCommand_Valve2Open = false;
    Serial.println(">>> Executing: Valve2 Open");
    valveController.openValve2(); delay(250);
  }
  if (pendingCommand_Valve2Close) {
    pendingCommand_Valve2Close = false;
    Serial.println(">>> Executing: Valve2 Close");
    valveController.closeValve2(); delay(250);
  }
  if (pendingCommand_Valve2Dose) {
    pendingCommand_Valve2Dose = false;
    Serial.println(">>> Executing: Valve2 Dose");
    unsigned long duration_ms = VALVE2_START_DELAY_MS + (unsigned long)((VALVE2_FULL_OPEN_TIME_MS - VALVE2_START_DELAY_MS) * (pendingCommand_Valve2DoseValue / 100.0));
    Serial.print("\n>>> App Dose Request: "); Serial.print(pendingCommand_Valve2DoseValue);
    Serial.print("% -> "); Serial.print(duration_ms / 1000.0, 1); Serial.println("s");
    Serial.println("  Opening valve..."); valveController.openValve2(); delay(duration_ms);
    Serial.print("  Closing valve (for "); Serial.print(duration_ms / 1000.0, 1); Serial.println("s)..."); 
    valveController.closeValve2(); 
    delay(duration_ms);
    Serial.println("✓ App dosing complete.");
  }
  
  if (nodeToDelete != "") {
    String path = String(FIREBASE_CONTROL_REQUEST_PATH) + "/" + nodeToDelete;
    fbHandler.deleteNode(path);
    fbHandler.deleteNode(String(FIREBASE_CONTROL_REQUEST_PATH) + "/command_timestamp"); 
    nodeToDelete = "";
  }

  // 1. Cek Auto Scraping TSS
  if (millis() - lastScrapingTime >= SCRAPING_INTERVAL) {
    Serial.println("\n--- Auto Scraping Check ---");
    if(!autoController.isRunning() || autoController.getCurrentState() == STATE_IDLE || autoController.getCurrentState() == STATE_SENSOR_ERROR) {
       Serial.println("✓ Starting scheduled TSS scraping...");
       tssSensor.startScraping();
       lastScrapingTime = millis();
    } else {
       Serial.println("  (Skipped, auto cycle running)");
    }
    Serial.println("---------------------------");
  }

  // 2. Cek Perintah dari Serial Monitor
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    handleCommand(command);
  }

  // 3. Cek Data Baru di Firebase Stream
  fbHandler.readControlStream();

  // 4. Jalankan Logika Mode Auto atau Manual
  if (autoController.isRunning()) {
    autoController.update();
  } else {
    // ===== MODE MANUAL =====
    if (manualSamplingActive && (millis() - manualSamplingStartTime > SAMPLING_DURATION)) {
      Serial.println("\n!! Manual sampling window expired. Pipe data may be stale.");
      manualSamplingActive = false;
    }

    static unsigned long lastManualUpdate = 0;
    if (millis() - lastManualUpdate >= 5000) {
      Serial.println("\n========== MANUAL MODE UPDATE ==========");
      
      // HAPUS Bacaan Level Pond
      
      float levelInletValue = flowSensor.readLevelMeter(); delay(50);
      float flowRateValue = flowSensor.calculateFlowRate(levelInletValue);
      
      float phValue = -1.0, tssValue = -1.0;
      if (manualSamplingActive) {
        Serial.println("  (Sampling Active - Reading pH/TSS)");
        phValue = phSensor.readPH();
        delay(200);
        tssValue = tssSensor.readTSS(); delay(200);
        if (phValue == -1.0) phValue = -2.0;
        if (tssValue == -1.0) tssValue = -2.0;
      } else { Serial.println("  (Sampling Inactive)"); } 

       // Update Serial Monitor
       if (phValue >= 0) { Serial.print("pH: "); Serial.print(phValue, 2); Serial.println((phValue >= PH_MIN && phValue <= PH_MAX) ? " ✓" : " ✗"); }
       else if (phValue == -1.0) { Serial.println("pH: --"); } 
       else { Serial.println("pH: ERROR"); }
       
       if (tssValue >= 0) { Serial.print("TSS: "); Serial.print(tssValue, 1); Serial.print(" mg/L"); Serial.println((tssValue <= TSS_MAX) ? " ✓" : " ✗"); }
       else if (tssValue == -1.0) { Serial.println("TSS: --"); } 
       else { Serial.println("TSS: ERROR"); }
       
       if (levelInletValue >= 0) { 
         Serial.print("Inlet Level: ");
         Serial.print(levelInletValue, 2); Serial.println(" m");
         Serial.print("Inlet Flow: ");
         Serial.print(flowRateValue * 86400, 2); Serial.println(" m3/d"); 
       }
       else { Serial.println("Inlet Level/Flow: ERROR"); }

      valveController.printCompactStatus();
      unsigned long timeUntilScraping = (SCRAPING_INTERVAL - (millis() - lastScrapingTime)) / 60000;
      Serial.print("Next auto scraping: "); Serial.print(timeUntilScraping); Serial.println(" min");
      Serial.println("=====================================\n");

       // Kirim data manual (HAPUS levelPond)
       fbHandler.sendData(phValue, tssValue, flowRateValue,
                         valveController.getValve1Status(),
                         valveController.getValve2Status(),
                         valveController.getPumpStatus(),
                         false, 
                         0);
      lastManualUpdate = millis();
    } 
  } 
} 


// ========================= MENU & HANDLER SERIAL =========================
void printMenu() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║           MENU KONTROL                 ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║ AUTOMATIC CONTROL                      ║");
  Serial.println("║  • AUTO_START    - Start auto cycle    ║");
  Serial.println("║  • AUTO_STOP     - Stop auto cycle     ║");
  Serial.println("║  • AUTO_STATUS   - Check status        ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║ MANUAL CONTROL                         ║");
  Serial.println("║  • PUMP_ON       - (Start Manual Sample) ║");
  Serial.println("║  • PUMP_OFF      - Turn OFF pump       ║");
  Serial.println("║  • V1_OPEN       - Drain pipe (Stop Sample) ║");
  Serial.println("║  • V1_CLOSE      - Close valve 1       ║");
  Serial.println("║  • V2_DOSE:50    - Dose tawas (% time) ║");
  Serial.println("║  • V2_OPEN       - Open valve 2 (Tahan)║");
  Serial.println("║  • V2_CLOSE      - Close valve 2 (Stop)║");
  Serial.println("║  • VALVE_STATUS  - Check all valves    ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║ SENSORS                                ║");
  Serial.println("║  • SCRAPING      - Force scraping now  ║");
  Serial.println("║  • STATUS_TSS    - Check TSS status    ║");
  Serial.println("║  • STATUS_FLOW   - Check Inlet Level/Flow ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║ SYSTEM                                 ║");
  Serial.println("║  • STATUS_ALL    - Complete status     ║");
  Serial.println("║  • MENU          - Show this menu      ║");
  Serial.println("╚════════════════════════════════════════╝\n");
}

void handleCommand(String cmd) {
    cmd.toUpperCase();
    Serial.print("\nReceived Serial Command: ");
    Serial.println(cmd);

    if (cmd == "AUTO_START") {
        manualSamplingActive = false;
        Serial.println(">>> Starting Automatic Control Cycle...");
        autoController.start();
    } else if (cmd == "AUTO_STOP") {
        Serial.println(">>> Stopping Automatic Control...");
        autoController.stop();
        manualSamplingActive = false;
    } else if (cmd == "AUTO_STATUS") {
        autoController.printStatus();
    } else if (cmd == "PUMP_ON") {
        if (!autoController.isRunning()) {
             Serial.println(">>> Turning ON Pump (Manual Sample Start)...");
             valveController.pumpOn(); delay(250);
             manualSamplingActive = true; manualSamplingStartTime = millis();
             Serial.println("✓ Manual Sampling Window ACTIVATED (5 min)");
        } else { Serial.println("✗ AUTO mode active!"); }
    } else if (cmd == "PUMP_OFF") {
        if (!autoController.isRunning()) { Serial.println(">>> Turning OFF Pump...");
        valveController.pumpOff(); delay(250); }
        else { Serial.println("✗ AUTO mode active!"); }
    } else if (cmd == "V1_OPEN") {
        if (!autoController.isRunning()) {
             Serial.println(">>> Opening Valve 1 (Drain)...");
             valveController.openValve1(); delay(250);
             manualSamplingActive = false; Serial.println("! Manual Sampling DEACTIVATED (Drain)");
        } else { Serial.println("✗ AUTO mode active!"); }
    } else if (cmd == "V1_CLOSE") {
        if (!autoController.isRunning()) { Serial.println(">>> Closing Valve 1...");
        valveController.closeValve1(); delay(250); manualSamplingActive = false; }
        else { Serial.println("✗ AUTO mode active!"); }
    } else if (cmd.startsWith("V2_DOSE:")) {
        if (!autoController.isRunning()) {
             int percent = cmd.substring(8).toInt();
             if (percent > 0 && percent <= 100) {
                 unsigned long duration_ms = VALVE2_START_DELAY_MS + (unsigned long)((VALVE2_FULL_OPEN_TIME_MS - VALVE2_START_DELAY_MS) * (percent / 100.0));
                 Serial.print("\n>>> Manual Dosing: "); Serial.print(percent); Serial.print("% -> "); Serial.print(duration_ms / 1000.0, 1); Serial.println("s)");
                 Serial.println("  Opening valve..."); valveController.openValve2();
                 delay(duration_ms);
                 Serial.print("  Closing valve (for "); Serial.print(duration_ms / 1000.0, 1); Serial.println("s)...");
                 valveController.closeValve2(); 
                 delay(duration_ms); 
                 Serial.println("✓ Manual dosing complete.");
             } else { Serial.println("✗ Invalid! Use V2_DOSE:1-100"); }
        } else { Serial.println("✗ AUTO mode active!"); }
    } else if (cmd == "V2_OPEN") {
        if (!autoController.isRunning()) { Serial.println(">>> Opening Valve 2 (Tahan)...");
        valveController.openValve2(); delay(250); }
        else { Serial.println("✗ AUTO mode active!"); }
    } else if (cmd == "V2_CLOSE") {
        if (!autoController.isRunning()) { Serial.println(">>> Closing Valve 2...");
        valveController.closeValve2(); delay(250); }
        else { Serial.println("✗ AUTO mode active!"); }
    } else if (cmd == "VALVE_STATUS") {
        valveController.printStatus();
    } else if (cmd == "SCRAPING") {
        Serial.println(">>> Force Manual Scraping NOW..."); tssSensor.startScraping();
        lastScrapingTime = millis();
    } else if (cmd == "STATUS_TSS") {
        tssSensor.readStatus();
        unsigned long nextScraping = (SCRAPING_INTERVAL - (millis() - lastScrapingTime)) / 60000;
        Serial.print("\n⏰ Next auto scraping in: "); Serial.print(nextScraping); Serial.println(" minutes\n");
    } else if (cmd == "STATUS_FLOW") {
         Serial.println("\n--- Inlet Flow Sensor Status ---");
         float levelInlet = flowSensor.readLevelMeter();
         if (levelInlet >= 0) { float flow = flowSensor.calculateFlowRate(levelInlet); Serial.print("Ketinggian Inlet: "); Serial.print(levelInlet, 2);
         Serial.print(" m | Debit: ");
         Serial.print(flow * 86400, 2); Serial.println(" m3/d");
         }
         else { Serial.println("✗ Error reading sensor"); }
    }
    else if (cmd == "STATUS_ALL") {
        printSystemStatus();
    } else if (cmd == "MENU") {
        printMenu();
    } else {
        Serial.println("✗ Unknown command!");
    }
}

// ======================= STATUS SISTEM LENGKAP =======================
void printSystemStatus() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║      COMPLETE SYSTEM STATUS            ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  Serial.println("┌─ Automatic Control ─────────────────┐");
  autoController.printStatus();
  Serial.println("└─────────────────────────────────────┘\n");
  Serial.println("┌─ Sensor Readings ───────────────────┐");

  // HAPUS Bacaan Level Pond
  
  float levelInlet = flowSensor.readLevelMeter(); delay(50);
  float flowRate = flowSensor.calculateFlowRate(levelInlet);

  if (manualSamplingActive || autoController.isRunning()) {
    float ph = phSensor.readPH(); delay(200);
    float tss = tssSensor.readTSS(); delay(200);
    if (ph >= 0) { Serial.print("│ pH: "); Serial.print(ph, 2);
      Serial.println((ph >= PH_MIN && ph <= PH_MAX) ? " ✓ OK       │" : " ✗ OUT RANGE │");
    } else { Serial.println("│ pH: ERROR                             │"); }
    
    if (tss >= 0) { Serial.print("│ TSS: "); Serial.print(tss, 1); Serial.print(" mg/L ");
      Serial.println((tss <= TSS_MAX) ? "✓ OK   │" : " ✗ HIGH  │");
    } else { Serial.println("│ TSS: ERROR                            │"); }
  } else {
    Serial.println("│ pH: -- (Inactive)                     │");
    Serial.println("│ TSS: -- (Inactive)                    │");
  }

  if (levelInlet >= 0) { Serial.print("│ Inlet Level: "); Serial.print(levelInlet, 2);
    Serial.println(" m              │"); Serial.print("│ Inlet Flow: ");
    Serial.print(flowRate * 86400, 1);
    Serial.println(" m3/d            │");
  }
  else { Serial.println("│ Inlet Level/Flow: ERROR               │"); }

  unsigned long nextScraping = (SCRAPING_INTERVAL - (millis() - lastScrapingTime)) / 60000;
  Serial.print("│ Next Scraping: "); Serial.print(nextScraping);
  Serial.println(" min           │");
  Serial.println("└─────────────────────────────────────┘\n");

  Serial.println("┌─ Actuator Status ───────────────────┐");
  valveController.printCompactStatus();
  Serial.println("└─────────────────────────────────────┘\n");
  Serial.println("════════════════════════════════════════\n");
}