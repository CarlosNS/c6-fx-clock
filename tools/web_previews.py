"""Takes the previews the app shows (src/web/fxN.webp for each face, alarmN.webp for each alarm
animation) from a real watch: USB on COM7 for the console, the watch's Wi-Fi for the screenshots.
Run it after adding a face or an alarm animation, then rebuild (the firmware packs src/web/ through
tools/web_pack.py; the app copies it).

    python tools/web_previews.py                # everything
    python tools/web_previews.py --faces 25     # only face 25
    python tools/web_previews.py --alarms       # only the alarm animations
    python tools/web_previews.py --port COM7

The watch restarts when the session opens (as in the tests). Its face, automatic change, alarm 0 and
Wi-Fi switch are put back as they were. The alarm previews need at least one alarm on the watch.
"""
import argparse
import io
import json
import os
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tests"))
from clocklib.board import SerialSession  # noqa: E402
from PIL import Image  # noqa: E402

OUT = os.path.join(ROOT, "src", "web")
SIZE = (274, 336)  # the wide canvas: what most faces draw; native 410x502 frames are scaled to it


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM7")
    ap.add_argument("--faces", type=int, nargs="*", help="only these face numbers")
    ap.add_argument("--alarms", action="store_true", help="only the alarm animations")
    a = ap.parse_args()
    s = SerialSession(a.port)
    s.read_until(r"Advertising as|Quiet for", 40)

    def cmd(c, wait=0.5):
        s.send(c)
        end = time.time() + wait
        while time.time() < end:
            s._read()

    m = s.mark()
    cmd("wifi 1", 0.3)
    ip = s.read_until(r"Connected to \S+, open http://([\d.]+)", 60, m).group(1)
    get = lambda p, t=20: urllib.request.urlopen(f"http://{ip}{p}", timeout=t).read()
    st = json.loads(get("/status"))
    shot = lambda q="": Image.open(io.BytesIO(get("/screenshot" + q, 30))).convert("RGB").resize(SIZE, Image.LANCZOS)
    save = lambda im, name: (im.save(os.path.join(OUT, name), "WEBP", quality=78, method=6), print("  ", name))
    try:
        if not a.alarms:
            cmd("auto 0")
            faces = json.loads(get("/faces"))["faces"]
            for i, f in enumerate(faces):
                if f.get("x") or (a.faces and i not in a.faces):
                    continue
                save(shot(f"?fx={i}&wait=2500"), f"fx{i}.webp")
        if a.alarms or not a.faces:
            al = json.loads(get("/alarms"))
            if not al["list"]:
                print("No alarms on the watch: create one to take the alarm previews")
            else:
                a0 = al["list"][0]
                cmd("alarm 0 label")  # no label: the ringing screen says ALARMA
                for k in range(len(al["anims"])):
                    cmd(f"alarm 0 anim {k}")
                    cmd("alarm 0 test", 3.5)
                    save(shot(), f"alarm{k}.webp")
                    cmd("ui tap 320 390", 3.5)  # Stop
                cmd(f"alarm 0 anim {a0['a']}")
                cmd("alarm 0 label " + a0["l"])
    finally:
        cmd(f"fx {st['fx']}")
        cmd(f"auto {st.get('auto', 0)}")
        cmd(f"wifi {st.get('wifi', 0)}")
        s.close()


if __name__ == "__main__":
    main()
