#!/usr/bin/env python3
"""Python programs as CeTTa runs them, against CPython.

    check_python_run.py CETTA PATH... [--jobs N] [--timeout SECONDS]
                        [--memory MIB] [--show N] [--report FILE]

Each PATH is a Python program or a directory searched for *.py files (not in
directories whose names begin with _: they hold the modules programs import).  Each
program runs under CPython (`python3 -s -P PROGRAM`: isolated as -I isolates,
but with PYTHONHASHSEED read, which -I's -E would ignore) and under CeTTa
(`CETTA --lang python PROGRAM`), with PYTHONHASHSEED=0 and no other PYTHON*
variable in the environment.  The runs agree when
their standard output is identical, the last line of their standard error is
identical (an uncaught exception's type and message), and their exit statuses
are equal; an address in a default repr (" at 0x...") compares as equal.  The check fails unless every program agrees.
"""
import argparse
import concurrent.futures
import os
import re
import resource
import subprocess
import sys

# A default repr shows the object's address, which no two runs share: an
# address after " at 0x" compares as equal on both sides.
ADDRESS = re.compile(r" at 0x[0-9a-f]+")


def same_addresses(text):
    return ADDRESS.sub(" at 0x", text)


def sources(paths):
    for path in paths:
        if os.path.isdir(path):
            for root, dirs, files in os.walk(path):
                # a directory whose name begins with _ holds the modules
                # the programs import, not programs
                dirs[:] = sorted(d for d in dirs if not d.startswith("_"))
                for name in sorted(files):
                    if name.endswith(".py"):
                        yield os.path.join(root, name)
        else:
            yield path


def last_line(text):
    lines = [line for line in text.splitlines() if line.strip()]
    return lines[-1] if lines else ""


def run(command, timeout, memory, path):
    # in the program's own directory (the files it writes stay there), its
    # standard input the file NAME.stdin beside it, else empty
    def limits():
        if memory:
            limit = memory * 1024 * 1024
            resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    env = {k: v for k, v in os.environ.items() if not k.startswith("PYTHON")}
    env["PYTHONHASHSEED"] = "0"
    stdin_path = os.path.splitext(path)[0] + ".stdin"
    stdin = open(stdin_path, "rb").read() if os.path.exists(stdin_path) else b""
    try:
        proc = subprocess.run([os.path.abspath(c) if i == len(command) - 1 else c for i, c in enumerate(command)],
                              input=stdin, capture_output=True, timeout=timeout,
                              preexec_fn=limits, env=env, cwd=os.path.dirname(os.path.abspath(path)))
    except subprocess.TimeoutExpired:
        return None
    return (same_addresses(proc.stdout.decode("utf-8", "replace")),
            same_addresses(last_line(proc.stderr.decode("utf-8", "replace"))), proc.returncode)


def check(cetta, path, timeout, memory):
    ref = run([sys.executable, "-s", "-P", path], timeout, 0, path)
    got = run([os.path.abspath(cetta) if os.path.exists(cetta) else cetta, "--lang", "python", path], timeout, memory, path)
    if ref is None:
        return path, "REF-TIMEOUT", ""
    if got is None:
        return path, "TIMEOUT", f"no answer in {timeout} s"
    if ref == got:
        return path, "same", ""
    what = []
    if ref[0] != got[0]:
        a, b = ref[0].splitlines(), got[0].splitlines()
        i = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
        what.append(f"stdout line {i + 1}: CPython {a[i] if i < len(a) else '<end>'!r} | "
                    f"CeTTa {b[i] if i < len(b) else '<end>'!r}")
    if ref[1] != got[1]:
        what.append(f"stderr: CPython {ref[1]!r} | CeTTa {got[1]!r}")
    if ref[2] != got[2]:
        what.append(f"status: CPython {ref[2]} | CeTTa {got[2]}")
    return path, "DIFFER", "; ".join(what)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cetta")
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--memory", type=int, default=0, help="address-space limit of each CeTTa run, MiB (none unless given)")
    parser.add_argument("--show", type=int, default=20)
    parser.add_argument("--report")
    args = parser.parse_args()
    files = list(sources(args.paths))
    if not files:
        print("FAIL: no Python program found")
        return 1
    cetta = os.path.abspath(args.cetta)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(lambda p: check(cetta, p, args.timeout, args.memory), files))
    counts = {}
    for _, verdict, _ in rows:
        counts[verdict] = counts.get(verdict, 0) + 1
    shown = 0
    for path, verdict, note in rows:
        if verdict != "same" and shown < args.show:
            print(f"{verdict:8s} {path}: {note}")
            shown += 1
    if args.report:
        with open(args.report, "w") as stream:
            for path, verdict, note in rows:
                stream.write(f"{verdict}\t{path}\t{note}\n")
    summary = f"programs {len(rows)}: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
    if counts.get("same", 0) == len(rows):
        print("PASS: every program runs as under CPython: " + summary)
        return 0
    print("FAIL: " + summary)
    return 1


if __name__ == "__main__":
    sys.exit(main())
