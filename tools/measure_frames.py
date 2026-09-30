"""Measure actual SurfaceFlinger presentation intervals; does not inspect game data."""
import argparse
import json
from pathlib import Path
import shlex
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    adb = [args.adb] + (["-s", args.serial] if args.serial else [])

    def shell(command):
        return subprocess.check_output(adb + ["shell", command], text=True)

    layers = shell("dumpsys SurfaceFlinger --list").splitlines()
    matches = [layer for layer in layers if "SurfaceView[org.supermetroid.thor/" in layer and "(BLAST)" in layer]
    if len(matches) != 1:
        raise RuntimeError("Open the Thor game surface first; expected exactly one game layer")
    command = "dumpsys SurfaceFlinger --latency " + shlex.quote(matches[0])
    timestamps = set()
    start = time.monotonic()
    period = 0
    while time.monotonic() - start < args.seconds:
        lines = shell(command).splitlines()
        if lines:
            period = int(lines[0])
        for line in lines[1:]:
            fields = line.split()
            if len(fields) == 3:
                presented = int(fields[1])
                if 0 < presented < 2**63 - 1:
                    timestamps.add(presented)
        time.sleep(.4)
    ordered = sorted(timestamps)
    intervals = [(b - a) / 1e6 for a, b in zip(ordered, ordered[1:])]
    if not intervals or not period:
        raise RuntimeError("SurfaceFlinger returned no usable presentation timestamps")
    # First batch includes at most ~1 second of history. Report sampled duration.
    sorted_intervals = sorted(intervals)
    result = {
        "scope": "current room only; not the complete-game performance acceptance gate",
        "nominal_refresh_hz": 1e9 / period,
        "sample_seconds": (ordered[-1] - ordered[0]) / 1e9,
        "presented_frames": len(ordered),
        "effective_fps": 1000 / statistics.mean(intervals),
        "median_interval_ms": statistics.median(intervals),
        "p99_interval_ms": sorted_intervals[min(len(intervals) - 1, int(len(intervals) * .99))],
        "worst_interval_ms": max(intervals),
        "intervals_within_8_333_ms_percent": 100 * sum(i <= 8.333333 for i in intervals) / len(intervals),
        "intervals_over_1_5_nominal_refresh": sum(i > period / 1e6 * 1.5 for i in intervals),
    }
    output = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
