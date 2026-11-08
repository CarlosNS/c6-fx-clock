"""Builds the files in webflasher/ from the last PlatformIO build (run `pio run` first):

- marea.bin      full image (bootloader + partition table + app) flashed at offset 0 by the web
                 installer (manifest.json). Use it for a first install or to recover.
- marea-ota.bin  just the app: what the watches download to update, and what the web page uploads
                 (Avanzado).

Offsets come from partitions.csv, so changing the table needs no edits here.

Usage: python tools/build_webflasher.py   (the system Python, which has esptool)
"""
import csv
import os
import shutil
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
build = os.path.join(root, ".pio", "build", "s3amoled")
out_dir = os.path.join(root, "webflasher")
FULL, OTA = "marea.bin", "marea-ota.bin"


def offsets():
    """(otadata offset, first app offset) from partitions.csv."""
    with open(os.path.join(root, "partitions.csv")) as f:
        rows = [r for r in csv.reader(line for line in f if line.strip() and not line.lstrip().startswith("#"))]
    otadata = app = None
    for name, ptype, subtype, offset, *_ in rows:
        ptype, subtype = ptype.strip(), subtype.strip()
        if ptype == "data" and subtype == "ota":
            otadata = offset.strip()
        if ptype == "app" and subtype in ("ota_0", "factory") and app is None:
            app = offset.strip()
    if not app:
        sys.exit("No app partition in partitions.csv")
    return otadata, app


def main():
    if not os.path.exists(os.path.join(build, "firmware.bin")):
        sys.exit("No build found: run `pio run` first")
    otadata, app = offsets()
    full = os.path.join(out_dir, FULL)
    # The image header says DIO; the bootloader switches the flash to QIO itself
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge-bin", "-o", full,
           "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "32MB",
           "0x0", os.path.join(build, "bootloader.bin"),
           "0x8000", os.path.join(build, "partitions.bin")]
    if otadata:
        cmd += [otadata, os.path.join(build, "ota_data_initial.bin")]
    cmd += [app, os.path.join(build, "firmware.bin")]
    subprocess.run(cmd, check=True)
    ota = os.path.join(out_dir, OTA)
    shutil.copyfile(os.path.join(build, "firmware.bin"), ota)
    print(f"{full}: {os.path.getsize(full)} bytes (app at {app})")
    print(f"{ota}: {os.path.getsize(ota)} bytes")


if __name__ == "__main__":
    main()
