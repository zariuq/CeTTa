#!/usr/bin/env python3
"""Control native peak-memory accounting and timeout collection in the MAM runner."""
import argparse
import os
from pathlib import Path
import resource
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "benchmarks/mam_families"))
from run import paired_measure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # A measurement of the child must not inherit this launcher's high-water mark.
    retained = bytearray(64 << 20)
    assert retained[0] == 0
    parent_peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    measurements = []
    for size in (1, 128):
        result = paired_measure([sys.executable, "-c", f"b=bytearray({size}<<20); print(len(b))"],
                                out, dict(os.environ), out / f"memory-{size}", 10, None, False)
        assert result["rc"] == 0 and not result["timed_out"], result
        assert Path(result["stdout_file"]).read_text() == str(size << 20) + "\n"
        measurements.append(result)
    small, large = measurements
    assert small["rss_kib"] < parent_peak // 2, (parent_peak, small)
    assert large["rss_kib"] > small["rss_kib"] + (96 << 10), measurements
    timed = paired_measure([sys.executable, "-c", "import time; time.sleep(5)"],
                           out, dict(os.environ), out / "cancel", 0.1, None, False)
    assert timed["timed_out"] and timed["rc"] != 0 and timed["rss_kib"] is None, timed
    print("PASS: native child peak memory excludes launcher history; cancelled jobs retain no invented measurement")


if __name__ == "__main__":
    main()
