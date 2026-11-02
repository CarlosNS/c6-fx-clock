"""Plays the phone app's periodic sync (docs/PLAN-bluetooth.md, 3.6): connect, send the time and the
weather for the clock's city, read the info, "sync done", disconnect.

    python tools/fake_phone.py                    # one sync
    python tools/fake_phone.py --every 60 --count 30   # a sync every minute, 30 times

Needs a Bluetooth adapter, `pip install bleak` and internet for the weather (Open-Meteo).
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tests"))
from clocklib.ble import ClockLink  # noqa: E402
from clocklib.phone import fetch_weather, sync_once  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="C6 Clock")
    ap.add_argument("--address")
    ap.add_argument("--every", type=float, default=0, help="seconds between syncs")
    ap.add_argument("--count", type=int, default=1)
    a = ap.parse_args()
    addr = a.address
    for i in range(a.count):
        t0 = time.monotonic()
        try:
            link = ClockLink(a.name).connect(addr)
            addr = link.address
            st = link.request("get status", "status")
            wx = fetch_weather(st["wlat"], st["wlon"])
            info = sync_once(link, wx)
            link.close()
            print(f"[{time.strftime('%H:%M:%S')}] sync {i + 1}/{a.count} ok en {time.monotonic() - t0:.1f} s · "
                  f"{st['wloc']} {int(wx.split()[1]) / 10:.1f} °C · syncs {info['sync']['count'] + 1} · heap {info['heap']}")
        except Exception as e:
            print(f"[{time.strftime('%H:%M:%S')}] sync {i + 1}/{a.count} FALLÓ: {e}")
        if i + 1 < a.count:
            time.sleep(max(0, a.every - (time.monotonic() - t0)))


if __name__ == "__main__":
    main()
