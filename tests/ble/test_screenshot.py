"""Phase 4: screenshots over Bluetooth (so faces can be checked with Wi-Fi off).

Protocol: "shot" -> {"t":"shot","w":..,"h":..,"fmt":"rgb565le"} then {"t":"shotdata","y":row,"d":"<base64>"}
messages until {"t":"shotend"}.
"""
import base64
import time

import pytest

pytestmark = pytest.mark.phase4


def grab(link, fx=None):
    if fx is not None:
        link.send(f"fx {fx}")
        time.sleep(1.5)
    link.drain()
    link.send("shot")
    head = link.wait_for("shot", timeout=10)
    data = bytearray()
    while True:
        m = link.messages.get(timeout=15)
        if m.get("t") == "shotdata":
            data += base64.b64decode(m["d"])
        elif m.get("t") == "shotend":
            break
    return head, bytes(data)


@pytest.mark.parametrize("fx,size", [(0, (172, 320)), (6, (320, 172))])
def test_shot_size_follows_orientation(link, fx, size):
    old = link.request("get status", "status")["fx"]
    t0 = time.monotonic()
    head, data = grab(link, fx)
    assert (head["w"], head["h"]) == size
    assert len(data) == size[0] * size[1] * 2
    assert time.monotonic() - t0 < 12
    link.send(f"fx {old}")


def test_shot_changes_with_the_face(link):
    old = link.request("get status", "status")["fx"]
    _, a = grab(link, 21)
    _, b = grab(link, 22)
    assert a != b
    link.send(f"fx {old}")
