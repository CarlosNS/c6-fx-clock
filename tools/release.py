"""Publishes a firmware version (docs/PLAN-bluetooth.md, 3.4): builds it, writes the update manifest
and puts everything on GitHub Pages (branch gh-pages), where the clocks look for updates:

    https://carlosns.github.io/c6-fx-clock/manifest.json       what the app and the clock read
    https://carlosns.github.io/c6-fx-clock/c6-fx-clock-ota.bin the C6 image (top-level manifest keys)
    https://carlosns.github.io/c6-fx-clock/s3-fx-clock-ota.bin the S3 image (manifest boards.s3amoled)
    https://carlosns.github.io/c6-fx-clock/c6-clock-app.apk    the Android app (it updates itself)
    https://carlosns.github.io/c6-fx-clock/flasher/            the web installer (USB, first install)

and creates the GitHub release vX.Y.Z with the images and the Android app.

    python tools/release.py --notes "Qué cambia"          # version from CMakeLists.txt (PROJECT_VER)
    python tools/release.py --dry-run                     # build and write release/ only

Needs PlatformIO, the GitHub CLI logged in, and for the app JAVA_HOME (JDK 21) and
android/keystore.properties. The version must be new: clocks only update to a different version.
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
APP_APK = "c6-clock-app.apk"  # fixed name on Pages, so the manifest URL never changes
GH = shutil.which("gh") or os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "gh", "bin", "gh.exe")
PIO = shutil.which("pio") or os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts", "pio.exe")
# Clocks up to 3.0.0 read at most 1023 bytes of manifest (src/update.c): a longer one would leave every
# installed C6 unable to update. Written compact, and checked before anything is published.
MANIFEST_MAX = 1000
# Boards besides the C6 (whose image stays in the manifest's top-level keys): env -> OTA image name
OTHER_BOARDS = {"s3amoled": "s3-fx-clock-ota.bin"}


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


def image_entry(version, path):
    data = open(path, "rb").read()
    return {"version": version, "url": f"{PAGES}/{os.path.basename(path)}", "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def manifest(version, notes, ota, apk=None, others=None):
    m = {"project": "c6-fx-clock", **image_entry(version, ota), "notes": notes,
         "date": datetime.date.today().isoformat()}
    if others:  # {board id: OTA image path}
        m["boards"] = {b: image_entry(version, p) for b, p in others.items()}
    if apk:  # the app updates itself from here too (it compares versionCode)
        name, code = app_version()
        blob = open(apk, "rb").read()
        m["app"] = {"version": name, "code": code, "url": f"{PAGES}/{APP_APK}", "size": len(blob),
                    "sha256": hashlib.sha256(blob).hexdigest()}
    return m


def build(with_app):
    run(PIO, "run", "-e", "c6lcd")
    for env in OTHER_BOARDS:
        run(PIO, "run", "-e", env)
    run(sys.executable, "tools/build_webflasher.py")
    if with_app:
        gradlew = os.path.join(ROOT, "android", "gradlew.bat" if os.name == "nt" else "gradlew")
        run(gradlew, "assembleRelease", "--console=plain", cwd=os.path.join(ROOT, "android"))


def stage(version, notes, with_app):
    """release/: pages/ is the gh-pages tree, assets/ goes to the GitHub release."""
    shutil.rmtree(OUT, ignore_errors=True)
    pages, assets = os.path.join(OUT, "pages"), os.path.join(OUT, "assets")
    os.makedirs(os.path.join(pages, "flasher"))
    os.makedirs(assets)
    web = os.path.join(ROOT, "webflasher")
    ota = os.path.join(web, "c6-fx-clock-ota.bin")
    apk = os.path.join(ROOT, "android", "app", "build", "outputs", "apk", "release", "app-release.apk") if with_app else None
    others = {b: os.path.join(web, name) for b, name in OTHER_BOARDS.items()}
    for path in others.values():
        if not os.path.exists(path):
            sys.exit(f"Missing {path}: build every board (without --no-build) first")
    m = manifest(version, notes, ota, apk, others)
    text = json.dumps(m, ensure_ascii=False, separators=(",", ":"))
    if len(text.encode("utf-8")) > MANIFEST_MAX:
        sys.exit(f"manifest.json takes {len(text.encode('utf-8'))} bytes, over {MANIFEST_MAX}: installed C6 clocks "
                 "could not read it. Shorten --notes.")
    full_images = ["c6-fx-clock.bin"] + [n.replace("-ota", "") for n in OTHER_BOARDS.values()]
    for path in [ota, *others.values()]:
        shutil.copy(path, pages)
        shutil.copy(path, assets)
    if apk:
        shutil.copy(apk, os.path.join(pages, APP_APK))
    for f in ["index.html", "manifest.json", *full_images]:
        shutil.copy(os.path.join(web, f), os.path.join(pages, "flasher"))
    with open(os.path.join(pages, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    open(os.path.join(pages, ".nojekyll"), "w").close()
    for f in full_images:
        shutil.copy(os.path.join(web, f), assets)
    if apk:
        shutil.copy(apk, os.path.join(assets, f"c6-clock-app-{m['app']['version']}.apk"))
    return m


def publish_pages(version):
    """Replaces the gh-pages branch content with release/pages (one commit per release)."""
    tree = os.path.join(OUT, "gh-pages")
    remote = run("git", "ls-remote", "--heads", "origin", "gh-pages", capture=True).strip()
    if remote:
        run("git", "fetch", "origin", "gh-pages")
        run("git", "worktree", "add", "-f", tree, "origin/gh-pages", "--detach")
    else:
        run("git", "worktree", "add", "-f", "--orphan", "-b", "gh-pages", tree)
    try:
        for name in os.listdir(tree):
            if name != ".git":
                p = os.path.join(tree, name)
                shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
        shutil.copytree(os.path.join(OUT, "pages"), tree, dirs_exist_ok=True)
        run("git", "add", "-A", cwd=tree)
        run("git", "commit", "-m", f"Firmware {version}", cwd=tree)
        run("git", "push", "origin", "HEAD:refs/heads/gh-pages", cwd=tree)
    finally:
        run("git", "worktree", "remove", "--force", tree)
    if subprocess.run([GH, "api", f"repos/{REPO}/pages"], capture_output=True).returncode != 0:
        run(GH, "api", "-X", "POST", f"repos/{REPO}/pages", "-f", "source[branch]=gh-pages", "-f", "source[path]=/")


def publish_release(version, notes):
    assets = [os.path.join(OUT, "assets", f) for f in sorted(os.listdir(os.path.join(OUT, "assets")))]
    body = f"{notes}\n\nLos relojes con la app se actualizan solos desde el móvil (Actualizaciones → Buscar actualización). " \
           f"Instalación por USB: {PAGES}/flasher/"
    run(GH, "release", "create", f"v{version}", *assets, "--repo", REPO, "--title", f"FX Clock {version}", "--notes", body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--notes", default="")
    ap.add_argument("--dry-run", action="store_true", help="build and stage in release/, publish nothing")
    ap.add_argument("--no-build", action="store_true", help="use the files already built")
    ap.add_argument("--no-app", action="store_true", help="leave the Android app out of the release")
    a = ap.parse_args()
    version = project_version()
    with_app = not a.no_app
    if not a.no_build:
        build(with_app)
    m = stage(version, a.notes, with_app)
    print(json.dumps(m, ensure_ascii=False, indent=1))
    if a.dry_run:
        print(f"\nPreparado en {OUT}; no se ha publicado nada (--dry-run).")
        return
    publish_pages(version)
    publish_release(version, a.notes)
    print(f"\nPublicada la {version}: {PAGES}/manifest.json")


if __name__ == "__main__":
    main()
