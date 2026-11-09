"""Packs the control page's pictures and fonts (src/web/*) into src/web.pack, which the firmware embeds
and serves at /a/<name> (src/web.c). PlatformIO runs it before every build (extra_scripts in
platformio.ini); it only rewrites the pack when something changed. The Android app copies src/web/
as it is (android/app/build.gradle.kts), so the pack is only for the watch.

Format (little-endian): b"MPK1", u32 count, then per file: u8 name length, name (UTF-8), u32 offset,
u32 size; then the file data. Offsets count from the start of the pack.

    python tools/web_pack.py      # by hand, also fine
"""
import os
import struct


def build(root):
    src = os.path.join(root, "src", "web")
    names = sorted(n for n in os.listdir(src) if os.path.isfile(os.path.join(src, n)) and not n.startswith("."))
    blobs = [open(os.path.join(src, n), "rb").read() for n in names]
    head = b"MPK1" + struct.pack("<I", len(names))
    size = len(head) + sum(1 + len(n.encode()) + 8 for n in names)
    table, off = b"", size
    for n, b in zip(names, blobs):
        e = n.encode()
        table += struct.pack("<B", len(e)) + e + struct.pack("<II", off, len(b))
        off += len(b)
    pack = head + table + b"".join(blobs)
    out = os.path.join(root, "src", "web.pack")
    if not os.path.exists(out) or open(out, "rb").read() != pack:
        open(out, "wb").write(pack)
        print(f"web_pack: {len(names)} files, {len(pack) // 1024} KB -> src/web.pack")
    return out


try:  # run by PlatformIO as an extra script
    Import("env")  # noqa: F821
    build(env["PROJECT_DIR"])  # noqa: F821
except NameError:
    if __name__ == "__main__":
        build(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
