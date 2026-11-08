"""Shared options for the clock test-suite.

  --host      the watch's IP on the home Wi-Fi        (default 192.168.1.157)
  --port      serial port of the board                (default COM7; sessions reset it through RTS)
  --ble-name  prefix of the advertised Bluetooth name (default "Marea")
"""
import pytest


def pytest_addoption(parser):
    parser.addoption("--host", default="192.168.1.157")
    parser.addoption("--port", default="COM7")
    parser.addoption("--ble-name", default="Marea")


@pytest.fixture(scope="session")
def host(request):
    return request.config.getoption("--host")


# User settings the tests may touch (settings.c keys). Saved before the run, restored after it.
USER_SETTINGS = ("fx", "fxoff", "auto", "spd", "bri", "h24", "secs", "non", "nst", "nen", "nbr", "aon", "wks", "chue")


@pytest.fixture(scope="session", autouse=True)
def keep_user_settings(request):
    """The clock is in daily use: whatever the tests change, put the owner's settings back."""
    import asyncio

    from clocklib.board import http_json, wait_http

    host = request.config.getoption("--host")
    try:
        before = http_json(host, "/status", timeout=5)
    except Exception:
        before = None  # not reachable over Wi-Fi now (e.g. Bluetooth-only run): nothing to restore here
    yield
    if not before:
        return
    try:
        now = wait_http(host, 180)
    except AssertionError:
        print("\n!! Could not restore the clock's settings: not reachable over Wi-Fi")
        return
    cmds = [f"{k} {before[k]}" for k in USER_SETTINGS if k in before and now.get(k) != before[k]]
    if not cmds:
        return

    async def send():
        import websockets
        async with websockets.connect(f"ws://{host}/ws", open_timeout=20) as ws:
            for c in cmds:
                await ws.send(c)
            await asyncio.sleep(0.5)
    asyncio.run(send())
    print(f"\nRestored the clock's settings: {', '.join(cmds)}")


@pytest.fixture(scope="session")
def serial_port(request):
    return request.config.getoption("--port")


@pytest.fixture(scope="session")
def ble_name(request):
    return request.config.getoption("--ble-name")
