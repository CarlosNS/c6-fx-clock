"""Screenshot of the clock over Bluetooth, saved as PNG (the Wi-Fi mode has GET /screenshot instead).

    python tools/ble_shot.py                       # what is on the screen now -> shot.png
    python tools/ble_shot.py --fx 22 --out clasico.png
    python tools/ble_shot.py --fx 0 6 21 --out caras.png --scale 2   # several faces in one image

If Windows is not paired with the clock yet, it pairs first: with --serial COM5 the passkey is read
from the clock's log, otherwise type the code the clock shows. Needs `pip install bleak pillow`.
"""
import argparse
import base64
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tests"))
from clocklib.ble import ClockLink  # noqa: E402


def grab(link, fx=None, settle=1.5):
    """Returns (width, height, RGB565 little-endian bytes) of the current screen."""
    if fx is not None:
        link.send(f"fx {fx}")
        time.sleep(settle)
    link.drain()
    link.send("shot")
    head = link.wait_for("shot", timeout=15)
    data = bytearray()
    while True:
        m = link.messages.get(timeout=20)
        if m.get("t") == "shotdata":
            data += base64.b64decode(m["d"])
        elif m.get("t") == "shotend":
            break
    return head["w"], head["h"], bytes(data)


def to_image(w, h, data):
    from PIL import Image
    img = Image.new("RGB", (w, h))
    px = img.load()
    for i in range(w * h):
        v = data[2 * i] | data[2 * i + 1] << 8
        px[i % w, i // w] = ((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
    return img


def ensure_paired(link, serial_port):
    try:
        link.request("get info", "info", timeout=4)
        return
    except AssertionError:
        pass
    if serial_port:
        from clocklib.board import SerialSession
        s = SerialSession(serial_port)  # note: opening the port reboots the clock
        s.read_until("Advertising as", 30)
        link = ClockLink(link.name_prefix).connect(link.address, retries=3)
        at = s.mark()
        status = link.pair_with_pin(lambda: s.read_until(r"passkey (\d{6})", 20, at).group(1))
        s.close()
    else:
        status = link.pair_with_pin(lambda: input("Clave que muestra el reloj: ").strip())
    print("Emparejado:", status)
    return link


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="C6 Clock")
    ap.add_argument("--address")
    ap.add_argument("--fx", type=int, nargs="*", help="faces to capture (default: the current one)")
    ap.add_argument("--out", default="shot.png")
    ap.add_argument("--scale", type=int, default=1)
    ap.add_argument("--serial", help="serial port to read the pairing passkey from (e.g. COM5)")
    a = ap.parse_args()
    link = ClockLink(a.name).connect(a.address, retries=2)
    link = ensure_paired(link, a.serial) or link
    before = link.request("get status", "status")["fx"]
    images = []
    for fx in a.fx or [None]:
        t0 = time.monotonic()
        w, h, data = grab(link, fx)
        print(f"esfera {fx if fx is not None else before}: {w}x{h} en {time.monotonic() - t0:.1f} s")
        images.append(to_image(w, h, data))
    if a.fx:
        link.send(f"fx {before}")
    link.close()
    from PIL import Image
    gap = 8
    out = Image.new("RGB", (sum(i.width for i in images) + gap * (len(images) - 1), max(i.height for i in images)), (255, 255, 255))
    x = 0
    for img in images:
        out.paste(img, (x, 0))
        x += img.width + gap
    if a.scale > 1:
        out = out.resize((out.width * a.scale, out.height * a.scale), Image.NEAREST)
    out.save(a.out)
    print("guardado:", a.out)


if __name__ == "__main__":
    main()
