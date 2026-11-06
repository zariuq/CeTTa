#!/usr/bin/env python3
"""Compare native Megalodon document evaluation with the Megalodon binary.

For every example document (.mg, .mgs) under a Megalodon checkout, both
`megalodon -sexprinfo` and the native `mg-sexprinfo` are run with the
document's options (theory and signatures), and their output (the -sexprinfo rows
and the final syntax error or failure line) and exit status are compared
byte for byte.

The binary's output is cached under --reference (one .sexpr and .status
per document); `reference` fills the cache, `compare` reads it.  Each
document is checked the way Megalodon's examples README checks it, as
tests/megalodon/corpus_checks.tsv records.  --declarations leaves out the
rows only a checked proof produces (DELTA, PROOF, QED, USESKNOWN), on both
sides.  --kernel runs the tool with -kernel, admitting every declaration
and proof into the native kernel; its report goes to the work directory
and any document it does not accept is a difference.

usage: compare_elab_rows.py reference --megalodon BIN --root EXAMPLES --reference DIR [--jobs N] [FILE...]
       compare_elab_rows.py compare --tool MG_SEXPRINFO --root EXAMPLES --reference DIR [--jobs N]
                            [--declarations] [--kernel] [--work DIR] [FILE...]
"""
import argparse
import re
import hashlib
import concurrent.futures
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent

PROOF_ROWS = ("(DELTA ", "(PROOF ", "(QED)", "(USESKNOWN ")


def recipes():
    """tests/megalodon/corpus_checks.tsv: each document's options."""
    table = {}
    for line in (HERE / "corpus_checks.tsv").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        name, args = line.split("\t", 1)
        table[name] = args.split()
    return table


def documents(root, files):
    root = pathlib.Path(root)
    if files:
        paths = [pathlib.Path(f).resolve() for f in files]
    else:
        paths = sorted(p for p in root.rglob("*") if p.suffix in (".mg", ".mgs"))
    table = recipes()
    out = []
    for p in paths:
        rel = str(p.relative_to(root.resolve()))
        args = [str(root / a) if prev == "-I" else a
                for prev, a in zip([None] + table.get(rel, []), table.get(rel, []))]
        out.append((rel, args + [str(p)]))
    return out


def key(rel):
    return rel.replace("/", "__")


def run(cmd, out_path, timeout, memory_kb=None, err_path=None):
    pre = None
    if memory_kb:
        import resource

        def pre():
            resource.setrlimit(resource.RLIMIT_AS, (memory_kb * 1024, memory_kb * 1024))
    err = open(err_path, "wb") if err_path else subprocess.DEVNULL
    try:
        with open(out_path, "wb") as out:
            try:
                p = subprocess.run(cmd, stdout=out, stderr=err, timeout=timeout, preexec_fn=pre)
                return p.returncode
            except subprocess.TimeoutExpired:
                return "timeout"
    finally:
        if err_path:
            err.close()


def input_key(megalodon_digest, dargs):
    """What decides a reference answer: the Megalodon binary, the options,
    and the bytes of every file read (signatures in order, then the
    document)."""
    h = hashlib.sha256()
    h.update(b"compare_elab_rows reference v2\0" + megalodon_digest.encode() + b"\0")
    for a in dargs:
        h.update(a.encode() + b"\0")
        p = pathlib.Path(a)
        if p.is_file():
            h.update(hashlib.sha256(p.read_bytes()).hexdigest().encode() + b"\0")
    return h.hexdigest()


def reference(args):
    ref = pathlib.Path(args.reference)
    ref.mkdir(parents=True, exist_ok=True)
    megalodon_digest = hashlib.sha256(pathlib.Path(args.megalodon).read_bytes()).hexdigest()

    def one(doc):
        rel, dargs = doc
        k = key(rel)
        want_key = input_key(megalodon_digest, dargs)
        key_path = ref / (k + ".key")
        if (ref / (k + ".status")).exists() and key_path.exists() and \
                key_path.read_text().strip() == want_key:
            return rel, "cached"
        st = run([args.megalodon, "-sexprinfo"] + dargs, ref / (k + ".sexpr"), args.timeout)
        (ref / (k + ".status")).write_text("%s\n" % st)
        key_path.write_text(want_key + "\n")
        return rel, st

    docs = documents(args.root, args.files)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for rel, st in pool.map(one, docs):
            print("%s %s" % (st, rel), flush=True)
    return 0


