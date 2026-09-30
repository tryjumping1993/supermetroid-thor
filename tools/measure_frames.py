"""Measure actual SurfaceFlinger presentation intervals; does not inspect game data."""
import argparse
import json
from pathlib import Path
import shlex
import statistics
import subprocess
import time


def summarize(timestamps, period, expected_hz=120):
    """Keep the absolute budget and measured cadence as separate results."""
    ordered = sorted(set(timestamps))
    intervals = [(b - a) / 1e6 for a, b in zip(ordered, ordered[1:])]
    if not intervals or period <= 0:
        raise RuntimeError("SurfaceFlinger returned no usable presentation timestamps")
    median = statistics.median(intervals)
    sorted_intervals = sorted(intervals)
    return {
        "scope": "current room only; not the complete-game performance acceptance gate",
        "requested_refresh_hz": expected_hz,
        "nominal_refresh_hz": 1e9 / period,
        "sample_seconds": (ordered[-1] - ordered[0]) / 1e9,
        "presented_frames": len(ordered),
        "effective_fps": 1000 / statistics.mean(intervals),
        "median_interval_ms": median,
        "p99_interval_ms": sorted_intervals[min(len(intervals) - 1, int(len(intervals) * .99))],
        "worst_interval_ms": max(intervals),
        "intervals_within_8_333_ms_percent": 100 * sum(i <= 1000 / 120 for i in intervals) / len(intervals),
        "intervals_within_requested_budget_percent": 100 * sum(i <= 1000 / expected_hz for i in intervals) / len(intervals),
        "intervals_over_1_5_nominal_refresh": sum(i > period / 1e6 * 1.5 for i in intervals),
        "median_offset_from_nominal_percent": 100 * (median / (period / 1e6) - 1),
        "estimated_missed_measured_refresh_slots": sum(max(0, round(i / median) - 1) for i in intervals),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expected-hz", type=int, choices=(60, 120), default=120)
    parser.add_argument("--label", default="unspecified")
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    adb = [args.adb] + (["-s", args.serial] if args.serial else [])

    def shell(command):
        return subprocess.check_output(adb + ["shell", command], text=True, encoding="utf-8", timeout=15)

    layers = shell("dumpsys SurfaceFlinger --list").splitlines()
    matches = [layer for layer in layers if "SurfaceView[org.supermetroid.thor/" in layer and "(BLAST)" in layer]
    if len(matches) != 1:
        raise RuntimeError("Open the Thor game surface first; expected exactly one game layer")
    command = "dumpsys SurfaceFlinger --latency " + shlex.quote(matches[0])
    timestamps = set()
    start = time.monotonic()
    periods = set()
    period = 0
    while time.monotonic() - start < args.seconds:
        lines = shell(command).splitlines()
        if lines:
            period = int(lines[0])
            if period > 0: periods.add(period)
        for line in lines[1:]:
            fields = line.split()
            if len(fields) == 3:
                presented = int(fields[1])
                if 0 < presented < 2**63 - 1:
                    timestamps.add(presented)
        time.sleep(.4)
    if len(periods) != 1:
        raise RuntimeError("Display period changed during the sample; repeat after mode settles")
    result = summarize(timestamps, period, args.expected_hz)
    result["label"] = args.label
    result["device_model"] = shell("getprop ro.product.model").strip()
    result["firmware"] = shell("getprop ro.build.display.id").strip()
    result["pacing_interpretation"] = (
        "Regular presentation cadence; absolute budget is reported independently."
        if result["intervals_over_1_5_nominal_refresh"] == 0
        else "Missed refresh intervals observed; investigate workload and pacing."
    )
    # Preserve raw timestamps to allow independent recalculation. First batch can
    # include roughly one second of history; sample_seconds reports that coverage.
    result["presentation_timestamps_ns"] = sorted(timestamps)
    output = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "presentation_timestamps_ns"}, indent=2))


if __name__ == "__main__":
    main()
