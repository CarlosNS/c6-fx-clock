"""Builds the files in webflasher/ from the last PlatformIO build of each board (run `pio run -e <env>`
first). For every board that has a build:

- <full>  full image (bootloader + partition table + app) flashed at offset 0 by the web installer
          (manifest.json, one build per chip family). Use it for a first install or to recover.
- <ota>   just the app: what the clocks download to update, and what the clock's web page uploads
          (Avanzado).

  C6: c6-fx-clock.bin / c6-fx-clock-ota.bin (names kept: installed clocks and the tests use them)
  S3: s3-fx-clock.bin / s3-fx-clock-ota.bin

Offsets come from each board's partition table, so changing a table needs no edits here.

Usage: python tools/build_webflasher.py [--board c6lcd|s3amoled]   (default: every board built)
"""
import argparse
import csv
import os
import shutil
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out_dir = os.path.join(root, "webflasher")

BOARDS = {
    "c6lcd": {"chip": "esp32c6", "table": "partitions.csv", "flash": "8MB",
              "full": "c6-fx-clock.bin", "ota": "c6-fx-clock-ota.bin"},
    "s3amoled": {"chip": "esp32s3", "table": "partitions_s3.csv", "flash": "32MB",
                 "full": "s3-fx-clock.bin", "ota": "s3-fx-clock-ota.bin"},
}


def offsets(table):
    """(otadata offset, first app offset) from a partition table."""
    with open(os.path.join(root, table)) as f:
        rows = [r for r in csv.reader(line for line in f if line.strip() and not line.lstrip().startswith("#"))]
    otadata = app = None
    for name, ptype, subtype, offset, *_ in rows:
        ptype, subtype = ptype.strip(), subtype.strip()
        if ptype == "data" and subtype == "ota":
            otadata = offset.strip()
        if ptype == "app" and subtype in ("ota_0", "factory") and app is None:
            app = offset.strip()
    if not app:
        sys.exit(f"No app partition in {table}")
    return otadata, app


def build_board(env):
    b = BOARDS[env]
    build = os.path.join(root, ".pio", "build", env)
    otadata, app = offsets(b["table"])
    full = os.path.join(out_dir, b["full"])
    # The image header says DIO; the bootloader switches the flash to QIO itself where configured
    cmd = [sys.executable, "-m", "esptool", "--chip", b["chip"], "merge-bin", "-o", full,
           "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", b["flash"],
           "0x0", os.path.join(build, "bootloader.bin"),
           "0x8000", os.path.join(build, "partitions.bin")]
    if otadata:
        cmd += [otadata, os.path.join(build, "ota_data_initial.bin")]
    cmd += [app, os.path.join(build, "firmware.bin")]
    subprocess.run(cmd, check=True)
    ota = os.path.join(out_dir, b["ota"])
    shutil.copyfile(os.path.join(build, "firmware.bin"), ota)
    print(f"{full}: {os.path.getsize(full)} bytes (app at {app})")
    print(f"{ota}: {os.path.getsize(ota)} bytes")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", choices=sorted(BOARDS))
    a = ap.parse_args()
    envs = [a.board] if a.board else [e for e in BOARDS if os.path.exists(os.path.join(root, ".pio", "build", e, "firmware.bin"))]
    if not envs:
        sys.exit("No build found: run `pio run -e c6lcd` (or s3amoled) first")
    for env in envs:
        build_board(env)


if __name__ == "__main__":
    main()
