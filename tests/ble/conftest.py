import contextlib
import os
import re
import threading

import pytest

from clocklib.ble import ClockLink
from clocklib.board import SerialSession

SERIAL_LOG = os.path.join(os.path.dirname(__file__), "..", ".ble_serial.log")


@contextlib.contextmanager
def ble_mode(serial_port):
    """The clock in BLE mode for a block of tests: switched over the serial console and put back in
    the mode it was in afterwards (until the app is in daily use the clock lives in Wi-Fi mode).
    The serial port stays open the whole time: its log gives the pairing passkey and is saved to
    tests/.ble_serial.log, so a failing Bluetooth test can be read against what the clock did.
    Each test package that needs it defines its own package-scoped fixture with it (a package
    fixture imported into another package would only end with the whole session)."""
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
    try:
        yield s
    finally:
        stop.set()
        reader.join(30)  # it may be reopening the port after a reboot: let it finish first
        with open(SERIAL_LOG, "w", encoding="utf-8") as f:
            f.write(s.text)
        if was != "ble":
            at = s.mark()
            s.send(f"mode {was}")
            s.read_until(f"Boot mode: {was}", 30, at)
        s.close()


@pytest.fixture(scope="package", autouse=True)
def ble_serial(serial_port):
    with ble_mode(serial_port) as s:
        yield s


def pair(link, serial, wrong_code=None):
    """Pairs like a phone does: the clock shows a passkey (also written to its log), and it is typed
    in. With wrong_code, that code is typed instead. Returns the Windows pairing status."""
    at = serial.mark()

    def code():
        shown = serial.wait_for(r"passkey (\d{6})", 20, at).group(1)
        return wrong_code or shown
    return link.pair_with_pin(code)


def forget_everywhere(link, serial):
    """Unpaired on both sides: the clock forgets every phone, Windows forgets the clock."""
    at = serial.mark()
    serial.send("unpair")
    serial.wait_for("forgotten", 5, at)
    link.unpair()


@pytest.fixture(scope="module")
def link(ble_name, ble_serial):
    """One connected, paired link per test module. Pairs with the on-screen passkey unless Windows
    and the clock already share a bond."""
    lk = ClockLink(ble_name).connect()
    status = pair(lk, ble_serial)
    assert status in ("PAIRED", "ALREADY_PAIRED"), f"pairing failed: {status}"
    yield lk
    lk.close()
