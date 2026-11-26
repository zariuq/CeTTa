#!/usr/bin/env python3
"""Python's tokens as CeTTa reads them, against CPython's tokenize.

    check_python_tokens.py CETTA PATH... [--jobs N] [--report FILE]
                           [--timeout SECONDS] [--memory MIB] [--show N]
                           [--exclude DIRNAME ...]

Each PATH is a Python source file or a directory searched for *.py files.
Each file is read by CeTTa with the python-tokens presentation
(langdef/python/Lexical.module, `--print-tokens`) and by CPython's
tokenize.generate_tokens; the two token streams are compared token by token
on (type, string, start, end).  CPython is the reference: this check runs
under the CPython whose tokens the language definition states.

Verdicts: same; DIFFER (the first token that differs is shown); OURS-FAILED
(CeTTa refused the file: its message is shown); REF-FAILED (tokenize refused
the file).  The check fails unless every file is the same.
"""
import argparse
import concurrent.futures
import io
import json
import os
import resource
import subprocess
import sys
import tokenize

CLASSES = {"Ident": "NAME", "symbol": "OP", "comment": "COMMENT", "nl": "NL", "end": "ENDMARKER"}


def sources(paths, excluded):
    for path in paths:
        if os.path.isdir(path):
            for root, dirs, files in os.walk(path):
                dirs[:] = sorted(d for d in dirs if d not in excluded)
                for name in sorted(files):
                    if name.endswith(".py"):
                        yield os.path.join(root, name)
        else:
            yield path


def reference(path):
    with tokenize.open(path) as stream:
        text = stream.read()
    out = []
    for t in tokenize.generate_tokens(io.StringIO(text).readline):
        out.append([tokenize.tok_name[t.type], t.string, t.start[0], t.start[1], t.end[0], t.end[1]])
    return out


def ours(cetta, path, timeout, memory):
    def limits():
        if memory:
            limit = memory * 1024 * 1024
            resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    proc = subprocess.run([cetta, "--lang", "petta", "--syntax", "python-tokens", "--print-tokens", path],
                          capture_output=True, timeout=timeout, preexec_fn=limits)
    if proc.returncode != 0:
        message = proc.stderr.decode("utf-8", "replace").strip().splitlines()
        return None, message[-1] if message else f"exit {proc.returncode}"
    out = []
    for line in proc.stdout.decode("utf-8").splitlines():
        row = json.loads(line)
        row[0] = CLASSES.get(row[0], row[0])
        out.append(row)
    return out, None


def check(cetta, path, timeout, memory):
    try:
        ref = reference(path)
    except (SyntaxError, tokenize.TokenError, UnicodeDecodeError) as e:
        return path, "REF-FAILED", f"{type(e).__name__}: {e}"
    try:
        got, failure = ours(cetta, path, timeout, memory)
    except subprocess.TimeoutExpired:
        return path, "OURS-FAILED", f"no answer in {timeout} s"
    if got is None:
        return path, "OURS-FAILED", failure
    for i, (a, b) in enumerate(zip(ref, got)):
        if a != b:
            return path, "DIFFER", f"token {i}: CPython {a} | CeTTa {b}"
    if len(ref) != len(got):
        return path, "DIFFER", f"{len(ref)} tokens from CPython, {len(got)} from CeTTa"
    return path, "same", ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cetta")
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--report")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--memory", type=int, default=0, help="address-space limit of each run, MiB (none unless given)")
    parser.add_argument("--show", type=int, default=12, help="differences shown on standard output")
    parser.add_argument("--exclude", action="append", default=[], help="a directory name not searched")
    args = parser.parse_args()
    files = list(sources(args.paths, set(args.exclude)))
    if not files:
        print("FAIL: no Python source found")
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
            print(f"{verdict:12s} {path}: {note}")
            shown += 1
    if args.report:
        with open(args.report, "w") as stream:
            for path, verdict, note in rows:
                stream.write(f"{verdict}\t{path}\t{note}\n")
    summary = f"files {len(rows)}: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
    if counts.get("same", 0) == len(rows):
        print("PASS: every file reads to CPython's tokens: " + summary)
        return 0
    print("FAIL: " + summary)
    return 1


if __name__ == "__main__":
    sys.exit(main())
