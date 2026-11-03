"""Phase 5b/5c: the Android app on the real phone, driven with adb (docs/PLAN-bluetooth.md, 3.5-3.6).

Needs the phone on USB with USB debugging on, the clock in Bluetooth mode and the app already paired
with it (the code on the clock's screen is typed by a person once: tests/MANUAL.md). Skipped otherwise.
The JVM unit tests of the app run with `gradlew test` (android/), not here.
"""
import os
import re
import shutil
import subprocess
import time

import pytest

pytestmark = pytest.mark.phase5

PKG = "io.github.carlosns.c6clock"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
# The signed release build is what people install; debug and release have different signatures and
# can't replace each other on the phone, so use release whenever it exists
APKS = [os.path.join(ROOT, "android", "app", "build", "outputs", "apk", b, f"app-{b}.apk") for b in ("release", "debug")]
APK = next((p for p in APKS if os.path.exists(p)), APKS[-1])


def adb_path():
    sdk = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk", "platform-tools", "adb.exe")
    return shutil.which("adb") or (sdk if os.path.exists(sdk) else None)


def adb(*args, timeout=60):
    return subprocess.run([adb_path(), *args], capture_output=True, text=True, timeout=timeout, encoding="utf-8").stdout


@pytest.fixture(scope="module")
def phone():
    if not adb_path():
        pytest.skip("no adb (Android SDK platform-tools)")
    devices = [l.split()[0] for l in adb("devices").splitlines()[1:] if l.strip().endswith("device")]
    if not devices:
        pytest.skip("no phone on USB with debugging on")
    if not os.path.exists(APK):
        pytest.skip("build the app first: android/gradlew assembleDebug")
    assert "Success" in adb("install", "-r", APK, timeout=180)
    return devices[0]


def logcat_since(mark, tag):
    return [l for l in adb("logcat", "-d", "-T", mark, "-s", f"{tag}:*").splitlines() if tag in l]


def now_mark():
    # one string: adb joins the arguments and the phone's shell would split the format at the space
    return adb("shell", "date '+%m-%d %H:%M:%S.000'").strip()


def test_app_starts_and_shows_the_page(phone):
    adb("shell", "am", "force-stop", PKG)
    adb("shell", "am", "start", "-W", "-n", f"{PKG}/.MainActivity")
    time.sleep(3)
    top = adb("shell", "dumpsys", "activity", "activities")
    assert f"{PKG}/.MainActivity" in top


def test_sync_reaches_the_clock(phone):
    mark = now_mark()
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity", "--ez", "sync", "true")
    t0 = time.monotonic()
    while time.monotonic() - t0 < 60:
        lines = logcat_since(mark, "SyncWorker")
        done = [l for l in lines if "sync ok" in l or "sync failed" in l]
        if done:
            assert "sync ok" in done[-1], done[-1]
            assert re.search(r"sync ok: \d+\.\d+\.\d+", done[-1])
            return
        time.sleep(2)
    pytest.fail("no sync within 60 s (is the app paired with the clock?)")


def test_periodic_sync_is_scheduled(phone):
    jobs = adb("shell", "dumpsys", "jobscheduler")
    assert PKG in jobs and "SystemJobService" in jobs, "WorkManager has no job for the app"
