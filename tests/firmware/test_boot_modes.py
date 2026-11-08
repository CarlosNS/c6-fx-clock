"""Phase 1: boot modes (docs/PLAN-bluetooth.md, section 2), driven over the serial console.

The tests run in order: the normal boot, UPDATE (returns by itself) and back. Wi-Fi and Bluetooth run
together in the normal modes ("ble" and "wifi" are the same). The module leaves the watch in "wifi".
Run with:  pytest -m phase1
"""
import re

import pytest

from clocklib import fx_count
from clocklib.board import SerialSession, http_json, wait_http

pytestmark = pytest.mark.phase1

PANIC = re.compile(r"abort\(\)|Guru Meditation|Backtrace:")


@pytest.fixture(scope="module")
def console(serial_port, host):
    s = SerialSession(serial_port)
    s.read_until(r"Boot mode: (\w+)", 20)
    yield s
    # Whatever happened, leave the clock as it was: in Wi-Fi mode
    try:
        if not re.findall(r"Boot mode: (\w+)", s.text)[-1] == "wifi":
            s.send("mode wifi")
            s.read_until(r"Boot mode: wifi", 30, s.mark())
    finally:
        s.close()


def switch(console, mode, timeout=30):
    """Sends "mode <mode>" and returns the log of the boot that follows."""
    at = console.mark()
    console.send(f"mode {mode}")
    console.read_until(f"Switching to {mode} mode", 10, at)
    start = console.read_until(r"Boot mode: \w+", timeout, at).start()
    console.read_until(r"Free heap after start: \d+|Update mode", timeout, start)
    return console.text[start:]


def free_heap(log):
    return int(re.search(r"Free heap after start: (\d+)", log).group(1))


def test_boots_cleanly(console):
    assert not PANIC.search(console.text)
    assert re.search(r"Boot mode: (ble|wifi)", console.text)


def test_console_works_without_radios(console):
    at = console.mark()
    console.send("bri 33")
    console.read_until(r"console: > bri 33", 5, at)
    console.send("bri 80")
    console.read_until(r"console: > bri 80", 5, at)


def test_update_mode_is_one_shot(console):
    at = console.mark()
    console.send("mode update")
    console.read_until(r"Boot mode: update", 30, at)
    console.read_until(r"single buffering", 10, at)
    # Without a manifest ("mode update" by hand) it records bad_manifest...
    done = console.read_until(r"update: Result: bad_manifest", 15, at)
    # ... and it comes back by itself, without any command
    after = console.read_until(r"Boot mode: (\w+)", 30, done.end())
    assert after.group(1) == "ble"
    assert not PANIC.search(console.text[at:])


def test_update_mode_cannot_trap_the_clock(console):
    """A reset in the middle of UPDATE (here: reopening the port) must boot the normal mode."""
    at = console.mark()
    console.send("mode update")
    console.read_until(r"Boot mode: update", 30, at)
    console.reset()  # reopening the port resets the board mid-update
    m = console.read_until(r"Boot mode: (\w+)", 30, console.mark())
    assert m.group(1) == "ble"


def test_back_to_wifi_keeps_the_web(console, host):
    log = switch(console, "wifi")
    assert log.startswith("Boot mode: wifi")
    st = wait_http(host, 180)
    assert st["mode"] == "wifi"
    assert len(http_json(host, "/faces")["faces"]) == fx_count()
