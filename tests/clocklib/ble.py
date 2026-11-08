"""Synchronous Bluetooth link to the clock for tests and PC tools.

bleak is asyncio-based and needs a running loop to deliver notifications. All links share ONE event
loop in a background thread: on Windows every client of the same address shares a single system
connection, and a client left behind on another loop can close it under the live one.
"""
import asyncio
import json
import queue
import threading

from bleak import BleakClient, BleakScanner

from . import protocol as P


_LOOP = None
_LOOP_LOCK = threading.Lock()


def _shared_loop():
    global _LOOP
    with _LOOP_LOCK:
        if _LOOP is None:
            _LOOP = asyncio.new_event_loop()
            threading.Thread(target=_LOOP.run_forever, daemon=True, name="bleak-loop").start()
        return _LOOP


class ClockLink:
    def __init__(self, name_prefix="Marea", timeout=15.0):
        self.name_prefix = name_prefix
        self.timeout = timeout
        self._loop = _shared_loop()
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

    def connect(self, address=None, pair=False, retries=0):
        """Connects (scanning by name if no address is given) and subscribes to EVT. With retries,
        failed attempts are repeated a few seconds apart: after the clock reboots, Windows can hand
        back a stale session for a paired device until it notices the old link is gone."""
        for attempt in range(retries + 1):
            try:
                return self._connect(address, pair)
            except Exception:
                if attempt == retries:
                    raise
                import time
                time.sleep(4)

    def _connect(self, address, pair):
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
        try:
            self._run(go())
        except BaseException:
            self.close()  # never leave a half-open client behind
            raise
        return self

    def pair_with_pin(self, get_pin, timeout=40):
        """Pairs using Passkey Entry, as a phone does: the clock shows a 6-digit code and `get_pin()`
        must return it (tests read it from the serial log). bleak itself only does Just Works, so
        this drives the Windows custom pairing API directly. Returns the result status name."""
        from winrt.windows.devices.enumeration import DevicePairingKinds, DevicePairingProtectionLevel

        async def go():
            info = self._client._backend._requester.device_information
            if info.pairing.is_paired:
                return "ALREADY_PAIRED"
            custom = info.pairing.custom

            def handler(sender, args):
                deferral = args.get_deferral()
                try:
                    if args.pairing_kind == DevicePairingKinds.PROVIDE_PIN:
                        args.accept(str(get_pin()))
                    else:
                        args.accept()
                finally:
                    deferral.complete()
            token = custom.add_pairing_requested(handler)
            try:
                kinds = DevicePairingKinds(int(DevicePairingKinds.CONFIRM_ONLY) | int(DevicePairingKinds.PROVIDE_PIN))
                res = await custom.pair_with_protection_level_async(
                    kinds, DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION)
            finally:
                custom.remove_pairing_requested(token)
            return res.status.name
        return self._run(go(), timeout)

    def unpair(self):
        """Makes Windows forget the clock and drops the link. (bleak's unpair alone can leave the
        radio link up, and while connected the clock does not advertise.)"""
        if self._client:
            try:
                self._run(self._client.unpair())
            finally:
                self.disconnect()

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
            try:
                self._run(self._client.disconnect())
            finally:
                self._client = None

    def close(self):
        """Disconnects; the shared loop keeps running for the next link."""
        try:
            self.disconnect()
        except Exception:
            pass

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
