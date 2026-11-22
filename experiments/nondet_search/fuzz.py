#!/usr/bin/env python3
"""Differential fuzzing of the search strategies.

Generates random terminating nondeterministic programs in the subset ndsearch
reads (overlapping clauses, constructor patterns, let with patterns, if,
superpose, empty, arithmetic, recursion on a decreasing counter), then
compares answer bags:

  * ndsearch's dfs, merge, hybrid and frontier engines (digest of the bag);
  * optionally CeTTa (--lang petta) and SWI-PeTTa on !(collapse ...),
    whose printed tuples are compared as multisets with ndsearch's bag.

  fuzz.py --ndsearch BIN [--cetta BIN] [--swi DIR] [--count N] [--seed S]

Programs that disagree are kept under fuzz_failures/ for inspection.
"""
import argparse
import collections
import os
import random
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


class Gen:
    def __init__(self, rng):
        self.rng = rng
        self.fns = []  # (name, arity)

    def atom(self, vars_):
        r = self.rng.random()
        if vars_ and r < 0.6:
            return self.rng.choice(vars_)
        if r < 0.8:
            return str(self.rng.randint(0, 4))
        return self.rng.choice(["a", "b"])

    def expr(self, vars_, depth, fn_index, rec_var):
        rng = self.rng
        if depth <= 0:
            return self.atom(vars_)
        k = rng.random()
        if k < 0.15:
            return self.atom(vars_)
        if k < 0.30:
            op = rng.choice(["+", "*"])
            return f"(% ({op} {self.num(vars_, depth - 1, fn_index, rec_var)} {self.num(vars_, depth - 1, fn_index, rec_var)}) 5)"
        if k < 0.45:
            n = rng.randint(2, 4)
            alts = " ".join(self.expr(vars_, depth - 1, fn_index, rec_var) for _ in range(n))
            return f"(superpose ({alts}))"
        if k < 0.58:
            v = f"$v{rng.randint(0, 99)}"
            while v in vars_:
                v = f"$v{rng.randint(0, 999)}"
            if rng.random() < 0.3:
                w = v + "w"
                bound = f"(C {self.expr(vars_, depth - 1, fn_index, rec_var)} {self.atom(vars_)})"
                return f"(let (C {v} {w}) {bound} {self.expr(vars_ + [v, w], depth - 1, fn_index, rec_var)})"
            return f"(let {v} {self.expr(vars_, depth - 1, fn_index, rec_var)} {self.expr(vars_ + [v], depth - 1, fn_index, rec_var)})"
        if k < 0.70:
            a = self.num(vars_, depth - 1, fn_index, rec_var)
            b = self.num(vars_, depth - 1, fn_index, rec_var)
            cmp_ = rng.choice(["==", "<", ">="])
            return (f"(if ({cmp_} {a} {b}) {self.expr(vars_, depth - 1, fn_index, rec_var)} "
                    f"{self.expr(vars_, depth - 1, fn_index, rec_var)})")
        if k < 0.75:
            return "(empty)"
        if k < 0.82:
            return f"(C {self.expr(vars_, depth - 1, fn_index, rec_var)} {self.atom(vars_)})"
        # Calls: earlier functions freely, or this one on a smaller counter.
        if rec_var and rng.random() < 0.4:
            name, arity = self.fns[fn_index]
            args = [f"(- {rec_var} 1)"] + [self.num(vars_, depth - 1, fn_index, rec_var) for _ in range(arity - 1)]
            return f"(if (> {rec_var} 0) ({name} {' '.join(args)}) {self.atom(vars_)})"
        if fn_index > 0:
            name, arity = self.fns[rng.randint(0, fn_index - 1)]
            args = [self.num(vars_, depth - 1, fn_index, rec_var) for _ in range(arity)]
            if arity == 0:
                return f"({name})"
            return f"({name} {' '.join(args)})"
        return self.atom(vars_)

    def num(self, vars_, depth, fn_index, rec_var):
        # A numeric expression: numbers only, so arithmetic never meets data.
        rng = self.rng
        if depth <= 0 or rng.random() < 0.5:
            nums = [v for v in vars_ if v.startswith("$n") or v == rec_var]
            if nums and rng.random() < 0.7:
                return rng.choice(nums)
            return str(rng.randint(0, 4))
        if rng.random() < 0.4:
            n = rng.randint(2, 3)
            return f"(superpose ({' '.join(str(rng.randint(0, 4)) for _ in range(n))}))"
        op = rng.choice(["+", "*"])
        return f"(% ({op} {self.num(vars_, depth - 1, fn_index, rec_var)} {self.num(vars_, depth - 1, fn_index, rec_var)}) 5)"

    def program(self):
        rng = self.rng
        nfn = rng.randint(2, 5)
        self.fns = []
        for i in range(nfn):
            self.fns.append((f"f{i}", rng.randint(1, 2)))
        lines = []
        for i, (name, arity) in enumerate(self.fns):
            nclauses = rng.randint(1, 3)
            for _ in range(nclauses):
                params = ["$n0"] + [f"$n{j}" for j in range(1, arity)]
                pats = list(params)
                # Some clauses test a constant in the second argument.
                if arity > 1 and rng.random() < 0.3:
                    pats[1] = str(rng.randint(0, 4))
                    vars_ = [params[0]]
                else:
                    vars_ = list(params)
                body = self.num_or_expr(vars_, i)
                lines.append(f"(= ({name} {' '.join(pats)}) {body})")
        main = self.fns[-1]
        args = " ".join(str(rng.randint(1, 3)) for _ in range(main[1]))
        lines.append(f"(= (main) ({main[0]} {args}))")
        lines.append("!(collapse (main))")
        return "\n".join(lines) + "\n"

    def num_or_expr(self, vars_, i):
        return self.expr(vars_, self.rng.randint(2, 4), i, "$n0")


