"""Drives Edge (or Chrome) through the DevTools protocol, so the control page can be tested over Web
Bluetooth without a person: DeviceAccess answers the Bluetooth device chooser the page opens.

    with Browser() as b:
        b.open("http://127.0.0.1:8765/src/index.html")
        b.click("#blego", choose=lambda d: d["name"].startswith("Marea"))
        b.eval("document.getElementById('dot').className")
"""
import asyncio
import functools
import http.server
import itertools
import json
import os
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.request

import websockets

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
]


def find_browser():
    return next((p for p in BROWSERS if os.path.exists(p)), None)


def _free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def serve_repo():
    """Serves the repository on http://127.0.0.1:<port>/ (localhost is a secure origin for Web Bluetooth)."""
    port = _free_port()
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=ROOT)
    handler.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, f"http://127.0.0.1:{port}"


class Browser:
    def __init__(self, exe=None):
        self.exe = exe or find_browser()
        self.loop = asyncio.new_event_loop()
        self.ids = itertools.count(1)
        self.events = []

    def __enter__(self):
        self.profile = tempfile.mkdtemp(prefix="c6web-")
        self.port = _free_port()
        self.proc = subprocess.Popen([self.exe, f"--remote-debugging-port={self.port}", f"--user-data-dir={self.profile}",
                                      "--no-first-run", "--no-default-browser-check", "--window-size=800,900", "about:blank"])
        for _ in range(100):
            try:
                tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{self.port}/json/list", timeout=1))
                page = next(t for t in tabs if t["type"] == "page")
                break
            except Exception:
                time.sleep(0.2)
        else:
            raise RuntimeError("the browser did not start")
        self.ws = self._run(websockets.connect(page["webSocketDebuggerUrl"], max_size=None))
        for domain in ("Page", "Runtime", "DeviceAccess"):
            self.call(f"{domain}.enable")
        return self

    def __exit__(self, *exc):
        try:
            self._run(self.ws.close())
        finally:
            self.proc.terminate()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            shutil.rmtree(self.profile, ignore_errors=True)

    def _run(self, coro, timeout=60):
        return self.loop.run_until_complete(asyncio.wait_for(coro, timeout))

    async def _call(self, method, params):
        mid = next(self.ids)
        await self.ws.send(json.dumps({"id": mid, "method": method, "params": params}))
        while True:
            msg = json.loads(await self.ws.recv())
            if msg.get("id") == mid:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
            self.events.append(msg)

    def call(self, method, **params):
        return self._run(self._call(method, params))

    def event(self, method, match=lambda p: True, timeout=30):
        """Returns the params of the first event `method` (already received or new) that `match` accepts."""
        async def wait():
            while True:
                for i, e in enumerate(self.events):
                    if e.get("method") == method and match(e["params"]):
                        return self.events.pop(i)["params"]
                self.events.append(json.loads(await self.ws.recv()))
        return self._run(wait(), timeout)

    def open(self, url):
        self.call("Page.navigate", url=url)
        self.event("Page.loadEventFired")

    def eval(self, expr, gesture=False):
        """Evaluates in the page (awaiting promises); gesture=True counts as a user tap."""
        r = self.call("Runtime.evaluate", expression=expr, awaitPromise=True, returnByValue=True, userGesture=gesture)
        if "exceptionDetails" in r:
            raise RuntimeError(r["exceptionDetails"].get("exception", {}).get("description") or r["exceptionDetails"]["text"])
        return r["result"].get("value")

    def click(self, selector, choose=None, timeout=30):
        """Taps an element; with `choose`, answers the Bluetooth chooser it opens with the first device
        (dict with id and name) `choose` accepts."""
        self.eval(f"document.querySelector({json.dumps(selector)}).click()", gesture=True)
        if not choose:
            return None
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            p = self.event("DeviceAccess.deviceRequestPrompted", timeout=timeout)
            dev = next((d for d in p["devices"] if choose(d)), None)
            if dev:
                self.call("DeviceAccess.selectPrompt", id=p["id"], deviceId=dev["id"])
                return dev
        raise AssertionError("the clock never showed up in the Bluetooth chooser")

    def wait_until(self, expr, timeout=30, every=0.25):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            v = self.eval(expr)
            if v:
                return v
            time.sleep(every)
        raise AssertionError(f"timed out waiting for: {expr}")
