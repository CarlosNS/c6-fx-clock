"""Phase 2 (clock side) and 5c (app side): periodic sync (docs/PLAN-bluetooth.md, 3.6).

The PC plays the phone with clocklib.phone, doing what the app's WorkManager task does.
"""
import time

import pytest

from clocklib.phone import fetch_weather, sync_once

pytestmark = pytest.mark.phase2


def test_time_from_the_phone_is_applied(link):
    # Send a deliberately wrong time, then the right one, and check the clock follows both
    link.send("time 1700000000 0")   # 14 Nov 2023
    assert link.request("get status", "status")["time"].count(":") == 2
    sync_once(link)
    clock = link.request("get status", "status")["time"]
    h, m, s = map(int, clock.split(":"))
    now = time.localtime()
    drift = abs((h * 3600 + m * 60 + s) - (now.tm_hour * 3600 + now.tm_min * 60 + now.tm_sec))
    assert min(drift, 86400 - drift) <= 2, f"clock {clock} vs PC {time.strftime('%H:%M:%S')}"


def test_weather_from_the_phone_is_shown(link):
    st = link.request("get status", "status")
    cmd = fetch_weather(st["wlat"], st["wlon"])
    sync_once(link, cmd)
    st = link.request("get status", "status")
    assert st["wv"] == 1 and st["wweb"] == 1
    assert st["wt"] == int(cmd.split()[1])
    assert st["wage"] < 30


def test_sync_is_recorded(link):
    before = link.request("get info", "info")["sync"]["count"]
    info = sync_once(link)
    after = link.request("get info", "info")["sync"]
    assert after["count"] == before + 1
    assert abs(after["last"] - time.time()) < 5
    assert info["mode"] == "ble"
