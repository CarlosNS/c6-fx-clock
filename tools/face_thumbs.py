"""Makes the face picker's thumbnails (src/thumbs/fxN.jpg, 200x245 JPEG) from the app's previews
(src/web/fxN.webp). tools/web_previews.py calls it after taking new previews; run it by hand after
changing a preview some other way. tools/web_pack.py packs src/thumbs/ into web.pack as thumb/<name>,
and the picker (src/app_faces.c) decodes them with TJpgDec.

    python tools/face_thumbs.py          # all of them
    python tools/face_thumbs.py 25 27    # only these faces
"""
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "web")
OUT = os.path.join(ROOT, "src", "thumbs")
SIZE = (200, 245)  # the card in the middle of the picker (app_faces.c CW x CH)


def build(faces=None):
    os.makedirs(OUT, exist_ok=True)
    n = 0
    for name in sorted(os.listdir(SRC)):
        m = re.fullmatch(r"fx(\d+)\.webp", name)
        if not m or (faces and int(m.group(1)) not in faces):
            continue
        im = Image.open(os.path.join(SRC, name)).convert("RGB").resize(SIZE, Image.LANCZOS)
        # baseline, 4:2:0 and no restart markers: what TJpgDec in the ROM decodes
        im.save(os.path.join(OUT, f"fx{m.group(1)}.jpg"), quality=88, optimize=True, progressive=False)
        n += 1
    print(f"face_thumbs: {n} thumbnails -> src/thumbs/")


if __name__ == "__main__":
    build({int(a) for a in sys.argv[1:]} or None)
