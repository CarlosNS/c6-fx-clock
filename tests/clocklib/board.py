"""Helpers for the board itself: HTTP (Wi-Fi mode) and the serial log.

Note: on this board opening the serial port resets the chip, so serial helpers double as "reboot and
capture the boot log".
"""
import json
import time
import urllib.error
import urllib.request


def http_json(host, path, timeout=10):
    return json.load(urllib.request.urlopen(f"http://{host}{path}", timeout=timeout))


def wait_http(host, timeout=180):
    """Waits until the clock answers /status (reboots on a mesh network can take a while)."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            return http_json(host, "/status", timeout=4)
        except (urllib.error.URLError, OSError, ValueError):
            time.sleep(3)
    raise AssertionError(f"The clock did not answer at {host} within {timeout} s")


def post(host, path, data: bytes, timeout=120):
    req = urllib.request.Request(f"http://{host}{path}", data=data, method="POST",
                                 headers={"Content-Type": "application/octet-stream"})
    try:
        r = urllib.request.urlopen(req, timeout=timeout)
        return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def boot_log(port, secs=25):
    """Resets the board (by opening the port) and returns the boot log as text."""
    import serial
    s = serial.Serial(port, 115200, timeout=0.2)
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    end = time.monotonic() + secs
    buf = b""
    while time.monotonic() < end:
        buf += s.read(4096)
    s.close()
    return buf.decode("utf-8", "replace")
