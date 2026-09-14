#ifndef CONFIG_H
#define CONFIG_H

// ========== WiFi Configuration ==========
#define WIFI_SSID "ORBIT_STARGATE" 
#define WIFI_PASSWORD "Aspire1@3" 

// #define WIFI_SSID "SPRM-CORP" 
// #define WIFI_PASSWORD "11223344"

// ========== Firebase Configuration ==========
#define FIREBASE_HOST "hydrosense-iot-ptspr-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "1B0xG4NOAkQqaIDlm3PY5SBRXe9NXq25VOAbQBFZ"

// ========== Firebase Paths ==========
#define FIREBASE_DEVICE_ID "/hydrosense_device_1"
#define FIREBASE_LOG_QUEUE_PATH FIREBASE_DEVICE_ID "/log_queue"
#define FIREBASE_LATEST_DATA_PATH FIREBASE_DEVICE_ID "/latest_data"
#define FIREBASE_CONTROL_REQUEST_PATH FIREBASE_DEVICE_ID "/control_requests"
#define FIREBASE_STREAM_TIMEOUT_MS 10000 

// ========== Pin Configuration ==========
#define RX_PIN 16 // Modbus RS485 RX
#define TX_PIN 15 // Modbus RS485 TX
#define RE_DE_PIN 14 // Modbus RS485 RE/DE Control

#define PUMP_PIN 21 // Relay Pompa Air (Active LOW)

// Pin untuk Motorized Valve 1 (Drain - Relay On-Board Shield, Active HIGH)
#define VALVE1_OPEN_PIN 48
#define VALVE1_CLOSE_PIN 47

// Pin untuk Motorized Valve 2 (Tawas - Relay Eksternal, Active LOW)
#define VALVE2_OPEN_PIN 19
#define VALVE2_CLOSE_PIN 20

// Pin Analog untuk Sensor Level Radar 4-20mA (Inlet Flow)
#define FLOW_LEVEL_PIN 1 // GPIO36 = ADC1_CH0

// ========== Modbus Configuration ==========
#define PH_SLAVE_ID 1
#define PH_REGISTER_ADDRESS 0x0006
#define TSS_SLAVE_ID 2

// ========== TSS Sensor Registers ==========
#define REG_TURBIDITY 0
#define REG_INTERNAL_TEMP 2
#define REG_ELECTRODE_TYPE 13
#define REG_MANUAL_SCRAPING 20
#define REG_AUTO_SCRAPING_INTERVAL 21

// ========== Timing Configuration (milliseconds) ==========
#define PUMP_FILL_TIME 18000       
#define SAMPLING_DURATION 300000  
#define DRAIN_TIME 15000           
#define SCRAPING_INTERVAL 3600000 
#define FIREBASE_UPDATE_INTERVAL 5000 

// ========== Valve 2 Dosing Timing (milliseconds) ==========
#define VALVE2_START_DELAY_MS 2000    
#define VALVE2_FULL_OPEN_TIME_MS 10000 

// ========== Flow Configuration ==========
#define INLET_CHANNEL_MAX_H_M 1.0   
#define RADAR_SENSOR_RANGE_M 0.4064 

#define CONVERTER_V_AT_4MA  0.66    
#define CONVERTER_V_AT_20MA 3.3     

// Konstanta Kalibrasi Flow Rate (Q = k * W * L^2)
#define FLOW_CALIBRATION_K 0.5      
#define NOMINAL_FLOW_RATE 0.1       

// ========== Baku Mutu KLH ==========
#define PH_MIN 6.5
#define PH_MAX 8.5
#define TSS_MAX 100.0

// (Variable Pipa untuk Flow Sensor dipindah ke Flow_Sensor.h atau defaultnya)
#ifndef PIPE_DIAMETER_M
  #define PIPE_DIAMETER_M 0.4064f
#endif

#endif // CONFIG_H