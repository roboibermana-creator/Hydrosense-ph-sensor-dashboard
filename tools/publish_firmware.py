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
    h = {"apikey": KEY, "Content-Type": tipe, "x-upsert": "true", "Cache-Control": "no-cache"}
    # Kunci baru "sb_secret_..." tidak boleh dikirim sebagai Bearer; service_role lama (JWT) boleh.
    if not KEY.startswith("sb_secret_"):
        h["Authorization"] = f"Bearer {KEY}"
    return h


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
    if not KEY:
        sys.exit("Set SUPABASE_SERVICE_KEY dulu (service_role / secret key).")
    if not os.path.exists(BIN):
        sys.exit(f"{BIN} tidak ada. Jalankan: pio run -e esp32-s3-devkitc-1")
    versi = sys.argv[1] if len(sys.argv) > 1 else time.strftime("%Y%m%d-%H%M")
    data = open(BIN, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    nama = f"firmware-{versi}.bin"
    unggah(nama, data, "application/octet-stream")
    meta = {"file": nama, "sha256": sha, "size": len(data), "versi": versi}
    unggah("latest.json", json.dumps(meta).encode(), "application/json")
    print(f"OK: {nama} ({len(data)} byte)\nsha256 {sha}\nSekarang tekan 'Update firmware' di dashboard.")


if __name__ == "__main__":
    main()
