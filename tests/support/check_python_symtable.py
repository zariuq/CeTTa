#!/usr/bin/env python3
"""Python's symbol tables as CeTTa analyzes them, against CPython's symtable.

    check_python_symtable.py CETTA PATH... [--jobs N] [--report FILE]
                             [--timeout SECONDS] [--memory MIB] [--show N]
                             [--exclude DIRNAME ...]

Each PATH is a Python source file or a directory searched for *.py files.
CeTTa reads each with `--lang python --stage scope` (langdef/python/
langdef.metta, stopped after its scope stage) and prints the module's table
as one atom: (st:otable KIND NAME LINENO SYMBOLS CHILDREN), KIND one of
(st:module) (st:function) (st:class) (st:type-params) (st:type-alias)
(st:type-var), SYMBOLS a list of (st:osym NAME NUMBER), NUMBER the symbol's
flags and scope as CPython numbers them, CHILDREN a list of tables; a list is
(LCons item rest) ... (LNil).  CPython's symtable.symtable is the reference:
the tables are compared in order, each by its type, name, line and symbols.

Verdicts: same; both-refused (CPython raised a SyntaxError and CeTTa gave
no tables); DIFFER (the first table and symbol that differ are shown);
OURS-FAILED (CeTTa gave no tables where CPython gave them: its message is
shown); OURS-ACCEPTED (CeTTa gave tables where CPython raised a SyntaxError);
REF-FAILED (CPython could not read the file).  The check fails unless every
file is same or both-refused.
"""
import argparse
import concurrent.futures
import os
import resource
import subprocess
import sys
import symtable
import tokenize

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_python_ast import brief, read_atoms, sources  # noqa: E402

KINDS = {
    "st:module": "module",
    "st:function": "function",
    "st:class": "class",
    "st:type-params": "type parameters",
    "st:type-alias": "type alias",
    "st:type-var": "type variable",
    "st:annotation": "annotation",
}

FLAGS = [(1, "global"), (2, "local"), (4, "param"), (8, "nonlocal"), (16, "use"),
         (32, "free"), (64, "free-class"), (128, "import"), (256, "annot"),
         (512, "comp-iter"), (1024, "type-param"), (2048, "comp-cell")]
SCOPES = {0: "-", 1: "LOCAL", 2: "GLOBAL_EXPLICIT", 3: "GLOBAL_IMPLICIT", 4: "FREE", 5: "CELL"}


def spelled(number):
    """A symbol's number as its flags' names and its scope."""
    if number is None:
        return "absent"
    flags = [name for bit, name in FLAGS if number & bit]
    return f"{number} ({'|'.join(flags)}; {SCOPES.get(number >> 12, number >> 12)})"


# ---------------------------------------------------------------- CPython

def reference(table):
    """symtable's tables as (type, name, lineno, {name: number}, [child ...])."""
    return (TYPE_NAMES[table.type], table.name, table.lineno, dict(table.symbols),
            [reference(child) for child in table.children])


TYPE_NAMES = {}


def type_names():
    import _symtable
    names = {"TYPE_MODULE": "module", "TYPE_FUNCTION": "function", "TYPE_CLASS": "class",
             "TYPE_ANNOTATION": "annotation", "TYPE_TYPE_ALIAS": "type alias",
             "TYPE_TYPE_PARAMETERS": "type parameters", "TYPE_TYPE_VARIABLE": "type variable"}
    for constant, name in names.items():
        TYPE_NAMES[getattr(_symtable, constant)] = name


# ---------------------------------------------------------------- CeTTa

def items(atom):
    out = []
    while isinstance(atom, list) and atom and atom[0] == "LCons":
        out.append(atom[1])
        atom = atom[2]
    if atom != ["LNil"]:
        raise ValueError(f"not a list: {brief(atom, 80)}")
    return out


def ours_table(atom):
    if not (isinstance(atom, list) and len(atom) == 6 and atom[0] == "st:otable"):
        raise ValueError(f"not a table: {brief(atom, 80)}")
    _, kind, name, lineno, symbols, children = atom
    if not (isinstance(kind, list) and len(kind) == 1 and kind[0] in KINDS):
        raise ValueError(f"not a table kind: {brief(kind, 80)}")
    found = {}
    for sym in items(symbols):
        if not (isinstance(sym, list) and len(sym) == 3 and sym[0] == "st:osym"):
            raise ValueError(f"not a symbol: {brief(sym, 80)}")
        if sym[1] in found:
            raise ValueError(f"symbol {sym[1]!r} twice in table {name!r}")
        found[sym[1]] = sym[2]
    return (KINDS[kind[0]], name, lineno, found, [ours_table(c) for c in items(children)])


