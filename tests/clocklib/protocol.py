"""Bluetooth protocol of the clock (docs/PLAN-bluetooth.md, section 3). Single source for the tests
and the PC tools; the phone app implements the same rules in transport.js."""

SERVICE_UUID = "7f1c0001-6c0c-4e0b-a1c6-0000c6c10c00"
CMD_UUID = "7f1c0002-6c0c-4e0b-a1c6-0000c6c10c00"   # write, encrypted: one text command
EVT_UUID = "7f1c0003-6c0c-4e0b-a1c6-0000c6c10c00"   # notify, encrypted: framed JSON messages
OTA_UUID = "7f1c0004-6c0c-4e0b-a1c6-0000c6c10c00"   # write without response, encrypted: firmware packets (src/bleota.h)
FW_REV_UUID = "00002a26-0000-1000-8000-00805f9b34fb"  # Device Information: firmware revision (public)

LAST = 0x80
FIRST = 0x40
ID_MASK = 0x3F


def frame(msg_id: int, payload: bytes, chunk: int):
    """Splits one message into notifications of at most `chunk` bytes (header included)."""
    body = chunk - 1
    if body < 1:
        raise ValueError("chunk too small")
    parts = [payload[i:i + body] for i in range(0, len(payload), body)] or [b""]
    out = []
    for i, p in enumerate(parts):
        h = msg_id & ID_MASK
        if i == 0:
            h |= FIRST
        if i == len(parts) - 1:
            h |= LAST
        out.append(bytes([h]) + p)
    return out


class Reassembler:
    """Joins notifications back into whole messages. Out-of-sequence data drops the partial message."""

    def __init__(self):
        self._id = None
        self._buf = bytearray()
        self.dropped = 0

    def feed(self, data: bytes):
        """Returns the complete payload when `data` finishes a message, else None."""
        if not data:
            return None
        h, body = data[0], data[1:]
        mid = h & ID_MASK
        if h & FIRST:
            if self._id is not None:
                self.dropped += 1
            self._id, self._buf = mid, bytearray(body)
        elif self._id == mid:
            self._buf += body
        else:
            self.dropped += 1
            self._id, self._buf = None, bytearray()
            return None
        if h & LAST and self._id == mid:
            done = bytes(self._buf)
            self._id, self._buf = None, bytearray()
            return done
        return None
