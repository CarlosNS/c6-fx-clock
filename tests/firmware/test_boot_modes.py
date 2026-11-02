"""Phase 1: boot modes. Reads the serial boot log, so each test RESETS the board.

Expected log lines (to be added by phase 1):
  I (...) main: Boot mode: ble | wifi | update
  I (...) main: Free heap after start: <n>
"""
import re

import pytest

from clocklib.board import boot_log

pytestmark = pytest.mark.phase1


def mode_of(log):
    m = re.search(r"Boot mode: (\w+)", log)
    return m.group(1) if m else None


def heap_of(log):
    m = re.search(r"Free heap after start: (\d+)", log)
    return int(m.group(1)) if m else None


def test_boots_cleanly(serial_port):
    log = boot_log(serial_port)
    assert "abort()" not in log and "Guru Meditation" not in log
    assert mode_of(log) in ("ble", "wifi")


def test_ble_mode_does_not_start_wifi(serial_port):
    log = boot_log(serial_port)
    if mode_of(log) != "ble":
        pytest.skip("board is not in BLE mode")
    assert "wifi:mode" not in log and "httpd" not in log
    assert heap_of(log) >= 30000, "phase 0 gate: >= 30 KB free in BLE mode"


def test_wifi_mode_keeps_the_web(serial_port):
    log = boot_log(serial_port)
    if mode_of(log) != "wifi":
        pytest.skip("board is not in Wi-Fi mode")
    assert "Connected to" in log or "AP '" in log
