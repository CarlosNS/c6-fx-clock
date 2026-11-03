"""What the Android app's periodic sync does (docs/PLAN-bluetooth.md, 3.6), so the clock side can be
tested from the PC without the phone. tools/fake_phone.py (phase 2) is a thin CLI over this.
"""
import json
import time
import urllib.request

from .ble import ClockLink

OPEN_METEO = ("https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}"
              "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
              "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1")


def fetch_weather(lat, lon):
    """Returns the "wx ..." command for a location, as the app builds it."""
    r = json.load(urllib.request.urlopen(OPEN_METEO.format(lat=lat, lon=lon), timeout=15))
    c, d = r["current"], r["daily"]
    t10 = lambda v: round(v * 10)  # noqa: E731
    return (f"wx {t10(c['temperature_2m'])} {c['weather_code']} {c['relative_humidity_2m']} "
            f"{round(c['wind_speed_10m'])} {t10(d['temperature_2m_max'][0])} {t10(d['temperature_2m_min'][0])} {c['is_day']}")


def sync_once(link: ClockLink, weather_cmd=None, now=None):
    """One sync session on an open link: time, weather, info, "sync done". Returns get info."""
    now = time.time() if now is None else now
    link.send(f"time {int(now)} {int(now * 1000) % 1000}")
    if weather_cmd:
        link.send(weather_cmd)
    info = link.request("get info", "info")
    link.send("sync done")
    link.wait_for("ack", match=lambda m: m.get("cmd") == "sync")
    return info


def sync_session(name_prefix="C6 Clock", address=None, weather_cmd=None, retries=0):
    """Connect, sync, disconnect — exactly one background sync of the app. Returns (info, seconds).
    retries: extra connection attempts, as the app makes (Windows sometimes fails one at random)."""
    t0 = time.monotonic()
    link = ClockLink(name_prefix).connect(address, retries=retries)
    try:
        info = sync_once(link, weather_cmd)
    finally:
        link.close()
    return info, time.monotonic() - t0
