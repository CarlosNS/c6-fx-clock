"""Phase 2: whole sessions (connect, work, disconnect), as the app's background sync does them.

Separate from test_sync.py on purpose: these open their own connections, and the clock accepts one
connection at a time (it stops advertising while connected).
"""
import pytest

from clocklib.ble import ClockLink
from clocklib.phone import sync_session

pytestmark = pytest.mark.phase2


def test_a_background_sync_is_short(ble_name):
    """Like the app: the paired clock's address is known, so a sync is connect + talk + disconnect
    with no scanning. (Finding it by name is only needed once, when pairing.)"""
    lk = ClockLink(ble_name)
    hit = lk.find()
    lk.close()
    assert hit, "clock not advertising"
    times = sorted(sync_session(ble_name, hit[0])[1] for _ in range(3))
    # Typically ~4 s; Windows sometimes takes longer to reconnect, so judge the median
    assert times[1] < 6 and times[-1] < 15, f"syncs took {[round(t, 1) for t in times]} s"


@pytest.mark.slow
def test_many_syncs_do_not_leak(ble_name):
    lk = ClockLink(ble_name).connect()
    heap0 = lk.request("get status", "status")["heap"]
    addr = lk.address
    lk.close()
    for _ in range(50):
        sync_session(ble_name, addr, retries=2)  # leaks are the point here, not Windows' odd failed connect
    lk = ClockLink(ble_name).connect(addr)
    heap1 = lk.request("get status", "status")["heap"]
    lk.close()
    assert heap1 > heap0 - 2048, f"heap fell from {heap0} to {heap1} after 50 syncs"


def test_clock_stays_usable_between_syncs(ble_name):
    """After a sync the clock keeps advertising so the next one finds it."""
    sync_session(ble_name)
    lk = ClockLink(ble_name)
    try:
        assert lk.find(timeout=5), "not advertising after a sync"
    finally:
        lk.close()


@pytest.mark.slow
def test_reconnect_cycles_do_not_leak(ble_name):
    lk = ClockLink(ble_name).connect()
    first = lk.request("get status", "status")["heap"]
    addr = lk.address
    lk.close()
    for _ in range(30):
        lk = ClockLink(ble_name).connect(addr, retries=2)
        lk.close()
    lk = ClockLink(ble_name).connect(addr)
    last = lk.request("get status", "status")["heap"]
    lk.close()
    assert last > first - 2048, f"heap fell from {first} to {last} after 30 reconnects"
