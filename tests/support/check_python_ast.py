#!/usr/bin/env python3
"""Python's abstract syntax as CeTTa folds it, against CPython's ast.

    check_python_ast.py CETTA PATH... [--jobs N] [--report FILE]
                        [--timeout SECONDS] [--memory MIB] [--show N]
                        [--exclude DIRNAME ...]

Each PATH is a Python source file or a directory searched for *.py files.
CeTTa reads each with `--lang python --stage ast` (langdef/python/langdef.metta,
stopped after its ast stage) and prints its abstract syntax as one atom, ast's node
NAME as (py:NAME field ... lineno col_offset end_lineno end_col_offset): the
fields in ast's order, then the four positions for the nodes ast locates.  A
list is (LCons item rest) ... (LNil); None, True, False and Ellipsis are
(py:None), (py:True), (py:False) and (py:Ellipsis); a bytes value is
(py:Bytes TEXT) with one character for each byte; a complex value is
(py:Complex REAL IMAG).  CPython's ast.parse is the reference: the two trees
are compared node by node, constants with their types (True is not 1).

Verdicts: same; DIFFER (the first place where the trees differ is shown);
OURS-FAILED (CeTTa gave no tree: its message is shown); REF-FAILED (CPython
refused the file).  The check fails unless every file is the same.
"""
import argparse
import ast
import concurrent.futures
import os
import resource
import subprocess
import sys
import tokenize


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


# ---------------------------------------------------------------- CPython

def reference(node):
    """ast's tree as nested lists: [NAME, field ..., position ...]."""
    if isinstance(node, ast.AST):
        row = [type(node).__name__]
        row += [reference(getattr(node, f, None)) for f in node._fields]
        row += [getattr(node, a, None) for a in node._attributes]
        return row
    if isinstance(node, list):
        return ("list", [reference(x) for x in node])
    return ("const", node)


# ---------------------------------------------------------------- CeTTa

class Symbol(str):
    pass


def read_atoms(text):
    """CeTTa's printed atoms: ( ) "text" numbers symbols.  Inside a string a
    backslash quotes the next character; every other character stands for
    itself."""
    pos, n = 0, len(text)
    stack, top = [], []
    while pos < n:
        c = text[pos]
        if c in " \t\r\n":
            pos += 1
        elif c == "(":
            stack.append(top)
            top = []
            pos += 1
        elif c == ")":
            done = top
            top = stack.pop()
            top.append(done)
            pos += 1
        elif c == '"':
            pos += 1
            out = []
            while text[pos] != '"':
                if text[pos] == "\\":
                    pos += 1
                out.append(text[pos])
                pos += 1
            pos += 1
            top.append("".join(out))
        else:
            end = pos
            while end < n and text[end] not in ' \t\r\n()"':
                end += 1
            word = text[pos:end]
            pos = end
            top.append(number(word))
    return top


def number(word):
    try:
        return int(word)
    except ValueError:
        pass
    if word in ("inf", "-inf", "nan"):
        return float(word)
    if any(ch.isdigit() for ch in word):
        try:
            return float(word)
        except ValueError:
            pass
    return Symbol(word)


CONSTANTS = {"py:None": None, "py:True": True, "py:False": False, "py:Ellipsis": ...}


def ours_tree(atom):
    """The atom CeTTa printed, as the nested lists reference() builds."""
    if isinstance(atom, list):
        if not atom or not isinstance(atom[0], Symbol):
            raise ValueError(f"not a node: {atom!r:.80}")
        head = str(atom[0])
        if head == "LCons" or head == "LNil":
            items = []
            while isinstance(atom, list) and atom and atom[0] == "LCons":
                items.append(ours_tree(atom[1]))
                atom = atom[2]
            if not (isinstance(atom, list) and atom == ["LNil"]):
                raise ValueError(f"not a list: {atom!r:.80}")
            return ("list", items)
        if head in CONSTANTS and len(atom) == 1:
            return ("const", CONSTANTS[head])
        if head == "py:Bytes":
            return ("const", atom[1].encode("latin-1"))
        if head == "py:Complex":
            return ("const", complex(atom[1], atom[2]))
        cls = getattr(ast, head[3:], None) if head.startswith("py:") else None
        if not (isinstance(cls, type) and issubclass(cls, ast.AST)):
            raise ValueError(f"not an ast node: {head}")
        fields = len(cls._fields)
        row = [head[3:]]
        for i, x in enumerate(atom[1:]):
            if i >= fields:
                row.append(x)                       # a position
            elif isinstance(x, Symbol):
                raise ValueError(f"a bare symbol in {head}: {x}")
            else:
                row.append(ours_tree(x) if isinstance(x, list) else ("const", x))
        return row
    if isinstance(atom, Symbol):
        raise ValueError(f"a bare symbol: {atom}")
    return ("const", atom)


