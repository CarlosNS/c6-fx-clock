"""Helpers shared by the tests: talking to the clock over serial, Wi-Fi and Bluetooth."""
import os
import re

_FX_H = os.path.join(os.path.dirname(__file__), "..", "..", "src", "fx.h")
_FX_COMMON = os.path.join(os.path.dirname(__file__), "..", "..", "src", "fx_common.c")


def fx_count():
    """Number of faces in the firmware being tested (FX_COUNT in src/fx.h): grows with every new face."""
    with open(_FX_H, encoding="utf-8") as f:
        return int(re.search(r"#define FX_COUNT (\d+)", f.read()).group(1))


def fx_retired():
    """Numbers of the retired faces: still in the catalogue (numbers never shift) but never shown."""
    with open(_FX_COMMON, encoding="utf-8") as f:
        src = f.read()
    body = src[src.index("const fx_t fx_list[FX_COUNT]"):]
    body = body[:body.index("\n};")]
    entries = re.findall(r"^    \{\.name = .*$", body, re.M)
    return {i for i, e in enumerate(entries) if ".retired = true" in e}


def fx_live():
    """Faces that really exist (catalogue entries that are not retired)."""
    return [i for i in range(fx_count()) if i not in fx_retired()]
