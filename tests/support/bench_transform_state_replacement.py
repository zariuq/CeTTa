"""Measure native replacement of a single row while its payload stays fixed."""

import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def program(width, steps):
    payload = "(" + " ".join(f"(cell {i})" for i in range(width)) + ")"
    source = ["!(bind! &rows (new-space))", f"!(add-atom &rows (state 0 {payload}))"]
    for i in range(steps):
        # Increment only the small field. No interpreter or per-step program
        # compilation is needed to reproduce the large-state transport.
        source.append(f"!(let $_ (transform &rows (take (state {i} $payload)) "
                      f"(state {i + 1} $payload) &rows) (empty))")
    source.append("!(contents (collapse (match &rows $row $row)))")
    return "\n".join(source) + "\n", f"(contents ((state {steps} {payload})))"


def observe(binary, path, width, steps):
    text, expected = program(width, steps)
    path.write_text(text)
    run = subprocess.run([binary, "--emit-runtime-stats", "--lang", "petta",
                          "--profile", "extended", str(path)], cwd=ROOT,
                         capture_output=True, text=True, timeout=30, check=True)
    actual = [line for line in run.stdout.splitlines() if line.startswith("(contents ")]
    assert actual == [expected], (width, steps, actual, expected)
    stats = {}
    for line in run.stderr.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0] == "runtime-counter":
            stats[fields[1]] = int(fields[2])
    assert stats, "use a build with runtime statistics"
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    fields = ("term-universe-copy-call", "term-universe-copy-node")
    print("payload cells\treplacements\t" + "\t".join(fields))
    with tempfile.TemporaryDirectory(dir=ROOT / "runtime", prefix="state-replacement-") as work:
        path = Path(work) / "replace.metta"
        for width in (32, 64, 128):
            initial = observe(args.binary.resolve(), path, width, 0)
            repeated = observe(args.binary.resolve(), path, width, 64)
            increment = [repeated.get(name, 0) - initial.get(name, 0) for name in fields]
            print("\t".join(map(str, [width, 64, *increment])))


if __name__ == "__main__":
    main()
