"""Phase 2: Bluetooth transport (docs/PLAN-bluetooth.md, 3.1 and 3.2)."""
import re
import time

import pytest

from clocklib import fx_count
from clocklib.ble import ClockLink

pytestmark = pytest.mark.phase2


def test_advertises(ble_name):
    lk = ClockLink(ble_name)
    try:
        hit = lk.find(timeout=10)
    finally:
        lk.close()
    assert hit, "clock not advertising"
    assert hit[1].startswith(ble_name) and len(hit[1]) == len(ble_name) + 5  # "Marea ABCD"


def test_mtu_is_negotiated(link):
    assert link.mtu >= 185, "small MTU: faces would take too many notifications"


def test_info(link):
    info = link.request("get info", "info")
    assert info["mode"] == "ble"
    assert re.match(r"\d+\.\d+\.\d+", info["ver"])  # "3.0.0", or with a suffix such as "3.0.0-beta.1"
    assert info["part"] in ("ota_0", "ota_1")
    assert "sync" in info and "upd" in info


def test_faces_match_the_firmware(link):
    f = link.request("get faces", "faces", timeout=20)
    assert len(f["faces"]) == fx_count() and len(f["groups"]) == 5
    assert link._rx.dropped == 0


def test_status_has_the_settings(link):
    s = link.request("get status", "status")
    for k in ("fx", "bri", "spd", "wks", "chue", "aon", "h24", "secs"):
        assert k in s, k


def test_setting_change_is_notified(link):
    old = link.request("get status", "status")["bri"]
    new = 41 if old != 41 else 42
    link.drain()
    link.send(f"bri {new}")
    s = link.wait_for("status", match=lambda m: m["bri"] == new)
    assert s["bri"] == new
    link.send(f"bri {old}")
    link.wait_for("status", match=lambda m: m["bri"] == old)


def test_face_change(link):
    old = link.request("get status", "status")["fx"]
    link.send("fx 21")
    assert link.wait_for("status", match=lambda m: m["fx"] == 21)["auto"] == 0
    link.send(f"fx {old}")


def test_unknown_and_malformed_commands_are_harmless(link):
    old = link.request("get status", "status")["fx"]
    for junk in ("nope 1", "bri", "fx -999", "x" * 240, "wifi\nsin-clave"):
        link.send(junk)
    s = link.request("get status", "status")
    assert s["fx"] == 0, "out-of-range values are clamped"
    assert link.connected
    link.send(f"fx {old}")


def test_burst_of_commands(link):
    t0 = time.monotonic()
    for i in range(100):
        link.send(f"spd {100 + i % 5}", response=False)
    link.send("spd 100")
    assert link.wait_for("status", match=lambda m: m["spd"] == 100)
    assert time.monotonic() - t0 < 20


def test_heartbeat_status_every_30s(link):
    link.drain()
    link.wait_for("status", timeout=40)
