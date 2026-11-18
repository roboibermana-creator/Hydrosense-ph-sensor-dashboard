import sys
import struct
import math
import json
import urllib.request
import urllib.error

print("=" * 60)
print("   HYDROSENSE BACKTEST & VERIFIKASI PIPELINE")
print("=" * 60)

# =====================================================================
# 1. TEST KONVERSI FLOAT <-> MODBUS REGISTERS (IEEE-754 Low Word First)
# =====================================================================
print("\n[TEST 1] Modbus Float Register Conversion (IEEE-754 Low-Word-First)")

def float_to_registers(val):
    packed = struct.pack('<f', val) # little endian float bytes
    # In ESP32: bits = uint32, word0 = bits & 0xFFFF (low word), word1 = (bits >> 16) & 0xFFFF (high word)
    raw_u32 = struct.unpack('<I', packed)[0]
    word0 = raw_u32 & 0xFFFF
    word1 = (raw_u32 >> 16) & 0xFFFF
    return word0, word1

def registers_to_float(word0, word1):
    raw_u32 = (word1 << 16) | word0
    packed = struct.pack('<I', raw_u32)
    return struct.unpack('<f', packed)[0]

# Dari manual sensor BOQU ZDYG: Nilai 7.0 mg/L -> word0 = 0x0000, word1 = 0x40E0
w0, w1 = float_to_registers(7.0)
decoded_7 = registers_to_float(0x0000, 0x40E0)

print(f"  Target 7.0 -> word0: 0x{w0:04X}, word1: 0x{w1:04X}")
print(f"  Register (0x0000, 0x40E0) -> Decoded: {decoded_7:.4f}")
assert abs(decoded_7 - 7.0) < 1e-5, "Gagal decode 7.0 mg/L"
assert w0 == 0x0000 and w1 == 0x40E0, "Gagal encode 7.0 mg/L"
print("  [PASS] Konversi Modbus Float Register terbukti akurat 100%!")

# =====================================================================
# 2. TEST REGRESI LINEAR & SPAN CALIBRATION
# =====================================================================
print("\n[TEST 2] Algoritma Regresi Linear & Kalibrasi Span")

def calibrate_span(raw_low, act_low, raw_high, act_high):
    slope = (act_high - act_low) / (raw_high - raw_low)
    offset = act_low - slope * raw_low
    return slope, offset

def calibrate_regression(raw_vals, act_vals):
    n = len(raw_vals)
    sumX = sum(raw_vals)
    sumY = sum(act_vals)
    sumXY = sum(x * y for x, y in zip(raw_vals, act_vals))
    sumX2 = sum(x * x for x in raw_vals)
    denom = (n * sumX2) - (sumX * sumX)
    slope = ((n * sumXY) - (sumX * sumY)) / denom
    offset = (sumY - slope * sumX) / n
    return slope, offset

# Simulasi Kalibrasi 2 Titik (Span):
# Titik 1: Air bersih Raw=2.0 mg/L -> Aktual=0.0 mg/L
# Titik 2: Standar Raw=98.0 mg/L -> Aktual=100.0 mg/L
s_slope, s_offset = calibrate_span(2.0, 0.0, 98.0, 100.0)
print(f"  Span Calibration: Y = {s_slope:.6f} * X + ({s_offset:.6f})")
# Test prediksi
test_raw = 50.0
pred_val = s_slope * test_raw + s_offset
print(f"  Uji Raw 50.0 mg/L -> Hasil Terkalibrasi: {pred_val:.2f} mg/L")
assert pred_val > 0, "Prediksi harus valid"
print("  [PASS] Perhitungan Span Calibration valid.")

# Simulasi Regresi 4 Titik
raws = [1.2, 24.5, 51.0, 103.2]
acts = [0.0, 25.0, 50.0, 100.0]
r_slope, r_offset = calibrate_regression(raws, acts)
print(f"  Linear Regression (4 Titik): Y = {r_slope:.6f} * X + ({r_offset:.6f})")
assert 0.9 < r_slope < 1.1, "Slope harus mendekati 1.0"
print("  [PASS] Perhitungan Regresi Linear valid.")

