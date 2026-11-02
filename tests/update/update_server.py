"""Local update server for the phase 6 tests: serves manifests and firmware images over HTTP on the
LAN, one manifest per scenario. Test builds of the clock accept http:// (production requires https).

    python tests/update/update_server.py FIRMWARE.bin [--port 8770]

Scenarios (GET /<name>.json):
  good       newer version, correct sha256           -> updated
  same       the version the clock already runs      -> up_to_date
  badsha     newer version, wrong sha256             -> bad_image (nothing activated)
  truncated  newer version, file cut in half         -> bad_image
  foreign    an image of another project             -> bad_image
  slow       good image sent at ~2 KB/s              -> timeout (5 min limit)
"""
import argparse
import hashlib
import http.server
import json
import socket
import threading
import time

ESP_APP_DESC_OFFSET = 0x20  # image header (24) + first segment header (8)
VERSION_OFFSET = ESP_APP_DESC_OFFSET + 16  # esp_app_desc_t.version (char[32])
PROJECT_OFFSET = VERSION_OFFSET + 32       # esp_app_desc_t.project_name (char[32])


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.168.1.1", 80))
        return s.getsockname()[0]
    finally:
        s.close()


def image_version(img: bytes):
    return img[VERSION_OFFSET:VERSION_OFFSET + 32].split(b"\0")[0].decode()


def patched(img: bytes, offset: int, text: str):
    """Rewrites a string field of the app description (the image then fails its own checksum,
    which is fine: the clock must reject it either way)."""
    b = bytearray(img)
    field = text.encode()[:31].ljust(32, b"\0")
    b[offset:offset + 32] = field
    return bytes(b)


class Server:
    def __init__(self, firmware_path, port=8770, newer="9.9.9"):
        self.port = port
        self.host = lan_ip()
        good = open(firmware_path, "rb").read()
        self.running_version = image_version(good)
        self.files = {
            "good.bin": good,
            "truncated.bin": good[: len(good) // 2],
            "foreign.bin": patched(good, PROJECT_OFFSET, "another-project"),
        }
        base = f"http://{self.host}:{port}"

        def manifest(version, name, sha=None, size=None):
            data = self.files[name]
            return {"version": version, "url": f"{base}/{name}", "size": size or len(data),
                    "sha256": sha or hashlib.sha256(data).hexdigest(), "notes": "test"}

        # "newer" is only claimed by the manifest; the image keeps its real version, so after a
        # successful test update the clock runs the same firmware it started with.
        self.manifests = {
            "good": manifest(newer, "good.bin"),
            "same": manifest(self.running_version, "good.bin"),
            "badsha": manifest(newer, "good.bin", sha="0" * 64),
            "truncated": manifest(newer, "truncated.bin", size=len(good)),
            "foreign": manifest(newer, "foreign.bin"),
            "slow": manifest(newer, "good.bin") | {"url": f"{base}/slow/good.bin"},
        }
        self.hits = []

    def url(self, scenario):
        return f"http://{self.host}:{self.port}/{scenario}.json"

    def start(self):
        srv = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                srv.hits.append(self.path)
                path = self.path.lstrip("/")
                slow = path.startswith("slow/")
                path = path.removeprefix("slow/")
                if path.endswith(".json") and path[:-5] in srv.manifests:
                    body = json.dumps(srv.manifests[path[:-5]]).encode()
                    ctype = "application/json"
                elif path in srv.files:
                    body, ctype = srv.files[path], "application/octet-stream"
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                step = 2048
                for i in range(0, len(body), step):
                    self.wfile.write(body[i:i + step])
                    if slow:
                        time.sleep(1)

        self.httpd = http.server.ThreadingHTTPServer(("0.0.0.0", self.port), Handler)
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        return self

    def stop(self):
        self.httpd.shutdown()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("firmware")
    ap.add_argument("--port", type=int, default=8770)
    a = ap.parse_args()
    s = Server(a.firmware, a.port).start()
    for k in s.manifests:
        print(f"{k:10s} {s.url(k)}")
    print("Ctrl+C para parar")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        s.stop()
