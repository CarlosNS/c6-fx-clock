"""Phase 6: updates over the internet (docs/PLAN-bluetooth.md, 3.4).

Flow per test: over Bluetooth send "update apply <manifest url>" -> the clock reboots into UPDATE
mode, tries, stores the result and reboots into BLE mode -> reconnect and read "get info".upd.

Needs: a test build (accepts http://), the clock's Wi-Fi network already stored, and the PC
reachable from the clock on port 8770 (Windows firewall may need an inbound rule).
"""
import os
import time

import pytest

from clocklib.ble import ClockLink
from clocklib.board import boot_log

from update_server import Server

pytestmark = [pytest.mark.phase6, pytest.mark.slow]

FIRMWARE = os.path.join(os.path.dirname(__file__), "..", "..", "webflasher", "c6-fx-clock-ota.bin")


@pytest.fixture(scope="module")
def server():
    s = Server(FIRMWARE).start()
    yield s
    s.stop()


def run_update(ble_name, url, timeout=420):
    lk = ClockLink(ble_name).connect()
    addr = lk.address
    before = lk.request("get info", "info")
    lk.send(f"update apply {url}")
    lk.wait_for("ack", match=lambda m: m.get("cmd") == "update")
    lk.close()
    # The clock is now in UPDATE mode (no Bluetooth); wait for it to come back
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        time.sleep(10)
        try:
            lk = ClockLink(ble_name, timeout=10).connect(addr)
        except Exception:
            continue
        try:
            info = lk.request("get info", "info")
        finally:
            lk.close()
        if info["upd"].get("at") != before["upd"].get("at"):
            return before, info
    raise AssertionError("the clock did not come back from UPDATE mode")


def test_same_version_is_up_to_date(ble_name, server):
    _, info = run_update(ble_name, server.url("same"))
    assert info["upd"]["state"] == "up_to_date"


def test_good_update_switches_slot(ble_name, server):
    before, info = run_update(ble_name, server.url("good"))
    assert info["upd"]["state"] == "updated"
    assert info["part"] != before["part"]


@pytest.mark.parametrize("scenario", ["badsha", "truncated", "foreign"])
def test_bad_images_are_rejected(ble_name, server, scenario):
    before, info = run_update(ble_name, server.url(scenario))
    assert info["upd"]["state"] == "bad_image"
    assert info["part"] == before["part"], "a rejected image must never be activated"


def test_missing_manifest(ble_name, server):
    _, info = run_update(ble_name, server.url("does-not-exist"))
    assert info["upd"]["state"] == "bad_manifest"


def test_slow_download_times_out(ble_name, server):
    before, info = run_update(ble_name, server.url("slow"), timeout=600)
    assert info["upd"]["state"] == "timeout"
    assert info["part"] == before["part"]


def test_power_cut_mid_download_keeps_old_firmware(ble_name, server, serial_port):
    lk = ClockLink(ble_name).connect()
    before = lk.request("get info", "info")
    lk.send(f"update apply {server.url('slow')}")
    lk.close()
    time.sleep(40)                     # downloading by now
    log = boot_log(serial_port, 30)    # opening the port resets the board = power cut
    assert "Loaded app" in log
    lk = ClockLink(ble_name).connect()
    try:
        assert lk.request("get info", "info")["part"] == before["part"]
    finally:
        lk.close()
