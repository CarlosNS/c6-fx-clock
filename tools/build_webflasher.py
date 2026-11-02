"""Builds the files in webflasher/ from the last PlatformIO build (run `pio run` first).

- c6-fx-clock.bin      full image (bootloader + partition table + app) flashed at offset 0 by the
                       web installer (manifest.json). Use it for a first install or to recover.
- c6-fx-clock-ota.bin  just the app, for updating over Wi-Fi from the clock's web page (Avanzado).

The app offset is read from partitions.csv, so changing the table needs no edits here.

Usage: python tools/build_webflasher.py
"""
import csv
import os
import shutil
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
build = os.path.join(root, ".pio", "build", "c6lcd")
out_dir = os.path.join(root, "webflasher")


def first_app_offset():
    with open(os.path.join(root, "partitions.csv")) as f:
        rows = [r for r in csv.reader(line for line in f if line.strip() and not line.lstrip().startswith("#"))]
    for name, ptype, subtype, offset, *_ in rows:
        if ptype.strip() == "app" and subtype.strip() in ("ota_0", "factory"):
            return offset.strip()
    sys.exit("No app partition in partitions.csv")


app_off = first_app_offset()
full = os.path.join(out_dir, "c6-fx-clock.bin")
cmd = [sys.executable, "-m", "esptool", "--chip", "esp32c6", "merge-bin", "-o", full,
       "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "8MB",
       "0x0", os.path.join(build, "bootloader.bin"),
       "0x8000", os.path.join(build, "partitions.bin"),
       "0xf000", os.path.join(build, "ota_data_initial.bin"),
       app_off, os.path.join(build, "firmware.bin")]
subprocess.run(cmd, check=True)
ota = os.path.join(out_dir, "c6-fx-clock-ota.bin")
shutil.copyfile(os.path.join(build, "firmware.bin"), ota)
print(f"{full}: {os.path.getsize(full)} bytes (app at {app_off})")
print(f"{ota}: {os.path.getsize(ota)} bytes")
