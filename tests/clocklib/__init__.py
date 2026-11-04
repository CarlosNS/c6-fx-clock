"""Helpers shared by the tests: talking to the clock over serial, Wi-Fi and Bluetooth."""
import os
import re

_FX_H = os.path.join(os.path.dirname(__file__), "..", "..", "src", "fx.h")


def fx_count():
    """Number of faces in the firmware being tested (FX_COUNT in src/fx.h): grows with every new face."""
    with open(_FX_H, encoding="utf-8") as f:
        return int(re.search(r"#define FX_COUNT (\d+)", f.read()).group(1))
