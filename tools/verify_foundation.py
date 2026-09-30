"""Run Thor foundation instrumentation and controlled 60/120 Hz cadence captures.

Uses a previously imported ROM. Temporarily changes Android min/peak refresh
settings, restoring them even on failure. ROMs and generated artifacts stay private.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--output", type=Path, default=Path("build/m1"))
    parser.add_argument("--skip-tests", action="store_true", help="Only repeat cadence captures")
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    adb = [args.adb] + (["-s", args.serial] if args.serial else [])

    def shell(command):
        return subprocess.check_output(adb + ["shell", command], text=True, encoding="utf-8", timeout=180)

    shell("input keyevent KEYCODE_WAKEUP")
    if not args.skip_tests:
        for apk in ("app/build/outputs/apk/debug/app-debug.apk",
                    "app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk"):
            subprocess.run(adb + ["install", "-r", str(root / "android" / apk)], check=True)
        print("Running Android window, display and controller contracts...", flush=True)
        tests = shell("am instrument -w -r -e class org.supermetroid.thor.FoundationDeviceTest "
                      "org.supermetroid.thor.test/androidx.test.runner.AndroidJUnitRunner")
        (output / "instrumentation.txt").write_text(tests, encoding="utf-8")
        print(tests, flush=True)
        if "OK (7 tests)" not in tests or "FAILURES" in tests:
            raise RuntimeError("Foundation instrumentation failed; see instrumentation.txt")

    settings = {key: shell("settings get system " + key).strip()
                for key in ("min_refresh_rate", "peak_refresh_rate")}
    results = []
    try:
        for hz in (60, 120):
            print(f"Capturing {hz} Hz firmware mode for {args.seconds:g} seconds...", flush=True)
            shell(f"settings put system min_refresh_rate {hz}")
            shell(f"settings put system peak_refresh_rate {hz}")
            shell("am force-stop org.supermetroid.thor")
            shell(f"am start -n org.supermetroid.thor/.MainActivity --ei development_hz {hz}")
            time.sleep(3)
            # Clear only this process's diagnostic scope via the timestamp filter;
            # keep system logs untouched. Raw display state permits mode verification.
            (output / f"display-{hz}.txt").write_text(shell("dumpsys display"), encoding="utf-8")
            command = [sys.executable, str(root / "tools/measure_frames.py"), "--adb", args.adb,
                       "--seconds", str(args.seconds), "--expected-hz", str(hz),
                       "--label", f"foundation-firmware-{hz}", "--output", str(output / f"cadence-{hz}.json")]
            if args.serial:
                command.extend(["--serial", args.serial])
            subprocess.run(command, check=True)
            metrics = json.loads((output / f"cadence-{hz}.json").read_text(encoding="utf-8"))
            if abs(metrics["nominal_refresh_hz"] - hz) > 1:
                raise RuntimeError(f"Firmware did not enter {hz} Hz; capture is not that mode")
            pid = shell("pidof org.supermetroid.thor").strip()
            logs = shell("logcat -d -t 3000 --pid=" + shlex.quote(pid) + " -s ThorNative:I AndroidRuntime:E")
            (output / f"host-{hz}.txt").write_text(logs, encoding="utf-8")
            results.append({k: v for k, v in metrics.items() if k != "presentation_timestamps_ns"})
    finally:
        for key, value in settings.items():
            shell(f"settings delete system {key}" if value == "null"
                  else "settings put system " + key + " " + shlex.quote(value))
        shell("am force-stop org.supermetroid.thor")
        shell("am start -n org.supermetroid.thor/.MainActivity")
    (output / "summary.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print("Foundation captures saved; original system refresh settings restored.")


if __name__ == "__main__":
    main()
