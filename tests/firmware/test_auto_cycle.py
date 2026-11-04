"""The automatic face rotation skips the faces switched off in the app (setting `fxoff`, a bitmask).
Driven over the serial console; takes about 70 s (the clock changes face every 30 s).
Run with:  pytest tests/firmware/test_auto_cycle.py
"""
import time

import pytest

from clocklib import fx_count
from clocklib.board import SerialSession

pytestmark = [pytest.mark.phase1, pytest.mark.slow]


def test_auto_cycle_skips_switched_off_faces(serial_port):
    s = SerialSession(serial_port)
    s.read_until(r"Face: ", 30)
    time.sleep(1)
    before = s.status(timeout=10, reading=False)
    try:
        keep = {2, 5}  # only these two take part in the rotation
        mask = ((1 << fx_count()) - 1) & ~sum(1 << i for i in keep)
        s.send(f"fxoff {mask}")
        s.send("fx 2")
        s.send("auto 1")
        time.sleep(1)
        assert s.status(reading=False)["fxoff"] == mask
        seen = []
        end = time.monotonic() + 70
        while time.monotonic() < end:
            seen.append(s.status(reading=False)["fx"])
            time.sleep(5)
        assert set(seen) <= keep, seen
        assert set(seen) == keep, f"it did not visit both faces: {seen}"
    finally:
        s.send("auto 0")
        s.send("fxoff 0")
        s.send(f"fx {before['fx']}")
        time.sleep(1)
        s.close()
