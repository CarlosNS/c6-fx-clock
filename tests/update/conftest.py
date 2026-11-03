import os
import sys

# update_server.py lives next to the tests and is also a standalone tool
sys.path.insert(0, os.path.dirname(__file__))

import pytest  # noqa: E402

# Same setup as the Bluetooth tests: the clock in BLE mode with its serial log open, and a bond
from ble.conftest import ble_mode, link, pair  # noqa: E402,F401


@pytest.fixture(scope="package", autouse=True)
def ble_serial(serial_port):
    with ble_mode(serial_port) as s:
        yield s