DECLARATION_ROWS = {b"(PRIM", b"(PARAM", b"(DEF", b"(AXIOM", b"(THM"}
IDENTITY = re.compile(rb'"[0-9a-f]{64}"')


def rows(path, declarations):
    data = pathlib.Path(path).read_bytes()
    if not declarations:
        return data
    keep = [l for l in data.split(b"\n") if not l.startswith(tuple(r.encode() for r in PROOF_ROWS))]
    return b"\n".join(keep)


def compare(args):
    ref = pathlib.Path(args.reference)
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)

    def one(doc):
        rel, dargs = doc
        k = key(rel)
        status_path = ref / (k + ".status")
        if not status_path.exists():
            return rel, "no-reference"
        want = status_path.read_text().strip()
        if want == "timeout":
            return rel, "reference-timeout"
        tool = [args.tool, "-kernel"] if args.kernel else [args.tool]
        st = run(tool + dargs, work / (k + ".sexpr"), args.timeout, args.memory_kb,
                 work / (k + ".kernel") if args.kernel else None)
        if args.kernel:
            report = (work / (k + ".kernel")).read_text()
            if want == "0" and "(MegalodonKernel ok" not in report:
                return rel, "KERNEL %s" % report.strip().splitlines()[-1][:200] if report.strip() else "KERNEL none"

        same_rows = rows(work / (k + ".sexpr"), args.declarations) == rows(ref / (k + ".sexpr"), args.declarations)
        if same_rows and str(st) == want:
            # what the equal rows carry: the declarations Megalodon exports
            # (PRIM PARAM DEF AXIOM THM) and every identity in them
            data = (ref / (k + ".sexpr")).read_bytes()
            heads = [l.split(b" ", 1)[0] for l in data.split(b"\n")]
            decls = sum(h in DECLARATION_ROWS for h in heads)
            return rel, "same", decls, len(IDENTITY.findall(data))
        return rel, "DIFFER exit=%s want=%s rows=%s" % (st, want, "same" if same_rows else "differ")
        return rel, "DIFFER exit=%s want=%s rows=%s" % (st, want, "same" if same_rows else "differ")

    docs = documents(args.root, args.files)
    counts = {}
    decls = identities = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for result in pool.map(one, docs):
            rel, verdict = result[0], result[1]
            if verdict == "same":
                decls += result[2]
                identities += result[3]
            counts[verdict.split()[0]] = counts.get(verdict.split()[0], 0) + 1
            if verdict != "same":
                print("%s %s" % (verdict, rel), flush=True)
    print("(MegalodonElabRows %s declarations=%d identities=%d)" % (
        " ".join("%s=%d" % kv for kv in sorted(counts.items())), decls, identities))
    return 0 if set(counts) <= {"same"} else 1


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("reference")
    r.add_argument("--megalodon", required=True)
    c = sub.add_parser("compare")
    c.add_argument("--tool", required=True)
    c.add_argument("--declarations", action="store_true")
    c.add_argument("--kernel", action="store_true")
    c.add_argument("--work", default="runtime/mg-elab/work")
    c.add_argument("--memory-kb", type=int, default=8 * 1024 * 1024)
    for p in (r, c):
        p.add_argument("--root", required=True)
        p.add_argument("--reference", required=True)
        p.add_argument("--jobs", type=int, default=8)
        p.add_argument("--timeout", type=int, default=3600)
        p.add_argument("files", nargs="*")
    args = ap.parse_args()
    return reference(args) if args.cmd == "reference" else compare(args)


if __name__ == "__main__":
    sys.exit(main())
