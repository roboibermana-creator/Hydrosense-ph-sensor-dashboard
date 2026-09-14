#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
// !! PLACEHOLDER !! — semua nilai di bawah ini masih contoh/dummy.
// WAJIB dicek & disesuaikan dengan datasheet sensor TSS dan wiring
// aslinya SEBELUM upload ke board yang terhubung ke sensor asli.
// =====================================================================

// --- Serial2 (RS485) UART pins ---
#define RX_PIN 16 // Modbus RS485 RX
#define TX_PIN 15 // Modbus RS485 TX

// --- RS485 Direction Control Pin ---
#define RE_DE_PIN 14

#define MODBUS_BAUD_RATE 9600 // TODO: cek baud rate di datasheet sensor / hasil test Modbus Poll

// --- Modbus Slave ID ---
#define TSS_SLAVE_ID 2 // TODO: sesuaikan slave ID sensor TSS (beda dari sensor pH di bus yang sama)

// --- Modbus Holding Register Addresses ---
// TODO: ganti semua alamat register di bawah sesuai datasheet sensor TSS.
#define REG_TURBIDITY 0x0000               // TODO
#define REG_INTERNAL_TEMP 0x0002           // TODO
#define REG_ELECTRODE_TYPE 0x0007          // TODO
#define REG_AUTO_SCRAPING_INTERVAL 0x0064  // TODO
#define REG_MANUAL_SCRAPING 0x0065         // TODO

#endif // CONFIG_H