# ---------------------------------------------------------------- comparison

def same_constant(a, b):
    return type(a) is type(b) and (a == b or (a != a and b != b))


def first_difference(ref, got, where):
    if isinstance(ref, tuple) and ref[0] == "list":
        if not (isinstance(got, tuple) and got[0] == "list"):
            return where, ref, got
        for i, (r, g) in enumerate(zip(ref[1], got[1])):
            d = first_difference(r, g, f"{where}[{i}]")
            if d:
                return d
        if len(ref[1]) != len(got[1]):
            return f"{where} (length)", len(ref[1]), len(got[1])
        return None
    if isinstance(ref, tuple):
        if isinstance(got, tuple) and got[0] == "const" and same_constant(ref[1], got[1]):
            return None
        return where, ref, got
    if isinstance(ref, list):
        if not isinstance(got, list) or got[0] != ref[0]:
            return where, ref, got
        if len(got) != len(ref):
            return f"{where} ({ref[0]} arity)", len(ref), len(got)
        names = ast_fields(ref[0])
        for i, (r, g) in enumerate(zip(ref[1:], got[1:])):
            d = first_difference(r, g, f"{where}.{names[i] if i < len(names) else i}")
            if d:
                return d
        return None
    # a position: an int or None
    if type(ref) is not type(got) or ref != got:
        return where, ref, got
    return None


def ast_fields(name):
    cls = getattr(ast, name, None)
    return list(getattr(cls, "_fields", ())) + list(getattr(cls, "_attributes", ()))


def brief(x, width=160):
    text = repr(x)
    return text if len(text) <= width else text[:width] + "..."


# ---------------------------------------------------------------- running CeTTa

def ours(cetta, path, timeout, memory):
    def limits():
        if memory:
            limit = memory * 1024 * 1024
            resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    proc = subprocess.run([cetta, "--lang", "python", "--stage", "ast", path],
                          capture_output=True, timeout=timeout, preexec_fn=limits)
    # a str holding a lone surrogate prints as its generalized UTF-8 bytes
    out = proc.stdout.decode("utf-8", "surrogatepass")
    if proc.returncode != 0 or not out.strip():
        message = proc.stderr.decode("utf-8", "replace").strip().splitlines()
        return None, message[-1] if message else f"exit {proc.returncode}, no tree"
    atoms = read_atoms(out)
    if len(atoms) != 1:
        return None, f"{len(atoms)} atoms printed: {brief(out, 200)}"
    return atoms[0], None


def check(cetta, path, timeout, memory):
    try:
        with tokenize.open(path) as stream:
            text = stream.read()
        ref = reference(ast.parse(text, filename=path))
    except (SyntaxError, UnicodeDecodeError, ValueError) as e:
        return path, "REF-FAILED", f"{type(e).__name__}: {e}"
    try:
        atom, failure = ours(cetta, path, timeout, memory)
    except subprocess.TimeoutExpired:
        return path, "OURS-FAILED", f"no answer in {timeout} s"
    if atom is None:
        return path, "OURS-FAILED", failure
    try:
        got = ours_tree(atom)
    except (ValueError, IndexError, UnicodeEncodeError) as e:
        return path, "OURS-FAILED", f"not an ast tree: {e}"
    d = first_difference(ref, got, "Module")
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
        print("PASS: every file folds to CPython's ast: " + summary)
        return 0
    print("FAIL: " + summary)
    return 1


if __name__ == "__main__":
    sys.exit(main())
