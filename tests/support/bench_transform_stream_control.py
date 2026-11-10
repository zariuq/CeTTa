#!/usr/bin/env python3
"""Compare first-answer and full-drain transform cursor work independently."""

import argparse
import os
from pathlib import Path
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def program(n, full):
    lines = ["!(bind! &join (new-space))"]
    lines += [f"!(add-atom &join (left {i}))" for i in range(n)]
    lines += [f"!(add-atom &join (right {i}))" for i in range(n)]
    transform = ("(transform &join (, (take (left $x)) (take (right $y))) "
                 "(claimed $x $y))")
    lines.append(f"!(all (collapse {transform}))" if full else f"!(once {transform})")
    return "\n".join(lines) + "\n"


def observe(binary, text, path, reference):
    path.write_text(text)
    env = dict(os.environ)
    if reference:
        env["CETTA_TRANSFORM_CURSOR_REFERENCE"] = "1"
    else:
        env.pop("CETTA_TRANSFORM_CURSOR_REFERENCE", None)
    start = time.perf_counter()
    proc = subprocess.run([binary, "--lang", "petta", "--profile", "extended", str(path)],
                          cwd=ROOT, env=env, capture_output=True, text=True,
                          timeout=180)
    elapsed = time.perf_counter() - start
    if proc.returncode != 0:
        raise RuntimeError(f"{path}: {proc.returncode}: {proc.stderr}")
    return elapsed, proc.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--n", type=int, default=200)
    parser.add_argument("--repeat", type=int, default=5)
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    for full in (False, True):
        label = "full-drain" if full else "first-answer"
        text = program(args.n, full)
        outputs = {}
        for reference in (False, True):
            route = "eager-reference" if reference else "cursor"
            times = []
            for repetition in range(args.repeat):
                path = ROOT / "runtime" / f"bench_transform_stream_{label}_{route}.metta"
                elapsed, output = observe(binary, text, path, reference)
                times.append(elapsed)
                outputs.setdefault(route, output)
                if outputs[route] != output:
                    raise AssertionError(f"unstable output on {label} {route}")
            print(f"{label} {route}: entry_product={args.n * args.n} "
                  f"median_ms={statistics.median(times) * 1000:.3f} "
                  f"runs_ms={','.join(f'{t * 1000:.3f}' for t in times)}")
        if outputs["cursor"] != outputs["eager-reference"]:
            raise AssertionError(f"cursor/reference answers differ for {label}")


if __name__ == "__main__":
    main()
