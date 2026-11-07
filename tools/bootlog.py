"""Resets a board on its native USB port (USB-Serial-JTAG) and prints its boot log.

    python tools/bootlog.py [--port COM7] [--secs 10] [--out log.txt]

Opening the port alone does not always reset the S3. The reset is the RTS line, and on Windows the
usbser.sys driver only sends the new line state when DTR is written too, so the script rewrites DTR
after each RTS change (esptool does the same)."""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", default="COM7")
    ap.add_argument("--secs", type=float, default=10)
    ap.add_argument("--out")
    a = ap.parse_args()

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = a.port, 115200, 0.2
    s.dtr = s.rts = False
    s.open()

    def rts(level):
        s.rts = level
        s.dtr = s.dtr

    rts(True)
    time.sleep(0.1)
    rts(False)

    data = bytearray()
    end = time.time() + a.secs
    while time.time() < end:
        try:
            data += s.read(4096)
        except serial.SerialException:
            # the port can vanish for a moment while the chip resets
            s.close()
            time.sleep(0.3)
            try:
                s.open()
            except serial.SerialException:
                pass
    s.close()

    text = data.decode("utf-8", "replace")
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            f.write(text)
    sys.stdout.write(text)


if __name__ == "__main__":
    main()
