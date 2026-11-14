#!/usr/bin/env python3
"""Direct compilation retains every source and entry across allocation boundaries."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("runtime", type=Path)
    args = parser.parse_args()
    compiler, runtime = args.compiler.resolve(), args.runtime.resolve()
    with tempfile.TemporaryDirectory(prefix="gslt-width-", dir=ROOT / "runtime") as directory:
        work = Path(directory)
        for index in range(129):
            (work / f"p{index}.metta").write_text(
                f"(gslt-presentation-v1 Piece{index} (signature (operator Choice 1)) "
                f"(equations) (rewrites (rule choose{index} (head (Choice item{index})) (body))))\n")
        query = work / "query.metta"
        query.write_text("!(collapse (gslt:Choice (Choice $item)))\n")
        checks = 0
        for count in (64, 65, 129):
            composition = work / f"composition-{count}.metta"
            composition.write_text('(gslt-composition-v1 (name "width") ' +
                "".join(f'(source "p{index}.metta")' for index in range(count)) + ")\n")
            explicit = [value for index in range(count)
                        for value in ("--source", str(work / f"p{index}.metta"))]
            outputs = []
            for route, inputs in (("argv", explicit), ("composition", ["--composition", str(composition)])):
                output = work / f"{route}-{count}.metta"
                compiled = subprocess.run([str(compiler), "petta-direct", *inputs, "--out", str(output)],
                    cwd=ROOT, text=True, capture_output=True, timeout=30)
                assert compiled.returncode == 0, compiled.stderr
                outputs.append(output.read_bytes())
                run = subprocess.run([sys.executable, "tests/support/run_gslt_program.py", str(runtime),
                    "--lang", "petta", str(output), str(query)], cwd=ROOT,
                    env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1"),
                    text=True, capture_output=True, timeout=30)
                expected = "(" + " ".join(f"(Choice item{index})" for index in range(count)) + ")\n"
                assert run.returncode == 0 and run.stdout == expected, (route, count, run.stdout, run.stderr)
                checks += 1
            assert outputs[0] == outputs[1], "source transport changed the emitted program"
            for mode in ("--entry-mode", "--closed-entry-mode"):
                output = work / (mode[2:] + ".metta")
                compiled = subprocess.run([str(compiler), "petta-direct", "--source", str(work / "p0.metta"),
                    *[value for _ in range(count) for value in (mode, "Choice:0")], "--out", str(output)],
                    cwd=ROOT, text=True, capture_output=True, timeout=30)
                assert compiled.returncode == 0, compiled.stderr
                # Repeated selection is idempotent, regardless of input-list size.
                single = work / "single-entry.metta"
                one = subprocess.run([str(compiler), "petta-direct", "--source", str(work / "p0.metta"),
                    mode, "Choice:0", "--out", str(single)], cwd=ROOT,
                    text=True, capture_output=True, timeout=30)
                assert one.returncode == 0 and output.read_bytes() == single.read_bytes(), one.stderr
                checks += 1
        for suffix, extra in (("duplicate", '(source "p0.metta")'), ("empty", '(source "")')):
            bad = work / (suffix + ".metta")
            bad.write_text('(gslt-composition-v1 (name "invalid") ' +
                "".join(f'(source "p{index}.metta")' for index in range(129)) + extra + ")\n")
            output = work / (suffix + ".out")
            rejected = subprocess.run([str(compiler), "petta-direct", "--composition", str(bad),
                "--out", str(output)], cwd=ROOT, text=True, capture_output=True, timeout=30)
            assert rejected.returncode != 0 and not output.exists()
            assert ("repeats a source" if suffix == "duplicate" else "invalid source") in rejected.stderr
            checks += 1
    print(f"PASS: {checks} source/entry width controls preserve order, multiplicity, transport and invalid-source diagnostics")
    return 0


if __name__ == "__main__":
    sys.exit(main())
