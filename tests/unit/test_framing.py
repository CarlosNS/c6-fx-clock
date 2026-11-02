"""Framing of Bluetooth messages (protocol.py). Pure Python: runs without the board."""
import json
import random

import pytest

from clocklib import protocol as P

pytestmark = pytest.mark.now


@pytest.mark.parametrize("size", [0, 1, 243, 244, 245, 6000])
@pytest.mark.parametrize("chunk", [20, 244])
def test_roundtrip(size, chunk):
    payload = bytes(random.Random(size).randrange(256) for _ in range(size))
    r = P.Reassembler()
    frames = P.frame(5, payload, chunk)
    assert all(len(f) <= chunk for f in frames)
    out = [r.feed(f) for f in frames]
    assert out[:-1] == [None] * (len(frames) - 1)
    assert out[-1] == payload


def test_header_bits():
    frames = P.frame(63, b"x" * 50, 20)
    assert frames[0][0] & P.FIRST and not frames[0][0] & P.LAST
    assert frames[-1][0] & P.LAST and not frames[-1][0] & P.FIRST
    assert all(f[0] & P.ID_MASK == 63 for f in frames)
    single = P.frame(1, b"{}", 244)
    assert len(single) == 1 and single[0][0] == P.FIRST | P.LAST | 1


def test_interleaved_message_is_dropped():
    r = P.Reassembler()
    a = P.frame(1, b"a" * 100, 20)
    b = P.frame(2, b"b" * 10, 20)
    assert len(b) == 1  # message 2 fits in one frame
    assert r.feed(a[0]) is None
    assert r.feed(b[0]) == b"b" * 10  # a new FIRST discards the unfinished message 1
    assert r.dropped == 1
    # The rest of message 1 is now out of sequence and ignored, then a new message works again
    for f in a[1:]:
        r.feed(f)
    assert r.feed(P.frame(3, b"ok", 20)[0]) == b"ok"


def test_json_messages_of_real_size():
    faces = {"t": "faces", "faces": [{"label": f"Esfera {i}", "desc": "á" * 200} for i in range(23)]}
    raw = json.dumps(faces, ensure_ascii=False).encode()
    r = P.Reassembler()
    done = [r.feed(f) for f in P.frame(9, raw, 244)][-1]
    assert json.loads(done.decode()) == faces
