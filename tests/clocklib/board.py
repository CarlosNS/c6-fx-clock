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


class SerialSession:
    """Serial console of the clock: reads the log and sends commands (console.c). Opening the port
    resets the board; when the firmware itself restarts (mode switch) the USB port drops for a
    moment and the session reopens it, which keeps the log going."""

    def __init__(self, port):
        import serial
        self._serial = serial
        self.port = port
        self.text = ""
        self._s = None
        self._closed = False
        self._open()

    def _open(self):
        end = time.monotonic() + 15
        while True:
            try:
                self._s = self._serial.Serial(self.port, 115200, timeout=0.2)
                self._s.dtr = False
                self._s.rts = False
                return
            except self._serial.SerialException:
                if time.monotonic() > end:
                    raise
                time.sleep(0.5)

    def _read(self):
        if self._closed:
            return
        try:
            data = self._s.read(4096)
        except self._serial.SerialException:
            try:
                self._s.close()
            except Exception:
                pass
            time.sleep(1)
            if not self._closed:  # never reopen a session that was closed meanwhile
                self._open()
            return
        self.text += data.decode("utf-8", "replace")

    def read_until(self, pattern, timeout=30, since=0):
        """Waits for a regex in the log after position `since`; returns the match."""
        import re
        rx = re.compile(pattern)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            m = rx.search(self.text, since)
            if m:
                return m
            self._read()
        raise AssertionError(f"'{pattern}' not seen in the serial log within {timeout} s")

    def mark(self):
        """Position in the log, to search only what comes after it."""
        return len(self.text)

    def send(self, line):
        self._s.write((line + "\n").encode("utf-8"))
        self._s.flush()

    def reset(self):
        """Closes and reopens the port, which resets the board (like a power cut)."""
        if self._s:
            self._s.close()
        self._open()

    def close(self):
        self._closed = True
        if self._s:
            self._s.close()


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
