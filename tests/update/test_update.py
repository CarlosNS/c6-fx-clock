"""Phase 6: updates over the internet (docs/PLAN-bluetooth.md, 3.4).

Flow per test: over Bluetooth send "update apply <manifest url>" -> the clock reboots into UPDATE
mode, tries, stores the result and reboots into BLE mode; the serial log tells when, then
"get info".upd gives the result. The manifests and images come from a server on this PC
(update_server.py, http:// is accepted only for LAN addresses).

Needs: the clock's Wi-Fi network stored, and the PC reachable from the clock on port 8770 (Windows
firewall: inbound rule for python.exe).
"""
import os
import time

import pytest

from clocklib.ble import ClockLink

from update_server import Server

pytestmark = [pytest.mark.phase6, pytest.mark.slow]

FIRMWARE = os.path.join(os.path.dirname(__file__), "..", "..", "webflasher", "c6-fx-clock-ota.bin")


@pytest.fixture(scope="module")
def server():
    s = Server(FIRMWARE).start()
    yield s
    s.stop()


@pytest.fixture(scope="module")
def clock(link, server):
    """The paired clock's address; the module link is closed so every test opens its own."""
    running = link.request("get info", "info")["ver"]
    if server.running_version != running:  # the "good" test would install that other version
        pytest.exit(f"webflasher/c6-fx-clock-ota.bin is {server.running_version}, the clock runs {running}: "
                    "run `pio run` and tools/build_webflasher.py first", returncode=2)
    addr = link.address
    link.close()
    return addr


def info(ble_name, addr):
    lk = ClockLink(ble_name).connect(addr, retries=3)
    try:
        return lk.request("get info", "info", timeout=10)
    finally:
        lk.close()


def run_update(ble_name, addr, serial, url, timeout=420):
    before = info(ble_name, addr)
    lk = ClockLink(ble_name).connect(addr, retries=3)
    at = serial.mark()
    lk.send(f"update apply {url}")
    ack = lk.wait_for("ack", match=lambda m: m.get("cmd") == "update")
    lk.close()
    assert ack["ok"], ack
    serial.wait_for(r"Boot mode: update", 30, at)
    serial.wait_for(r"update: Result: (\w+)", timeout, at)
    serial.wait_for(r"Advertising as", 60, at)
    after = info(ble_name, addr)
    assert after["upd"]["at"] == before["upd"]["at"] + 1
    return before, after


def test_refuses_plain_http_to_the_internet(ble_name, clock):
    lk = ClockLink(ble_name).connect(clock, retries=3)
    try:
        lk.send("update apply http://example.com/manifest.json")
        ack = lk.wait_for("ack", match=lambda m: m.get("cmd") == "update")
        assert not ack["ok"] and "https" in ack["msg"]
    finally:
        lk.close()


def test_same_version_is_up_to_date(ble_name, clock, ble_serial, server):
    _, after = run_update(ble_name, clock, ble_serial, server.url("same"))
    assert after["upd"]["state"] == "up_to_date"


def test_good_update_switches_slot(ble_name, clock, ble_serial, server):
    before, after = run_update(ble_name, clock, ble_serial, server.url("good"))
    assert after["upd"]["state"] == "updated"
    assert after["upd"]["to"] == "9.9.9" and after["upd"]["from"] == before["ver"]
    assert after["part"] != before["part"]


@pytest.mark.parametrize("scenario", ["badsha", "truncated", "foreign"])
def test_bad_images_are_rejected(ble_name, clock, ble_serial, server, scenario):
    before, after = run_update(ble_name, clock, ble_serial, server.url(scenario))
    assert after["upd"]["state"] == "bad_image"
    assert after["part"] == before["part"], "a rejected image must never be activated"


def test_missing_manifest(ble_name, clock, ble_serial, server):
    _, after = run_update(ble_name, clock, ble_serial, server.url("does-not-exist"))
    assert after["upd"]["state"] == "bad_manifest"


def test_slow_download_times_out(ble_name, clock, ble_serial, server):
    before, after = run_update(ble_name, clock, ble_serial, server.url("slow"), timeout=600)
    assert after["upd"]["state"] == "timeout"
    assert after["part"] == before["part"]


def test_power_cut_mid_download_keeps_old_firmware(ble_name, clock, ble_serial, server):
    before = info(ble_name, clock)
    lk = ClockLink(ble_name).connect(clock, retries=3)
    at = ble_serial.mark()
    lk.send(f"update apply {server.url('slow')}")
    lk.wait_for("ack", match=lambda m: m.get("cmd") == "update")
    lk.close()
    ble_serial.wait_for(r"Offered 9\.9\.9", 90, at)
    time.sleep(20)                  # downloading by now
    cut = ble_serial.mark()
    ble_serial.pulse_reset()        # = power cut
    ble_serial.wait_for(r"Boot mode: ble", 30, cut)  # never stuck in UPDATE mode
    ble_serial.wait_for(r"Advertising as", 30, cut)
    after = info(ble_name, clock)
    assert after["part"] == before["part"]
    assert after["upd"]["state"] == "interrupted"
