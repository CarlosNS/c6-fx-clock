"""Shared test vectors (tests/app/vectors.json): built from protocol.py and phone.py here, and checked
byte for byte by the app's Kotlin tests (android/app/src/test), so the PC tools, the clock tests and the
phone agree on framing and on the "wx" command.

    REGEN=1 python -m pytest unit/test_shared_vectors.py    # rewrite the file after a protocol change
"""
import json
import os
import random

import pytest

from clocklib import protocol as P

pytestmark = pytest.mark.now

PATH = os.path.join(os.path.dirname(__file__), "..", "app", "vectors.json")

# A real Open-Meteo answer (trimmed to the fields the clock uses)
FORECAST = {
    "current": {"temperature_2m": 18.4, "relative_humidity_2m": 63, "weather_code": 3, "wind_speed_10m": 12.6, "is_day": 1},
    "daily": {"temperature_2m_max": [24.95], "temperature_2m_min": [11.04]},
}


def weather_command(forecast):
    """The same formula as clocklib.phone.fetch_weather, without the network."""
    c, d = forecast["current"], forecast["daily"]
    t10 = lambda v: round(v * 10)  # noqa: E731
    return (f"wx {t10(c['temperature_2m'])} {c['weather_code']} {c['relative_humidity_2m']} "
            f"{round(c['wind_speed_10m'])} {t10(d['temperature_2m_max'][0])} {t10(d['temperature_2m_min'][0])} {c['is_day']}")


def build():
    frames = []
    for msg_id, size, chunk in [(0, 0, 244), (5, 1, 20), (63, 243, 244), (12, 245, 244), (7, 600, 20), (33, 6000, 244)]:
        payload = bytes(random.Random(size).randrange(256) for _ in range(size))
        frames.append({"id": msg_id, "chunk": chunk, "payload": payload.hex(),
                       "frames": [f.hex() for f in P.frame(msg_id, payload, chunk)]})
    text = json.dumps({"t": "status", "wloc": "Sevilla", "nota": "ñandú, «acentos» y 😀"}, ensure_ascii=False).encode()
    frames.append({"id": 9, "chunk": 20, "payload": text.hex(), "frames": [f.hex() for f in P.frame(9, text, 20)]})
    return {"framing": frames, "weather": {"forecast": FORECAST, "command": weather_command(FORECAST)}}


def test_vectors_match_protocol():
    want = build()
    if os.environ.get("REGEN"):
        with open(PATH, "w", encoding="utf-8", newline="\n") as f:
            json.dump(want, f, ensure_ascii=False, indent=1)
            f.write("\n")
    with open(PATH, encoding="utf-8") as f:
        assert json.load(f) == want, "tests/app/vectors.json is stale: regenerate with REGEN=1"