# ---------------------------------------------------------------- comparison

def first_difference(ref, got, where):
    rkind, rname, rline, rsyms, rkids = ref
    gkind, gname, gline, gsyms, gkids = got
    here = f"{where}/{rkind} {rname}@{rline}"
    if (rkind, rname, rline) != (gkind, gname, gline):
        return here, f"{rkind} {rname!r} line {rline}", f"{gkind} {gname!r} line {gline}"
    for name in sorted(set(rsyms) | set(gsyms)):
        if rsyms.get(name) != gsyms.get(name):
            return f"{here} symbol {name!r}", spelled(rsyms.get(name)), spelled(gsyms.get(name))
    for i, (r, g) in enumerate(zip(rkids, gkids)):
        d = first_difference(r, g, f"{here}[{i}]")
        if d:
            return d
    if len(rkids) != len(gkids):
        return f"{here} (children)", [f"{k[0]} {k[1]}" for k in rkids], [f"{k[0]} {k[1]}" for k in gkids]
    return None


# ---------------------------------------------------------------- running CeTTa

def ours(cetta, path, timeout, memory):
    def limits():
        if memory:
            limit = memory * 1024 * 1024
            resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    proc = subprocess.run([cetta, "--lang", "python", "--stage", "scope", path],
                          capture_output=True, timeout=timeout, preexec_fn=limits)
    out = proc.stdout.decode("utf-8", "surrogatepass")
    if proc.returncode != 0 or not out.strip():
        message = proc.stderr.decode("utf-8", "replace").strip().splitlines()
        return None, message[-1] if message else f"exit {proc.returncode}, no tables"
    atoms = read_atoms(out)
    if len(atoms) != 1:
        return None, f"{len(atoms)} atoms printed: {brief(out, 200)}"
    return atoms[0], None


def check(cetta, path, timeout, memory):
    refused = None
    try:
        with tokenize.open(path) as stream:
            text = stream.read()
    except (SyntaxError, UnicodeDecodeError, ValueError) as e:
        return path, "REF-FAILED", f"{type(e).__name__}: {e}"
    try:
        ref = reference(symtable.symtable(text, path, "exec")._table)
    except SyntaxError as e:
        refused = f"SyntaxError: {e.msg} (line {e.lineno})"
    except (UnicodeDecodeError, ValueError) as e:
        return path, "REF-FAILED", f"{type(e).__name__}: {e}"
    try:
        atom, failure = ours(cetta, path, timeout, memory)
    except subprocess.TimeoutExpired:
        return path, "OURS-FAILED", f"no answer in {timeout} s"
    if refused is not None:
        if atom is None:
            return path, "both-refused", f"{refused} | CeTTa: {failure}"
        return path, "OURS-ACCEPTED", refused
    if atom is None:
        return path, "OURS-FAILED", failure
    try:
        got = ours_table(atom)
    except (ValueError, IndexError) as e:
        return path, "OURS-FAILED", f"not a symbol table: {e}"
    d = first_difference(ref, got, "")
    if d:
        where, a, b = d
        return path, "DIFFER", f"{where}: CPython {brief(a)} | CeTTa {brief(b)}"
    return path, "same", ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cetta")
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--report")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--memory", type=int, default=0, help="address-space limit of each run, MiB (none unless given)")
    parser.add_argument("--show", type=int, default=12, help="differences shown on standard output")
    parser.add_argument("--exclude", action="append", default=[], help="a directory name not searched")
    args = parser.parse_args()
    type_names()
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
    agreed = ("same", "both-refused")
    shown = 0
    for path, verdict, note in rows:
        if verdict not in agreed and shown < args.show:
            print(f"{verdict:12s} {path}: {note}")
            shown += 1
    if args.report:
        with open(args.report, "w") as stream:
            for path, verdict, note in rows:
                stream.write(f"{verdict}\t{path}\t{note}\n")
    summary = f"files {len(rows)}: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
    if sum(counts.get(v, 0) for v in agreed) == len(rows):
        print("PASS: every file's symbol tables are CPython's: " + summary)
        return 0
    print("FAIL: " + summary)
    return 1


if __name__ == "__main__":
    sys.exit(main())
