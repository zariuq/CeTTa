#!/usr/bin/env python3
"""Python's syntax errors as CeTTa reports them, against CPython.

    check_python_syntax_errors.py CETTA MANIFEST... [--stage STAGE] [--jobs N]
                                  [--timeout S] [--memory MIB] [--show N]
                                  [--report FILE]

Each MANIFEST is a JSON list of {"file", "valid", "msg", "lineno", "offset"} rows
(a corpus directory's manifest.json: what CPython says of each file, its
parser, ast.parse, for the ast stage, and its symbol table pass,
symtable.symtable, for the scope stage).  Each file is read with
`--lang python --stage STAGE` (langdef/python/langdef.metta, stopped after
STAGE; ast by default).  A refusal is read from standard error as
"FILE: line L, column C: MESSAGE" or "FILE: MESSAGE".

Each file gets three verdicts, counted separately:
  decision  CeTTa refuses exactly the files CPython refuses;
  place     a refusal stands at CPython's line and offset (1-based columns);
  message   a refusal says CPython's message.
"""
import argparse
import concurrent.futures
import json
import os
import re
import resource
import subprocess
import sys

PLACED = re.compile(r"^line (\d+), column (\d+): (.*)$", re.S)


def ours(cetta, stage, path, timeout, memory):
    def limits():
        if memory:
            limit = memory * 1024 * 1024
            resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    proc = subprocess.run([cetta, "--lang", "python", "--stage", stage, path],
                          capture_output=True, timeout=timeout, preexec_fn=limits)
    err = proc.stderr.decode("utf-8", "replace").strip()
    if proc.returncode == 0 and proc.stdout.strip():
        return {"valid": True}
    prefix = path + ": "
    line = err.splitlines()[-1] if err else f"exit {proc.returncode}, no message"
    text = line[len(prefix):] if line.startswith(prefix) else line
    m = PLACED.match(text)
    if m:
        return {"valid": False, "lineno": int(m.group(1)), "offset": int(m.group(2)), "msg": m.group(3)}
    return {"valid": False, "lineno": None, "offset": None, "msg": text}


def check(cetta, stage, base, row, timeout, memory):
    path = os.path.join(base, row["file"])
    try:
        got = ours(cetta, stage, path, timeout, memory)
    except subprocess.TimeoutExpired:
        got = {"valid": None, "msg": f"no answer in {timeout} s"}
    v = {"file": path, "ref": row, "got": got}
    v["decision"] = got["valid"] == row["valid"]
    if not row["valid"] and got["valid"] is False:
        v["place"] = (got.get("lineno"), got.get("offset")) == (row.get("lineno"), row.get("offset"))
        v["message"] = got.get("msg") == row.get("msg")
    return v


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cetta")
    parser.add_argument("manifests", nargs="+")
    parser.add_argument("--stage", default="ast")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--memory", type=int, default=0, help="address-space limit of each run, MiB (none unless given)")
    parser.add_argument("--show", type=int, default=12)
    parser.add_argument("--report")
    args = parser.parse_args()
    cetta = os.path.abspath(args.cetta)
    work = []
    for manifest in args.manifests:
        base = os.path.dirname(os.path.abspath(manifest))
        work += [(base, row) for row in json.load(open(manifest))]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(lambda w: check(cetta, args.stage, w[0], w[1], args.timeout, args.memory), work))
    refused = [r for r in rows if not r["ref"]["valid"]]
    both = [r for r in refused if r["got"]["valid"] is False]
    n_dec = sum(r["decision"] for r in rows)
    n_place = sum(r.get("place", False) for r in both)
    n_msg = sum(r.get("message", False) for r in both)
    shown = 0
    for r in rows:
        if shown >= args.show:
            break
        if not r["decision"] or (r in both and not (r["place"] and r["message"])):
            ref, got = r["ref"], r["got"]
            print(f"{os.path.basename(r['file'])}: CPython {ref.get('lineno')}:{ref.get('offset')} {ref.get('msg') if not ref['valid'] else 'valid'!s}"
                  f" | CeTTa {got.get('lineno')}:{got.get('offset')} {got.get('msg') if got['valid'] is not True else 'valid'!s}")
            shown += 1
    if args.report:
        json.dump(rows, open(args.report, "w"), indent=1)
    print(f"files {len(rows)} ({len(refused)} refused by CPython): decision {n_dec}/{len(rows)}, "
          f"place {n_place}/{len(refused)}, message {n_msg}/{len(refused)}")
    ok = n_dec == len(rows) and n_place == len(refused) and n_msg == len(refused)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
