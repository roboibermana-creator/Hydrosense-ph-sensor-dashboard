#include <ModbusMaster.h>
#include <HardwareSerial.h>

// Definisikan pin yang digunakan untuk Serial2 (RX2, TX2)
// Pada kebanyakan board ESP32-S3, defaultnya adalah GPIO 16 (RX) dan 17 (TX)
#define RX_PIN 16
#define TX_PIN 15

// Definisikan pin untuk kontrol DE/RE pada MAX485
#define RE_DE_PIN 14

// Alamat Slave ID dari sensor pH (default adalah 1)
#define SENSOR_SLAVE_ID 1

// Alamat register untuk nilai pH
#define PH_REGISTER_ADDRESS 0x0006

// Buat instance ModbusMaster
ModbusMaster node;

// Gunakan Serial2 untuk komunikasi Modbus
HardwareSerial& modbusSerial = Serial2;

// Fungsi ini akan dipanggil sebelum transmisi Modbus
void preTransmission() {
  // Set pin DE/RE ke HIGH untuk mengaktifkan mode pengiriman (Transmit)
  digitalWrite(RE_DE_PIN, HIGH);
}

// Fungsi ini akan dipanggil setelah transmisi Modbus selesai
void postTransmission() {
  // Set pin DE/RE ke LOW untuk mengaktifkan mode penerimaan (Receive)
  digitalWrite(RE_DE_PIN, LOW);
}

void setup() {
  // Mulai serial monitor untuk debugging
  Serial.begin(115200);
  Serial.println("ESP32-S3 Modbus pH Sensor Reader");

  // Atur pin DE/RE sebagai OUTPUT dan set ke LOW (mode receive) secara default
  pinMode(RE_DE_PIN, OUTPUT);
  digitalWrite(RE_DE_PIN, LOW);

  // Mulai komunikasi Serial2 dengan parameter dari datasheet (9600, 8N1)
  // dan pin yang telah didefinisikan
  modbusSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

  // Mulai ModbusMaster dengan Slave ID sensor dan serial port yang digunakan
  node.begin(SENSOR_SLAVE_ID, modbusSerial);

  // Daftarkan fungsi callback untuk kontrol pin DE/RE
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
}

void loop() {
  uint8_t result;

  Serial.println("--------------------");
  Serial.print("Membaca data dari sensor pH, Slave ID: ");
  Serial.println(SENSOR_SLAVE_ID);

  // Kirim permintaan untuk membaca 1 holding register dari alamat PH_REGISTER_ADDRESS
  result = node.readHoldingRegisters(PH_REGISTER_ADDRESS, 1);

  // Cek apakah pembacaan berhasil
  if (result == node.ku8MBSuccess) {
    // Ambil data mentah dari buffer respons (data ada di posisi 0)
    uint16_t rawPH = node.getResponseBuffer(0);

    // Konversi data mentah ke nilai pH sebenarnya (dibagi 100.0)
    // Gunakan 100.0 untuk memastikan hasil pembagian adalah float
    float pH_value = rawPH / 100.0;

    Serial.print("Data mentah (raw): ");
    Serial.println(rawPH);
    Serial.print("Nilai pH: ");
    Serial.println(pH_value, 2); // Tampilkan 2 angka di belakang koma

  } else {
    // Jika gagal, cetak kode error untuk troubleshooting
    Serial.print("Gagal membaca data. Kode Error: 0x");
    Serial.println(result, HEX);
    Serial.println("Cek koneksi kabel A/B, Slave ID, dan catu daya.");
  }

  // Tunggu 2 detik sebelum pembacaan berikutnya
  delay(2000);
}