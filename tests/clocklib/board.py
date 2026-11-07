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
    """Serial console of the clock: reads the log and sends commands (console.c). Opening the session
    resets the board: the C6 resets when its port is opened, the S3 does not, so if no boot banner
    shows up the session pulses RTS (_ensure_reset). When the firmware itself restarts (mode switch)
    the USB port drops for a moment and the session reopens it, which keeps the log going."""

    def __init__(self, port):
        import serial
        self._serial = serial
        self.port = port
        self.text = ""
        self._s = None
        self._closed = False
        self._busy = False
        self._open()
        self._ensure_reset()

    def _ensure_reset(self):
        """Waits briefly for the ROM banner of a reset; without it, resets through RTS. On Windows the
        usbser.sys driver only sends the line state when DTR is written too (esptool does the same)."""
        start = len(self.text)
        end = time.monotonic() + 1.5
        while time.monotonic() < end:
            self._read()
            if "ESP-ROM:" in self.text[start:]:
                return
        self._s.rts = True
        self._s.dtr = self._s.dtr
        time.sleep(0.1)
        self._s.rts = False
        self._s.dtr = self._s.dtr

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
        if self._closed or self._busy:
            time.sleep(0.05)
            return
        try:
            data = self._s.read(4096)
        except self._serial.SerialException:
            try:
                self._s.close()
            except Exception:
                pass
            time.sleep(1)
            # never reopen a session closed meanwhile, nor one pulse_reset already reopened
            if not self._closed and not self._busy and not (self._s and self._s.is_open):
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

    def wait_for(self, pattern, timeout=30, since=0):
        """Like read_until, for when another thread is already reading the port (it only watches
        the collected text)."""
        import re
        rx = re.compile(pattern)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            m = rx.search(self.text, since)
            if m:
                return m
            time.sleep(0.1)
        raise AssertionError(f"'{pattern}' not seen in the serial log within {timeout} s")

    def wait_advertising(self, since=0, timeout=30):
        """Waits until the clock advertises over Bluetooth after a boot. A paired clock that boots
        without the time keeps quiet 60 s so the phone notices (src/ble.c); here it gets the time
        over the console instead, which ends the silence at once."""
        m = self.wait_for(r"Advertising as|Quiet for", timeout, since)
        if m.group(0) == "Quiet for":
            t = time.time()
            self.send(f"time {int(t)} {int(t * 1000) % 1000}")
            self.wait_for(r"Advertising as", 10, m.end())

    def mark(self):
        """Position in the log, to search only what comes after it."""
        return len(self.text)

    def send(self, line):
        self._s.write((line + "\n").encode("utf-8"))
        self._s.flush()

    def status(self, timeout=5, reading=True):
        """The clock's status JSON (same as /status) via the console, whatever link is in use.
        reading=True when another thread is already reading the port (as in tests/ble)."""
        import json
        at = self.mark()
        self.send("status")
        m = (self.wait_for if reading else self.read_until)(r"status (\{.*\})\r?\n", timeout, at)
        return json.loads(m.group(1))

    def pulse_reset(self):
        """Resets the board like a power cut, keeping the session (and the reader thread): the
        port is closed and opened again, and opening it is what resets the chip."""
        self._busy = True
        try:
            self._s.close()
            time.sleep(0.3)
            self._open()
        finally:
            self._busy = False
        self._ensure_reset()

    def reset(self):
        """Closes and reopens the port, which resets the board (like a power cut)."""
        if self._s:
            self._s.close()
        self._open()
        self._ensure_reset()

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
