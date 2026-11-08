"""Publishes a Marea firmware version: builds it, updates the manifest on GitHub Pages (branch
gh-pages), where the watches and the app look for updates, and creates the GitHub release.

    https://carlosns.github.io/c6-fx-clock/manifest.json   what the app and the watch read
        boards.s3amoled  the watch's image (marea-ota.bin): version, url, size, sha256
        app              the Android app (it updates itself, comparing versionCode)
        top-level keys   the old ESP32-C6 clocks' image (2.1.0): NEVER touched, so no C6 ever tries to
                         install an image for another chip
    https://carlosns.github.io/c6-fx-clock/flasher/        the web installer (USB, first install)

    python tools/release.py --notes "Qué cambia"     # version from CMakeLists.txt (PROJECT_VER)
    python tools/release.py --dry-run                # build and show the manifest, publish nothing
    python tools/release.py --no-app --no-release    # just the watch image on Pages (tests, betas)

Publishing needs the owner's explicit OK every time (CLAUDE.md). Needs PlatformIO, the system Python
with esptool, the GitHub CLI logged in, and for the app JAVA_HOME (JDK 21) and
android/keystore.properties. The version must be new: watches only update to a different version.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = "CarlosNS/c6-fx-clock"
PAGES = "https://carlosns.github.io/c6-fx-clock"
OUT = os.path.join(ROOT, "release")
WEB = os.path.join(ROOT, "webflasher")
BOARD = "s3amoled"
OTA, FULL = "marea-ota.bin", "marea.bin"
APP_APK = "c6-clock-app.apk"  # fixed name on Pages: installed apps already know this URL
GH = shutil.which("gh") or os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "gh", "bin", "gh.exe")
PIO = shutil.which("pio") or os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts", "pio.exe")
APK = os.path.join(ROOT, "android", "app", "build", "outputs", "apk", "release", "app-release.apk")
# Old C6 clocks (up to 3.0.0) read at most 1023 bytes of manifest: a longer one would leave them unable
# to read even their own entry. Written compact and checked before publishing.
MANIFEST_MAX = 1000


def run(*cmd, cwd=ROOT, capture=False):
    print("$", " ".join(cmd))
    r = subprocess.run(cmd, cwd=cwd, check=True, text=True, capture_output=capture)
    return r.stdout if capture else None


def project_version():
    text = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    return re.search(r'set\(PROJECT_VER "([^"]+)"\)', text).group(1)


def app_version():
    """(versionName, versionCode) of the Android app, from android/app/build.gradle.kts."""
    text = open(os.path.join(ROOT, "android", "app", "build.gradle.kts"), encoding="utf-8").read()
    return re.search(r'versionName = "([^"]+)"', text).group(1), int(re.search(r"versionCode = (\d+)", text).group(1))


def entry(version, path, url_name):
    data = open(path, "rb").read()
    return {"version": version, "url": f"{PAGES}/{url_name}", "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def build(with_app):
    run(PIO, "run")
    run(sys.executable, "tools/build_webflasher.py")
    if with_app:
        gradlew = os.path.join(ROOT, "android", "gradlew.bat" if os.name == "nt" else "gradlew")
        run(gradlew, "assembleRelease", "--console=plain", cwd=os.path.join(ROOT, "android"))


def update_pages(version, notes, with_app, dry_run):
    """Edits the gh-pages tree in place: our entry, the app block and the installer; the rest stays."""
    tree = os.path.join(OUT, "gh-pages")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    run("git", "fetch", "origin", "gh-pages")
    run("git", "worktree", "add", "-f", tree, "origin/gh-pages", "--detach")
    try:
        path = os.path.join(tree, "manifest.json")
        m = json.load(open(path, encoding="utf-8"))
        e = entry(version, os.path.join(WEB, OTA), OTA)
        e["notes"] = notes
        e["date"] = datetime.date.today().isoformat()
        m.setdefault("boards", {})[BOARD] = e
        if with_app:
            name, code = app_version()
            m["app"] = {"version": name, "code": code, **{k: v for k, v in entry(name, APK, APP_APK).items() if k != "version"}}
        text = json.dumps(m, ensure_ascii=False, separators=(",", ":"))
        size = len(text.encode("utf-8"))
        print(json.dumps(m, ensure_ascii=False, indent=1))
        if size > MANIFEST_MAX:
            sys.exit(f"manifest.json would take {size} bytes, over {MANIFEST_MAX}: shorten --notes")
        if dry_run:
            print(f"--dry-run: {size} bytes; nothing published.")
            return
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        shutil.copy(os.path.join(WEB, OTA), tree)
        if with_app:
            shutil.copy(APK, os.path.join(tree, APP_APK))
        flasher = os.path.join(tree, "flasher")
        os.makedirs(flasher, exist_ok=True)
        for f in ("index.html", "manifest.json", FULL):
            shutil.copy(os.path.join(WEB, f), flasher)
        open(os.path.join(tree, ".nojekyll"), "w").close()
        run("git", "add", "-A", cwd=tree)
        run("git", "commit", "-m", f"Marea {version}", cwd=tree)
        run("git", "push", "origin", "HEAD:refs/heads/gh-pages", cwd=tree)
    finally:
        run("git", "worktree", "remove", "--force", tree)


def publish_release(version, notes, with_app):
    assets = [os.path.join(WEB, OTA), os.path.join(WEB, FULL)]
    if with_app:
        assets.append(APK)
    body = f"{notes}\n\nEl reloj se actualiza solo desde el móvil (Actualizaciones → Buscar actualización). " \
           f"Instalación por USB: {PAGES}/flasher/"
    run(GH, "release", "create", f"v{version}", *assets, "--repo", REPO, "--title", f"Marea {version}", "--notes", body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--notes", default="")
    ap.add_argument("--dry-run", action="store_true", help="build and show the manifest, publish nothing")
    ap.add_argument("--no-build", action="store_true", help="use the files already built")
    ap.add_argument("--no-app", action="store_true", help="leave the Android app out")
    ap.add_argument("--no-release", action="store_true", help="only GitHub Pages, no GitHub release")
    a = ap.parse_args()
    version = project_version()
    with_app = not a.no_app
    if not a.no_build:
        build(with_app)
    update_pages(version, a.notes, with_app, a.dry_run)
    if a.dry_run:
        return
    if not a.no_release:
        publish_release(version, a.notes, with_app)
    print(f"\nPublicada la {version}: {PAGES}/manifest.json (boards.{BOARD})")


if __name__ == "__main__":
    main()