def parse_tuple(text):
    """Parse a printed tuple of atoms into a list of canonical strings."""
    text = text.strip()
    if not (text.startswith("(") and text.endswith(")")):
        return None
    inner = text[1:-1]
    items, depth, cur = [], 0, ""
    for ch in inner:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == " " and depth == 0:
            if cur:
                items.append(cur)
            cur = ""
        else:
            cur += ch
    if cur:
        items.append(cur)
    return items


def run(cmd, cwd=None, timeout=30):
    p = subprocess.run(["timeout", str(timeout), *cmd], cwd=cwd,
                       capture_output=True, text=True)
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ndsearch", required=True)
    ap.add_argument("--cetta")
    ap.add_argument("--swi")
    ap.add_argument("--count", type=int, default=200)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--workdir", default=str(HERE / "fuzz_work"))
    args = ap.parse_args()
    work = Path(args.workdir)
    work.mkdir(exist_ok=True)
    fail_dir = HERE / "fuzz_failures"
    rng = random.Random(args.seed)
    gen = Gen(rng)
    stats = collections.Counter()
    for n in range(args.count):
        prog = gen.program()
        path = work / f"p{n}.metta"
        path.write_text(prog)
        # The merged frontier first: it bounds the size of the answer bag.
        p = run([args.ndsearch, "--engine", "merge", str(path)], timeout=20)
        if p.returncode != 0:
            stats["ndsearch_error"] += 1
            continue
        total = int([f for f in p.stdout.split("\t") if f.startswith("answers=")][0].split("=")[1])
        stats["answers"] += total
        if total > 2_000_000:
            # Too large to enumerate: compare merge with hybrid only.
            q = run([args.ndsearch, "--engine", "hybrid", str(path)], timeout=20)
            d1 = [f for f in p.stdout.split("\t") if f.startswith("digest=")]
            d2 = [f for f in q.stdout.split("\t") if f.startswith("digest=")]
            stats["large_programs"] += 1
            if q.returncode != 0 or d1 != d2:
                stats["large_disagreements"] += 1
                fail_dir.mkdir(exist_ok=True)
                (fail_dir / f"seed{args.seed}_p{n}.metta").write_text(prog)
            continue
        p = run([args.ndsearch, "--engine", "all", "--print-bag", str(path)], timeout=60)
        if p.returncode != 0:
            stats["ndsearch_error"] += 1
            continue
        digests = {}
        bag = collections.Counter()
        for line in p.stdout.splitlines():
            if line.startswith("bag\t"):
                _, v, c = line.split("\t")
                bag[v] += int(c)
            elif "\tdigest=" in line:
                eng = line.split("\t")[0]
                digests[eng] = [f for f in line.split("\t") if f.startswith("digest=")][0]
        engines_agree = len(set(digests.values())) == 1 and len(digests) == 3
        stats["programs"] += 1
        ok = engines_agree
        external = total <= 200_000
        if not engines_agree:
            stats["engine_disagreements"] += 1
        for name, cmd, cwd in (("cetta", [args.cetta, "--lang", "petta", str(path)], None) if args.cetta else (None, None, None),
                               ("swi", ["bash", "./run.sh", str(path), "--silent"], args.swi) if args.swi else (None, None, None)):
            if not name or not external:
                continue
            q = run(cmd, cwd=cwd, timeout=30)
            lines = [l for l in q.stdout.splitlines() if l.strip() and "janus" not in l.lower()]
            got = parse_tuple(lines[-1]) if lines else None
            if got is None:
                stats[f"{name}_unparsed"] += 1
                ok = False
                continue
            if collections.Counter(got) != bag:
                stats[f"{name}_disagreements"] += 1
                ok = False
            else:
                stats[f"{name}_agreements"] += 1
        if not ok:
            fail_dir.mkdir(exist_ok=True)
            (fail_dir / f"seed{args.seed}_p{n}.metta").write_text(prog)
    print(dict(stats))


if __name__ == "__main__":
    main()
