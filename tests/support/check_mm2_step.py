#!/usr/bin/env python3
"""The MM2 step written with transform leaves the store the MM2 runtime leaves.

examples/mm2_step.metta runs two MM2 programs with a set space, a priority
space and transform.  The same programs run here on the native MM2 runtime
(--lang mm2 --profile gslt), and each must leave the same atoms.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def atoms(text):
    """The top-level atoms of an S-expression list's inside."""
    found, depth, start = [], 0, None
    for i, ch in enumerate(text):
        if ch == "(":
            if depth == 0:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                found.append(re.sub(r"\s+", " ", text[start:i + 1]))
    return found


def main():
    binary = os.path.abspath(sys.argv[1])
    example = os.path.join(ROOT, "examples", "mm2_step.metta")
    source = open(example).read()
    # Each program is the list given to mm2:load.
    programs = [atoms(m[len("!(mm2:load ("):-2])
                for m in re.findall(r"!\(mm2:load \(.*?\)\)\n(?=!\(steps)", source, re.S)]
    ran = subprocess.run([binary, "--lang", "petta", "--profile", "extended", example],
                         cwd=ROOT, capture_output=True, text=True, timeout=300)
    stores = [sorted(atoms(line[len("(store ("):-2]))
              for line in ran.stdout.splitlines() if line.startswith("(store ")]
    if ran.returncode != 0 or len(stores) != len(programs) or not programs:
        print("FAIL: the example did not run its %d programs: %s"
              % (len(programs), ran.stdout[-300:] + ran.stderr[-300:]))
        return 1
    for number, (program, store) in enumerate(zip(programs, stores)):
        with tempfile.NamedTemporaryFile("w", suffix=".mm2", delete=False) as f:
            f.write("\n".join(program) + "\n")
            path = f.name
        try:
            native = subprocess.run([binary, "--lang", "mm2", "--profile", "gslt", path],
                                    cwd=ROOT, capture_output=True, text=True, timeout=300)
        finally:
            os.unlink(path)
        left = sorted(atoms(native.stdout))
        if native.returncode != 0 or left != store:
            print("FAIL: program %d: the step written with transform leaves\n  %s\n"
                  "the MM2 runtime leaves\n  %s" % (number, store, left))
            return 1
    print("PASS: the MM2 step written with transform leaves the store the MM2 "
          "runtime leaves, on %d programs" % len(programs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
