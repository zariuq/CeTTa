#!/usr/bin/env python3
"""transform's native step runs where it is admitted, at match's speed.

Usage: check_transform_step_paths.py CETTA_BINARY

Each native path of the step must run in its own program, as the transform
trace reports: a collected transform with reads only, answered by the native
query engine as match's answers are; a collected transform with takes, its
proposals and its firings native; a transform whose branches the caller
continues one by one, its proposals and guided sources native; and a collected
match of data, answered as the step with reads only.  The authored library
(CETTA_TRANSFORM_REFERENCE=1) runs none of them and answers alike.

A collected transform with reads only joins as fast as the match with the same
conjunction: over 20000 facts and 40000 answers, the best of three runs of the
transform takes at most 1.5 times the match's, plus 50 ms for noise.
"""
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IMPORT = "!(import! &self (library lib_transform))\n"

PATHS = [
    ("reads only", IMPORT +
     "!(bind! &s (new-space))\n"
     "!(add-atom &s (p a 1))\n!(add-atom &s (p a 2))\n!(add-atom &s (q 1 x))\n"
     "!(sort-atom (collapse (transform &s (, (p a $n) (q $n $y)) (hit $n $y))))\n",
     ["[transform] fired, reads only"], "((hit 1 x))"),
    ("collected takes", IMPORT +
     "!(bind! &s (new-space))\n"
     "!(add-atom &s (job a))\n!(add-atom &s (job b))\n"
     "!(sort-atom (collapse (transform &s (take (job $j)) (done $j))))\n"
     "!(collapse (match &s (job $j) $j))\n",
     ["[transform] proposals made", "[transform] fired (tf:fire"],
     "((done a) (done b))\n()"),
    ("branches continued one by one", IMPORT +
     "!(bind! &s (new-space))\n"
     "!(add-atom &s (job a))\n"
     "!(transform &s (take (job $j)) (done $j))\n",
     ["[transform] proposals made", "[transform] fired (tf:fire"], "(done a)"),
    ("collected match", "!(bind! &s (new-space))\n"
     "!(add-atom &s (p a 1))\n!(add-atom &s (p a 2))\n!(add-atom &s (p b 3))\n"
     "!(sort-atom (collapse (match &s (p a $n) (got $n))))\n",
     ["[step] match reads"], "((got 1) (got 2))"),
]


def run(binary, path, env_extra=None):
    env = dict(os.environ)
    env.pop("CETTA_TRANSFORM_REFERENCE", None)
    env.update(env_extra or {})
    p = subprocess.run([binary, "--lang", "petta", path], cwd=ROOT, env=env,
                       capture_output=True, text=True, timeout=300)
    return p.returncode, p.stdout, p.stderr


def answers(stdout):
    return "\n".join(line for line in stdout.splitlines() if line != "true")


def check_paths(binary, work):
    for name, source, wanted, expected in PATHS:
        path = os.path.join(work, "path.metta")
        with open(path, "w") as f:
            f.write(source)
        code, out, err = run(binary, path, {"CETTA_TRANSFORM_TRACE": "1"})
        trace = [line for line in err.splitlines() if line.startswith("[")]
        if code != 0 or answers(out) != expected:
            print(f"FAIL: {name}: answers {answers(out)!r}, expected {expected!r}")
            return False
        missing = [w for w in wanted if not any(line.startswith(w) for line in trace)]
        if missing or any("declined" in line for line in trace):
            print(f"FAIL: {name}: the native step did not run ({missing or 'declined'})")
            return False
        code, out, err = run(binary, path, {"CETTA_TRANSFORM_REFERENCE": "1",
                                            "CETTA_TRANSFORM_TRACE": "1"})
        if code != 0 or answers(out) != expected or \
                any(line.startswith("[transform] fired") for line in err.splitlines()):
            print(f"FAIL: {name}: the authored library answered otherwise or fired natively")
            return False
    return True


def join_program(op):
    lines = [IMPORT] if op == "transform" else []
    lines.append("!(bind! &f (new-space))\n")
    lines += [f"!(add-atom &f (p a{i % 100} {i}))\n" for i in range(20000)]
    lines.append(f"!(size-atom (collapse ({op} &f (, (p a7 $v) (p a8 $w)) (pair $v $w))))\n")
    return "".join(lines)


def best_time(binary, path):
    best = None
    for _ in range(3):
        start = time.perf_counter()
        code, out, _ = run(binary, path)
        elapsed = time.perf_counter() - start
        if code != 0 or "40000" not in out.split():
            return None
        best = elapsed if best is None else min(best, elapsed)
    return best


def check_speed(binary, work):
    times = {}
    for op in ("match", "transform"):
        path = os.path.join(work, f"join_{op}.metta")
        with open(path, "w") as f:
            f.write(join_program(op))
        times[op] = best_time(binary, path)
        if times[op] is None:
            print(f"FAIL: the {op} join did not answer 40000")
            return None
    if times["transform"] > 1.5 * times["match"] + 0.05:
        print(f"FAIL: a collected transform with reads only took {times['transform']:.3f}s, "
              f"the match {times['match']:.3f}s")
        return None
    return times


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cetta-transform-step-") as work:
        if not check_paths(binary, work):
            return 1
        times = check_speed(binary, work)
        if times is None:
            return 1
    print(f"PASS: transform's native step runs on its {len(PATHS)} paths, the authored "
          f"library answering alike; a collected transform joins at match's speed "
          f"({times['transform']:.2f}s against {times['match']:.2f}s, best of three)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
