#!/usr/bin/env python3
"""Unggah firmware ke bucket Supabase "firmware" untuk update lewat dashboard.

Pakai (setelah `pio run -e esp32-s3-devkitc-1`):
    set SUPABASE_SERVICE_KEY=<service_role (eyJ...) atau secret (sb_secret_...) dari Project Settings > API Keys>
    python tools/publish_firmware.py [versi]

- Mengunggah .pio/build/esp32-s3-devkitc-1/firmware.bin sebagai firmware-<versi>.bin
- Menulis latest.json {file, sha256, size, versi} yang dibaca tombol "Update firmware"
- Kunci service_role JANGAN dimasukkan ke repo/dashboard; hanya lewat variabel lingkungan.
"""
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request

URL = os.environ.get("SUPABASE_URL", "https://vupbropaxsqireeullbm.supabase.co")
KEY = os.environ.get("SUPABASE_SERVICE_KEY")
BIN = os.path.join(".pio", "build", "esp32-s3-devkitc-1", "firmware.bin")


def header_auth(tipe):
    # Storage API mewajibkan header Authorization. Untuk kunci "sb_secret_..." isinya harus sama
    # dengan apikey; untuk service_role lama (JWT) juga dipakai sebagai Bearer.
    return {
        "apikey": KEY,
        "Authorization": f"Bearer {KEY}",
        "Content-Type": tipe,
        "x-upsert": "true",
        "Cache-Control": "no-cache",
    }


def unggah(nama, data, tipe):
    req = urllib.request.Request(
        f"{URL}/storage/v1/object/firmware/{nama}",
        data=data,
        method="POST",
        headers=header_auth(tipe),
    )
    try:
        urllib.request.urlopen(req, timeout=120).read()
    except urllib.error.HTTPError as e:
        sys.exit(f"Gagal unggah {nama}: HTTP {e.code} {e.read().decode()[:200]}")


def main():
    lokal = "--lokal" in sys.argv
    args = [x for x in sys.argv[1:] if x != "--lokal"]
    if not KEY and not lokal:
        sys.exit("Set SUPABASE_SERVICE_KEY dulu (service_role), atau pakai --lokal (tanpa kunci).")
    if not os.path.exists(BIN):
        sys.exit(f"{BIN} tidak ada. Jalankan: pio run -e esp32-s3-devkitc-1")
    versi = args[0] if args else time.strftime("%Y%m%d-%H%M")
    data = open(BIN, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    nama = f"firmware-{versi}.bin"
    meta = {"file": nama, "sha256": sha, "size": len(data), "versi": versi}
    if lokal:
        # Cadangan tanpa kunci: siapkan 2 file di folder firmware_unggah/ untuk diunggah MANUAL
        # lewat dashboard Supabase > Storage > bucket "firmware" > Upload file.
        os.makedirs("firmware_unggah", exist_ok=True)
        open(os.path.join("firmware_unggah", nama), "wb").write(data)
        open(os.path.join("firmware_unggah", "latest.json"), "w").write(json.dumps(meta))
        print(f"Siap di folder firmware_unggah/: {nama} dan latest.json (sha256 {sha}).")
        print("Unggah KEDUANYA manual ke bucket firmware, lalu tekan CEK di dashboard.")
        return
    unggah(nama, data, "application/octet-stream")
    unggah("latest.json", json.dumps(meta).encode(), "application/json")
    print(f"OK: {nama} ({len(data)} byte)
sha256 {sha}
Sekarang tekan 'Update firmware' di dashboard.")


if __name__ == "__main__":
    main()
