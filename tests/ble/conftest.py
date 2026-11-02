import pytest

from clocklib.ble import ClockLink


@pytest.fixture(scope="module")
def link(ble_name):
    """One paired, connected link per test module (test builds pair with Just Works)."""
    lk = ClockLink(ble_name).connect()
    yield lk
    lk.close()