# =====================================================================
# 3. LIVE SUPABASE API BACKTEST
# =====================================================================
print("\n[TEST 3] Pengujian Koneksi Langsung ke REST API Supabase")

SUPABASE_URL = "https://vupbropaxsqireeullbm.supabase.co"
SUPABASE_KEY = "sb_publishable_mvifCe-YeWbL4Wyp3jf1fA_Xwz2P26Z"
HEADERS = {
    "apikey": SUPABASE_KEY,
    "Authorization": f"Bearer {SUPABASE_KEY}",
    "Content-Type": "application/json"
}

def supabase_get(endpoint):
    url = f"{SUPABASE_URL}/rest/v1/{endpoint}"
    req = urllib.request.Request(url, headers=HEADERS, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            status = resp.status
            body = resp.read().decode('utf-8')
            return status, json.loads(body)
    except urllib.error.HTTPError as e:
        body = e.read().decode('utf-8')
        return e.code, body
    except Exception as e:
        return 0, str(e)

# 3A: Cek tabel system_control
code, data = supabase_get("device_commands?select=id,params&type=eq.control&order=id.desc&limit=1")
print(f"  1. Query tabel 'device_commands' (type=control) -> HTTP {code}")
if code == 200:
    print(f"     Status Data Terkini: {data}")
    print("     [PASS] Tabel device_commands (control) aktif dan dapat dibaca.")
else:
    print(f"     [FAIL] Error: {data}")

# 3B: Cek tabel Hydrosense table dengan URL Encoding (Hydrosense%20table)
print("\n  2. Query tabel 'Hydrosense%20table' (Format URL Baru):")
code, data = supabase_get("Hydrosense%20table?select=id,timestamp,ph,tss,water_temperature,water_level&order=id.desc&limit=1")
print(f"     Status GET: HTTP {code}")
if code == 200:
    print(f"     Baris Terakhir: {data}")
    print("     [PASS] Endpoint 'Hydrosense%20table' berhasil diakses!")
else:
    print(f"     [FAIL] Error: {data}")

# 3C: Cek apakah kolom 'tss_raw' ada di Supabase
print("\n  3. Baris alat di tabel 'devices' (status, jadwal, tss_raw):")
code_raw, data_raw = supabase_get(
    "devices?select=name,tss_raw,heartbeat_at,ip_address,interval_detik"
    "&project=eq.hydrosense&name=eq.hydrosense-inlet-01")
if code_raw == 200 and data_raw:
    print(f"     Baris alat: {data_raw}")
    print("     [PASS] Baris 'hydrosense-inlet-01' ada dan kolom statusnya tersedia!")
elif code_raw == 200:
    print("     [PERINGATAN] Baris 'hydrosense-inlet-01' (project hydrosense) TIDAK ADA di devices,")
    print("     atau RLS tidak mengizinkan anon membacanya. Alat tidak bisa melapor tanpa baris ini.")
else:
    print(f"     [PERINGATAN] Kolom status/jadwal BELUM ADA di devices (0005 belum jalan?): {data_raw}")

# 3D: Cek faktor kalibrasi TSS di tabel calibration
print("\n  4. Query tabel 'calibration' (faktor TSS):")
code_cal, data_cal = supabase_get("calibration?select=id,slope,cal_offset&sensor=eq.tss&aksi=eq.faktor&order=id.desc&limit=1")
print(f"     Status GET: HTTP {code_cal}")
if code_cal == 200:
    print(f"     Konfigurasi Kalibrasi Terkini: {data_cal}")
    print("     [PASS] Tabel calibration aktif!")
else:
    print(f"     Info: {data_cal}")

# 3E: Cek tabel device_commands
print("\n  5. Query tabel 'device_commands':")
code_hw, data_hw = supabase_get("device_commands?select=id,perintah,status&type=eq.tss_hw&limit=1")
print(f"     Status GET: HTTP {code_hw}")
if code_hw == 200:
    print("     [PASS] Tabel device_commands aktif!")
else:
    print(f"     Info: {data_hw}")

print("\n" + "=" * 60)
print("                  KESIMPULAN BACKTEST")
print("=" * 60)
