import os
import re
import threading

import pytest

from clocklib.ble import ClockLink
from clocklib.board import SerialSession

SERIAL_LOG = os.path.join(os.path.dirname(__file__), "..", ".ble_serial.log")


@pytest.fixture(scope="package", autouse=True)
def ble_mode(serial_port):
    """Bluetooth tests need the BLE boot mode: switch to it over the serial console and go back to
    the mode the clock was in afterwards (until the app exists the clock lives in Wi-Fi mode).
    The serial port stays open the whole time and its log is saved to tests/.ble_serial.log, so a
    failing Bluetooth test can be read against what the clock was doing."""
    s = SerialSession(serial_port)  # opening the port reboots the board
    s.read_until(r"Boot mode: \w+", 30)
    was = re.findall(r"Boot mode: (\w+)", s.text)[-1]
    if was != "ble":
        at = s.mark()
        s.send("mode ble")
        s.read_until(r"Boot mode: ble", 30, at)
    s.read_until(r"Advertising as", 30)
    stop = threading.Event()

    def pump():
        while not stop.is_set():
            s._read()
    reader = threading.Thread(target=pump, daemon=True)
    reader.start()
    yield
    stop.set()
    reader.join(30)  # it may be reopening the port after a reboot: let it finish first
    with open(SERIAL_LOG, "w", encoding="utf-8") as f:
        f.write(s.text)
    if was != "ble":
        at = s.mark()
        s.send(f"mode {was}")
        s.read_until(f"Boot mode: {was}", 30, at)
    s.close()


@pytest.fixture(scope="module")
def link(ble_name):
    """One connected link per test module. Phase 2 has no encryption yet; phase 3 pairs."""
    lk = ClockLink(ble_name).connect(pair=False)
    yield lk
    lk.close()
