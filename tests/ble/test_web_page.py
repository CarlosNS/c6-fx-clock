"""Phase 5a: the control page over Web Bluetooth (docs/PLAN-bluetooth.md, 3.5), in a real Edge/Chrome
with the PC's Bluetooth adapter. The browser is driven through DevTools (clocklib/browser.py), which
also answers its device chooser. Windows must share a bond with the clock first: the `link` fixture
pairs with the code from the serial log, and the browser then uses that bond. While the browser holds
the link, the clock is checked through its serial console (`status`).
"""
import time

import pytest

from clocklib import fx_live
from clocklib.browser import Browser, find_browser, serve_repo

pytestmark = [pytest.mark.phase5, pytest.mark.skipif(not find_browser(), reason="needs Edge or Chrome")]

DOT_ON = "document.getElementById('dot').classList.contains('on')"


@pytest.fixture(scope="module")
def page(link, ble_name, ble_serial):
    srv, base = serve_repo()
    before = ble_serial.status()
    link.close()  # a connected clock stops advertising: the chooser would never list it
    with Browser() as b:
        b.open(f"{base}/src/index.html")
        assert b.eval("!document.getElementById('blecard').classList.contains('hide')"), "no Bluetooth card"
        t0 = time.monotonic()
        b.click("#blego", choose=lambda d: d["name"].startswith(ble_name))
        b.wait_until(DOT_ON, 30)
        b.connect_s = time.monotonic() - t0
        yield b
    srv.shutdown()
    for _ in range(10):  # the last test reboots the clock: commands sent while it starts are lost
        try:
            st = ble_serial.status()
            if st["bri"] == before["bri"] and st["fx"] == before["fx"]:
                break
        except AssertionError:
            pass
        ble_serial.send(f"bri {before['bri']}")
        ble_serial.send(f"fx {before['fx']}")
        time.sleep(1)
    else:
        print("\n!! Could not restore brightness and face after the web page test")


def test_page_connects_and_loads_everything(page):
    assert page.connect_s < 20
    page.wait_until(f"document.querySelectorAll('.fx .pick').length === {len(fx_live())}", 20)
    page.wait_until("document.getElementById('cname').textContent !== '--'", 15)
    assert page.eval("document.getElementById('blecard').classList.contains('hide')")
    assert page.eval("document.getElementById('wifionly').classList.contains('hide')"), "OTA upload shown over Bluetooth"
    assert page.eval("document.getElementById('tomode').textContent") == "Pasar a WiFi"


def test_clock_time_ticks(page):
    page.wait_until("/^\\d\\d:\\d\\d:\\d\\d$/.test(document.getElementById('clk').textContent)", 15)
    a = page.eval("document.getElementById('clk').textContent")
    time.sleep(2.2)
    assert page.eval("document.getElementById('clk').textContent") != a


def test_slider_reaches_the_clock(page, ble_serial):
    page.eval("(()=>{const e=document.getElementById('bri');e.value=37;e.dispatchEvent(new Event('input'))})()", gesture=True)
    time.sleep(1.5)
    assert ble_serial.status()["bri"] == 37


def test_change_on_the_clock_shows_on_the_page(page, ble_serial):
    time.sleep(2)  # the page ignores the clock for 1.5 s after the user touches something
    ble_serial.send("bri 64")
    page.wait_until("document.getElementById('bri').value === '64'", 10)  # pushed by the clock, no polling needed


def test_face_button_changes_the_face(page, ble_serial):
    page.eval("document.querySelectorAll('.fx .pick')[3].click()", gesture=True)
    time.sleep(1.5)
    assert ble_serial.status()["fx"] == 3
    assert page.eval("document.querySelectorAll('.fx .pick')[3].parentElement.classList.contains('on')")


def test_faces_can_be_left_out_of_the_automatic_rotation(page, ble_serial):
    page.eval("document.querySelectorAll('.fx .eye')[4].click()", gesture=True)  # the eye toggles, it does not select
    time.sleep(1.5)
    assert ble_serial.status()["fxoff"] == 1 << 4
    assert page.eval("document.querySelectorAll('.fx .fc')[4].classList.contains('off')")
    assert " de " in page.eval("document.getElementById('offinfo').textContent")
    page.eval("document.querySelectorAll('.fx .eye')[4].click()", gesture=True)  # back in
    time.sleep(1.5)
    assert ble_serial.status()["fxoff"] == 0


def test_page_reconnects_after_a_drop(page, ble_serial):
    at = ble_serial.mark()
    ble_serial.send("mode ble")  # the clock restarts: the link drops
    page.wait_until(f"!{DOT_ON}", 15)
    ble_serial.wait_advertising(at, 30)
    t0 = time.monotonic()
    page.wait_until(DOT_ON, 40)
    print(f"reconnected {time.monotonic() - t0:.1f} s after the clock came back")
