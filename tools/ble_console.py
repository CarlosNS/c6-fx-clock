"""Bluetooth console for the clock: type commands, see every message it sends back.

    python tools/ble_console.py                 # finds "C6 Clock XXXX"
    python tools/ble_console.py --cmd "get info" --cmd "fx 3"   # one-shot

Commands are the same as the web page / serial console (src/control.h) plus "get info",
"get status", "get faces" and "sync done". Needs a Bluetooth adapter and `pip install bleak`.
"""
import argparse
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tests"))
from clocklib.ble import ClockLink  # noqa: E402


def printer(link, stop):
    while not stop.is_set():
        try:
            m = link.messages.get(timeout=0.3)
        except Exception:
            continue
        text = json.dumps(m, ensure_ascii=False)
        print(f"< {text[:600]}{' ...' if len(text) > 600 else ''}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="C6 Clock")
    ap.add_argument("--address")
    ap.add_argument("--cmd", action="append", help="send and exit (repeatable)")
    a = ap.parse_args()
    link = ClockLink(a.name).connect(a.address)
    print(f"Conectado a {link.address} (MTU {link.mtu}). Escribe comandos; línea vacía para salir.")
    stop = threading.Event()
    threading.Thread(target=printer, args=(link, stop), daemon=True).start()
    try:
        if a.cmd:
            for c in a.cmd:
                print(f"> {c}")
                link.send(c)
            time.sleep(2)
        else:
            while True:
                line = input()
                if not line:
                    break
                link.send(line.replace("\\n", "\n"))
    finally:
        stop.set()
        link.close()


if __name__ == "__main__":
    main()
