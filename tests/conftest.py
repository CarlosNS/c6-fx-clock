"""Shared options for the clock test-suite.

  --host      the clock's IP in Wi-Fi mode            (default 192.168.1.140)
  --port      serial port of the board                (default COM5; opening it RESETS the board)
  --ble-name  prefix of the advertised Bluetooth name (default "C6 Clock")
"""
import pytest


def pytest_addoption(parser):
    parser.addoption("--host", default="192.168.1.140")
    parser.addoption("--port", default="COM5")
    parser.addoption("--ble-name", default="C6 Clock")


@pytest.fixture(scope="session")
def host(request):
    return request.config.getoption("--host")


@pytest.fixture(scope="session")
def serial_port(request):
    return request.config.getoption("--port")


@pytest.fixture(scope="session")
def ble_name(request):
    return request.config.getoption("--ble-name")
