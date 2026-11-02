"""Phase 3: Bluetooth security (docs/PLAN-bluetooth.md, 3.3).

The passkey shown on the screen is tested by hand with the phone (tests/MANUAL.md): Windows cannot
type it in automatically, so test builds pair with Just Works.
"""
import pytest

from clocklib.ble import ClockLink
from clocklib import protocol as P

pytestmark = pytest.mark.phase3


def test_firmware_version_is_public(ble_name):
    lk = ClockLink(ble_name).connect(pair=False)
    try:
        assert lk.read_fw_revision().count(".") == 2
    finally:
        lk.close()


def test_commands_need_an_encrypted_link(ble_name):
    lk = ClockLink(ble_name)
    hit = lk.find()
    lk.close()
    # Forget any bond Windows kept, then try without pairing
    lk = ClockLink(ble_name).connect(hit[0], pair=False)
    try:
        lk._run(lk._client.unpair())
    except Exception:
        pass
    lk.close()
    lk = ClockLink(ble_name).connect(hit[0], pair=False)
    try:
        with pytest.raises(Exception):
            lk._run(lk._client.write_gatt_char(P.CMD_UUID, b"bri 5", response=True))
    finally:
        lk.close()


def test_unpair_forgets_this_pc(link):
    link.send("unpair")
    link.wait_for("ack", match=lambda m: m.get("cmd") == "unpair")
