"""Phase 3: Bluetooth security (docs/PLAN-bluetooth.md, 3.3).

Pairing uses the real passkey: the clock shows it on the screen and writes it to its serial log,
where the test reads it and types it into Windows (clocklib.ble.ClockLink.pair_with_pin). What a
person must still check by hand (the code on the screen, the phone's dialog) is in tests/MANUAL.md.
Each test starts by making both sides forget each other.
"""
import time

import pytest

from clocklib.ble import ClockLink

from .conftest import forget_everywhere, pair

pytestmark = pytest.mark.phase3


@pytest.fixture
def fresh(ble_name, ble_serial):
    """An unpaired, connected link (both sides have forgotten each other)."""
    lk = ClockLink(ble_name).connect()
    forget_everywhere(lk, ble_serial)
    lk = ClockLink(ble_name).connect()  # Windows dropped it from its cache: found again by scanning
    yield lk
    lk.close()


def paired_status(lk, serial):
    assert pair(lk, serial) in ("PAIRED", "ALREADY_PAIRED")
    return lk.request("get status", "status", timeout=10)


def test_firmware_version_is_public(fresh):
    assert fresh.read_fw_revision().count(".") == 2


def test_commands_without_pairing_are_ignored(fresh, ble_serial):
    fresh.send("bri 7", response=False)  # an unpaired write: the clock must drop it
    time.sleep(1)
    st = paired_status(fresh, ble_serial)
    assert st["bri"] != 7, "a command from an unpaired device was applied"


def test_nothing_is_sent_to_an_unpaired_device(fresh):
    fresh.drain()
    fresh.send("get status", response=False)  # asks for data without pairing
    time.sleep(2)
    assert fresh.messages.empty(), "the clock sent data over an unpaired link"


def test_pairing_with_the_screen_code(fresh, ble_serial):
    at = ble_serial.mark()
    assert pair(fresh, ble_serial) == "PAIRED"
    ble_serial.wait_for(r"Encryption on, paired", 10, at)
    assert fresh.request("get info", "info")["mode"] == "ble"


def test_pairing_code_changes_every_time(ble_name, ble_serial):
    codes = []
    for _ in range(2):
        lk = ClockLink(ble_name).connect()
        forget_everywhere(lk, ble_serial)
        lk = ClockLink(ble_name).connect()
        at = ble_serial.mark()
        pair(lk, ble_serial)
        codes.append(ble_serial.wait_for(r"passkey (\d{6})", 5, at).group(1))
        lk.close()
    assert codes[0] != codes[1]


def test_wrong_code_is_rejected(fresh, ble_serial):
    at = ble_serial.mark()
    status = pair(fresh, ble_serial, wrong_code="000000")
    assert status != "PAIRED"
    assert "Encryption on, paired" not in ble_serial.text[at:]


def test_pairing_survives_a_reboot(ble_name, ble_serial, fresh):
    assert pair(fresh, ble_serial) == "PAIRED"
    addr = fresh.address
    fresh.close()
    at = ble_serial.mark()
    ble_serial.send("mode ble")  # restarts the clock in the same mode
    ble_serial.wait_for(r"Advertising as", 30, at)
    lk = ClockLink(ble_name).connect(addr, retries=3)  # an app retries the same way
    try:
        assert lk.request("get status", "status", timeout=10)["mode"] == "ble"
        assert "passkey" not in ble_serial.text[at:], "the bond was lost: it asked for a new code"
    finally:
        lk.close()


def test_unpair_command_forgets_the_phone(ble_name, ble_serial, fresh):
    assert pair(fresh, ble_serial) == "PAIRED"
    fresh.send("unpair")
    fresh.wait_for("ack", match=lambda m: m.get("cmd") == "unpair")
    at = ble_serial.mark()
    ble_serial.wait_for("forgotten", 5, at)
    fresh.unpair()  # the phone side forgets too, as the app will after "Olvidar reloj"
    lk = ClockLink(ble_name).connect()
    try:
        at = ble_serial.mark()
        assert pair(lk, ble_serial) == "PAIRED"
        ble_serial.wait_for(r"passkey \d{6}", 5, at)  # a new code was needed
    finally:
        lk.close()
