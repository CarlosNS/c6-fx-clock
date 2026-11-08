"""Firmware update over Bluetooth from the PC, exactly as the Android app does it (src/bleota.h).

    python tools/build_webflasher.py                       # makes webflasher/marea-ota.bin
    python tools/ble_ota.py                                # sends it; the version is read from CMakeLists.txt
    python tools/ble_ota.py --image other.bin --version 3.0.0-test

The watch must already be paired with Windows (see tools/ble_shot.py) and run a firmware that has
Bluetooth updates (3.0.0-beta.6 or later). Needs `pip install bleak`.
"""
import argparse
import hashlib
import os
import re
import sys
import time

ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.join(ROOT, "tests"))
from clocklib import protocol as P  # noqa: E402
from clocklib.ble import ClockLink  # noqa: E402


def project_version():
    text = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    return re.search(r'PROJECT_VER "([^"]+)"', text).group(1)


def next_ota(link, timeout):
    m = link.wait_for("ota", timeout)
    if m.get("st") == "err":
        raise SystemExit(f"The watch refused: {m.get('msg')}")
    return m


def send(link, data, sha, version):
    link.drain()
    link.send(f"ota begin {len(data)} {sha} {version}")
    ready = next_ota(link, 15)
    window = ready.get("win", 16384)
    chunk = max(16, min(240, link.mtu - 3 - 4))
    t0 = time.monotonic()
    state = {"acked": 0, "err": None, "done": False}

    def take(m):
        if m.get("t") != "ota":
            return
        if m.get("st") == "err":
            state["err"] = m.get("msg")
        elif m.get("st") == "done":
            state["done"] = True
        elif m.get("st") == "ack":
            state["acked"] = m.get("off", state["acked"])
            a = state["acked"]
            print(f"\r{a * 100 // len(data):3d} %  {a / 1024 / (time.monotonic() - t0):5.1f} KB/s", end="", flush=True)

    # The whole transfer runs inside bleak's event loop: one cross-thread hop per packet made it crawl
    async def pump():
        import asyncio
        off = 0
        client = link._client
        while off < len(data):
            while not link.messages.empty():
                take(link.messages.get_nowait())
            if state["err"]:
                return
            if off - state["acked"] + chunk > window:
                await asyncio.sleep(0.005)
                if time.monotonic() - pump.last > 20:
                    state["err"] = "no ack for 20 s"
                    return
                continue
            pump.last = time.monotonic()
            n = min(chunk, len(data) - off)
            await client.write_gatt_char(P.OTA_UUID, off.to_bytes(4, "little") + data[off:off + n], response=False)
            off += n
    pump.last = time.monotonic()
    link._run(pump(), timeout=1800)
    end = time.monotonic() + 30
    while not state["done"] and not state["err"] and time.monotonic() < end:
        try:
            take(link.messages.get(timeout=1))
        except Exception:
            pass
    if state["err"] or not state["done"]:
        raise SystemExit(f"\nThe update failed: {state['err'] or 'no answer at the end'}")
    print(f"\rDone in {time.monotonic() - t0:.0f} s ({len(data) / 1024 / (time.monotonic() - t0):.1f} KB/s); the watch reboots")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--image", default=os.path.join(ROOT, "webflasher", "marea-ota.bin"))
    ap.add_argument("--version", default=None, help="version the image says it is (default: CMakeLists.txt)")
    ap.add_argument("--address", default=None)
    a = ap.parse_args()
    data = open(a.image, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    version = a.version or project_version()
    print(f"{a.image}: {len(data)} bytes, {version}")
    link = ClockLink().connect(a.address, retries=2)
    try:  # Windows 11: ask for the fast connection parameters (the watch asks too)
        from winrt.windows.devices.bluetooth import BluetoothLEPreferredConnectionParameters as PCP
        link._client._backend._requester.request_preferred_connection_parameters(PCP.throughput_optimized)
    except Exception as e:
        print(f"(fast connection request not available: {e})")
    try:
        send(link, data, sha, version)
    except BaseException:
        try:
            link.send("ota abort")
        except Exception:
            pass
        raise
    finally:
        link.close()


if __name__ == "__main__":
    main()
