"""
Tool test langsung ke sensor TSS RD-SSBWT-01 lewat USB-RS485,
BYPASS ESP32/firmware sepenuhnya -- buat isolasi apakah masalah wiper
ada di sensor/hardware/power, atau di firmware ESP32.

Jalankan: python test_wiper.py
"""

import minimalmodbus
import serial
import time

# =========================
# KONFIGURASI -- SESUAIKAN INI
# =========================

# Ganti sesuai port USB-RS485 kamu (cek Device Manager kalau ragu)
PORT = "COM6"
# TSS_SLAVE_ID yang sudah terverifikasi jalan di firmware ESP32 (bukan
# default manual = 1)
SLAVE_ID = 2
BAUDRATE = 9600

# Register sesuai manual RD-SSBWT-01 / BOQU family
REG_TSS = 2          # function 03, quantity 2 -> float TSS
REG_BRUSHING_TIME = 11
REG_WIPER = 20       # function 06, value 66 -> manual brush ON
WIPER_VALUE = 66
REG_AUTO_INTERVAL = 21  # function 06, menit


def make_instrument(port, slave_id, baudrate):
    instrument = minimalmodbus.Instrument(port, slave_id)
    instrument.serial.baudrate = baudrate
    instrument.serial.bytesize = 8
    instrument.serial.parity = serial.PARITY_NONE
    instrument.serial.stopbits = 1
    instrument.serial.timeout = 1
    instrument.mode = minimalmodbus.MODE_RTU
    instrument.clear_buffers_before_each_transaction = True
    return instrument


def scan_slave(port, baudrate):
    print("\n>>> Scanning slave ID 1-10 ...")
    found = []
    for sid in range(1, 11):
        instrument = make_instrument(port, sid, baudrate)
        try:
            # Coba baca TSS (2 register) sebagai probe
            regs = instrument.read_registers(REG_TSS, 2, functioncode=3)
            print(f"  ID {sid:2d} : RESPON  -> raw registers = {regs}")
            found.append(sid)
        except Exception as e:
            print(f"  ID {sid:2d} : (timeout/no response) {type(e).__name__}")
        finally:
            instrument.serial.close()
        time.sleep(0.1)
    if found:
        print(f"\nSlave ID yang merespon: {found}")
    else:
        print("\nTidak ada slave yang merespon sama sekali.")
        print("Cek: kabel A/B (mungkin ketuker), baudrate, power sensor, wiring RS485.")
    return found


def read_tss(port, slave_id, baudrate):
    instrument = make_instrument(port, slave_id, baudrate)
    try:
        print(f"\n>>> Membaca TSS dari slave ID {slave_id} ...")
        value = instrument.read_float(
            REG_TSS, functioncode=3, number_of_registers=2)
        print(f"TSS = {value:.2f} mg/L")
    except Exception as e:
        print(f"ERROR baca TSS: {e}")
    finally:
        instrument.serial.close()


def read_register(port, slave_id, baudrate):
    try:
        reg = int(input("Register address (desimal): ").strip())
        count = int(
            input("Jumlah register yang dibaca (default 1): ").strip() or "1")
    except ValueError:
        print("Input tidak valid.")
        return
    instrument = make_instrument(port, slave_id, baudrate)
    try:
        regs = instrument.read_registers(reg, count, functioncode=3)
        print(f"Register {reg} ({count}x) = {regs}")
    except Exception as e:
        print(f"ERROR baca register: {e}")
    finally:
        instrument.serial.close()


def test_wiper(port, slave_id, baudrate):
    instrument = make_instrument(port, slave_id, baudrate)
    try:
        print(
            f"\n>>> Mengirim command WIPER ke slave ID {slave_id} (register {REG_WIPER} = {WIPER_VALUE}) ...")
        instrument.write_register(REG_WIPER, WIPER_VALUE, functioncode=6)
        print("Command berhasil dikirim (ACK diterima).")
        print("PERHATIKAN FISIK PROBE SEKARANG -- dengar/lihat apakah motor benar-benar berputar.")

        # Poll brushing time sebelum/sesudah untuk lihat apakah state internal
        # berubah
        try:
            before = instrument.read_register(
                REG_BRUSHING_TIME, functioncode=3)
            print(f"Brushing Time (sebelum tunggu) = {before}")
        except Exception:
            pass

        for i in range(15):
            time.sleep(1)
            print(".", end="", flush=True)
        print()

        try:
            after = instrument.read_register(REG_BRUSHING_TIME, functioncode=3)
            print(f"Brushing Time (setelah tunggu) = {after}")
        except Exception:
            pass

    except Exception as e:
        print(f"ERROR: {e}")
    finally:
        instrument.serial.close()


def set_auto_wiper(port, slave_id, baudrate):
    try:
        minutes = int(input("Interval auto-wiper (menit): ").strip())
    except ValueError:
        print("Input tidak valid.")
        return
    instrument = make_instrument(port, slave_id, baudrate)
    try:
        instrument.write_register(REG_AUTO_INTERVAL, minutes, functioncode=6)
        print(f"Auto-wiper interval diset ke {minutes} menit.")
    except Exception as e:
        print(f"ERROR: {e}")
    finally:
        instrument.serial.close()


def main():
    port = PORT
    slave_id = SLAVE_ID
    baudrate = BAUDRATE

    while True:
        print("\n================================")
        print("  TEST SENSOR TSS RD-SSBWT-01")
        print("================================")
        print(f"Port={port}  SlaveID={slave_id}  Baud={baudrate}")
        print("[1] Scan Slave")
        print("[2] Read TSS")
        print("[3] Read Register (manual)")
        print("[4] TEST WIPER")
        print("[5] Set Auto Wiper Interval")
        print("[6] Ganti Port/SlaveID/Baudrate")
        print("[0] Exit")
        choice = input("Pilih menu: ").strip()

        if choice == "1":
            scan_slave(port, baudrate)
        elif choice == "2":
            read_tss(port, slave_id, baudrate)
        elif choice == "3":
            read_register(port, slave_id, baudrate)
        elif choice == "4":
            test_wiper(port, slave_id, baudrate)
        elif choice == "5":
            set_auto_wiper(port, slave_id, baudrate)
        elif choice == "6":
            port = input(f"Port [{port}]: ").strip() or port
            sid_in = input(f"Slave ID [{slave_id}]: ").strip()
            if sid_in:
                slave_id = int(sid_in)
            baud_in = input(f"Baudrate [{baudrate}]: ").strip()
            if baud_in:
                baudrate = int(baud_in)
        elif choice == "0":
            print("Bye.")
            break
        else:
            print("Pilihan tidak dikenal.")


if __name__ == "__main__":
    main()
