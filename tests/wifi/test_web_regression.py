"""Regression of the Wi-Fi web mode (what works today, must keep working in the WIFI boot mode).

Needs the clock on the network: pytest tests/wifi --host 192.168.1.140
"""
import asyncio
import json
import struct
import urllib.request

import pytest
import websockets

from clocklib.board import http_json, post, wait_http

pytestmark = pytest.mark.now


@pytest.fixture(scope="module")
def status(host):
    return wait_http(host)


def ws_send(host, *cmds, settle=0.3):
    async def go():
        async with websockets.connect(f"ws://{host}/ws", open_timeout=20) as ws:
            for c in cmds:
                await ws.send(c)
            await asyncio.sleep(settle)
    asyncio.run(go())


def test_status_is_complete_json(status):
    for k in ("fx", "bri", "spd", "wks", "chue", "ver", "part", "otamax", "heap", "wloc", "tv"):
        assert k in status, k
    assert status["heap"] > 20000, "free heap dangerously low"


def test_faces_catalogue(host):
    f = http_json(host, "/faces")
    assert len(f["groups"]) == 5
    assert len(f["faces"]) == 23
    for face in f["faces"]:
        assert face["label"] and face["desc"] and face["o"] in ("h", "v")
        assert 0 <= face["g"] < len(f["groups"])
    assert {x["label"] for x in f["faces"] if x["hint"]} == {"Plasma", "Cubo 3D", "Túnel", "Lava", "Vida"}


def test_setting_roundtrip(host, status):
    old = status["bri"]
    new = 37 if old != 37 else 38
    ws_send(host, f"bri {new}")
    assert http_json(host, "/status")["bri"] == new
    ws_send(host, f"bri {old}")
    assert http_json(host, "/status")["bri"] == old


def test_setting_is_clamped(host, status):
    old = status["wks"]
    ws_send(host, "wks 99999")
    assert http_json(host, "/status")["wks"] == 600
    ws_send(host, f"wks {old}")


@pytest.mark.parametrize("fx,size", [(0, (172, 320)), (6, (320, 172))])
def test_screenshot_orientation(host, status, fx, size):
    data = urllib.request.urlopen(f"http://{host}/screenshot?fx={fx}&wait=800", timeout=30).read()
    assert data[:2] == b"BM"
    w, h = struct.unpack("<ii", data[18:26])
    assert (w, abs(h)) == size
    ws_send(host, f"fx {status['fx']}")


def test_ota_rejects_garbage(host):
    code, msg = post(host, "/ota", b"\x00" * 4096)
    assert code == 400 and "firmware" in msg
    assert http_json(host, "/status")["ota"] == -1


@pytest.mark.slow
def test_every_face_holds_30fps(host, status):
    faces = http_json(host, "/faces")["faces"]
    ws_send(host, "aon 1")
    slow = []
    try:
        for i, f in enumerate(faces):
            ws_send(host, f"fx {i}", settle=2.4)
            s = http_json(host, "/status")
            if s["rmax"] > 33333:
                slow.append((f["label"], s["rmax"]))
    finally:
        ws_send(host, f"fx {status['fx']}", f"aon {status['aon']}", f"auto {status['auto']}")
    assert not slow, f"faces over the 33 ms budget: {slow}"
