import minimalmodbus
import serial
import sys
import time

def print_menu():
    print("\n" + "="*50)
    print(" ?? TSS WIPER MODBUS TOOL (VSCode) ??")
    print("="*50)
    print("1. Tes Wiper (ID 1) - pakai angka 66")
    print("2. Ubah Slave ID dari 1 menjadi 2 (Permanen)")
    print("3. Tes Wiper (ID 2) - pakai angka 66")
    print("4. Tes Wiper ALTERNATIF (ID 2) - pakai angka 1")
    print("5. Cek Status Sensor (Tipe Elektroda & TSS)")
    print("0. Keluar")
    print("="*50)

def main():
    if len(sys.argv) < 2:
        print("Penggunaan: python wiper_tool.py <PORT_COM>")
        sys.exit(1)

    port = sys.argv[1]
    
    try:
        instrument = minimalmodbus.Instrument(port, 2)  # Default ke ID 2 sekarang
        instrument.serial.baudrate = 9600
        instrument.serial.bytesize = 8
        instrument.serial.parity   = serial.PARITY_NONE
        instrument.serial.stopbits = 1
        instrument.serial.timeout  = 1.0
        instrument.mode = minimalmodbus.MODE_RTU
        print(f"Berhasil membuka port {port}")
    except Exception as e:
        print(f"Gagal membuka port: {e}")
        sys.exit(1)

    while True:
        print_menu()
        pilihan = input("Pilih menu (0-5): ")

        if pilihan == '0':
            break
            
        elif pilihan == '1':
            instrument.address = 1
            try:
                instrument.write_register(20, 66, functioncode=6)
                print("? SUKSES! Command 66 ke ID 1.")
            except Exception as e:
                print(f"? ERROR: {e}")

        elif pilihan == '2':
            instrument.address = 1
            try:
                instrument.write_register(17, 2, functioncode=6)
                print("? SUKSES! ID diubah ke 2. Restart power sensor.")
            except Exception as e:
                print(f"? ERROR: {e}")
                    
        elif pilihan == '3':
            instrument.address = 2
            try:
                instrument.write_register(20, 66, functioncode=6)
                print("? SUKSES! Command 66 diterima ID 2.")
            except Exception as e:
                print(f"? ERROR: {e}")
                
        elif pilihan == '4':
            instrument.address = 2
            print("\nMengirim perintah alternatif (angka 1) ke ID 2...")
            try:
                instrument.write_register(20, 1, functioncode=6)
                print("? SUKSES! Command alternatif 1 diterima ID 2.")
            except Exception as e:
                print(f"? ERROR: {e}")
                
        elif pilihan == '5':
            instrument.address = 2
            print("\nMembaca status dari ID 2...")
            try:
                tipe = instrument.read_register(13, functioncode=3)
                print(f"Tipe Elektroda (Reg 13) : {tipe} (1 = Dengan Wiper, 0 = Tanpa Wiper)")
                
                # Baca TSS (Float 32 bit, reg 0)
                try:
                    tss_raw = instrument.read_float(0, functioncode=3, number_of_registers=2)
                    print(f"Data Turbidity (TSS)    : {tss_raw/100.0} mg/L")
                except Exception as e:
                    print(f"Gagal baca data TSS: {e}")
                    
            except Exception as e:
                print(f"? ERROR membaca status: {e}")
                
        time.sleep(1)

if __name__ == '__main__':
    main()
