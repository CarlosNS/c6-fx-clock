"""Synchronous Bluetooth link to the clock for tests and PC tools.

bleak is asyncio-based and needs a running loop to deliver notifications, so the link owns an event
loop in a background thread and exposes plain blocking methods.
"""
import asyncio
import json
import queue
import threading

from bleak import BleakClient, BleakScanner

from . import protocol as P


class ClockLink:
    def __init__(self, name_prefix="C6 Clock", timeout=15.0):
        self.name_prefix = name_prefix
        self.timeout = timeout
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, daemon=True)
        self._thread.start()
        self._client = None
        self._rx = P.Reassembler()
        self.messages = queue.Queue()  # every decoded JSON message, in order
        self.address = None

    def _run(self, coro, timeout=None):
        return asyncio.run_coroutine_threadsafe(coro, self._loop).result(timeout or self.timeout + 5)

    # ---- discovery / connection ----
    def find(self, timeout=None):
        """Returns (address, advertised name) of the first clock seen, or None."""
        async def scan():
            found = await BleakScanner.discover(timeout=timeout or self.timeout, return_adv=True)
            for dev, adv in found.values():
                if (adv.local_name or "").startswith(self.name_prefix):
                    return dev.address, adv.local_name
            return None
        return self._run(scan(), (timeout or self.timeout) + 10)

    def connect(self, address=None, pair=True):
        if address is None:
            hit = self.find()
            if not hit:
                raise AssertionError(f"No device advertising '{self.name_prefix}*'")
            address = hit[0]
        self.address = address

        async def go():
            self._client = BleakClient(address, timeout=self.timeout)
            await self._client.connect()
            if pair:
                await self._client.pair()
            await self._client.start_notify(P.EVT_UUID, self._on_evt)
        self._run(go())
        return self

    def _on_evt(self, _char, data: bytearray):
        done = self._rx.feed(bytes(data))
        if done is not None:
            try:
                self.messages.put(json.loads(done.decode("utf-8")))
            except ValueError:
                self.messages.put({"t": "_bad_json", "raw": done})

    @property
    def connected(self):
        return bool(self._client and self._client.is_connected)

    @property
    def mtu(self):
        return self._client.mtu_size

    def disconnect(self):
        if self._client:
            self._run(self._client.disconnect())

    def close(self):
        try:
            self.disconnect()
        finally:
            self._loop.call_soon_threadsafe(self._loop.stop)

    # ---- traffic ----
    def send(self, cmd: str, response=True):
        self._run(self._client.write_gatt_char(P.CMD_UUID, cmd.encode("utf-8"), response=response))

    def read_fw_revision(self):
        return self._run(self._client.read_gatt_char(P.FW_REV_UUID)).decode("utf-8")

    def drain(self):
        while not self.messages.empty():
            self.messages.get_nowait()

    def wait_for(self, kind, timeout=None, match=None):
        """Waits for the next message of type `kind` (and optional predicate); returns it."""
        deadline = timeout or self.timeout
        import time
        end = time.monotonic() + deadline
        while True:
            left = end - time.monotonic()
            if left <= 0:
                raise AssertionError(f"No '{kind}' message within {deadline} s")
            try:
                m = self.messages.get(timeout=left)
            except queue.Empty:
                continue
            if m.get("t") == kind and (match is None or match(m)):
                return m

    def request(self, cmd, kind, timeout=None):
        self.drain()
        self.send(cmd)
        return self.wait_for(kind, timeout)
