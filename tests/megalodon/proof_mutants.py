#!/usr/bin/env python3
"""The native kernel refuses corrupted proofs.

Takes Megalodon's -sexprinfo exports of the example documents (the cache
compare_elab_rows.py fills), and for sampled theorems rewrites one PROOF row
at a time with a mutation that changes what the proof proves:

  hyp        the first (HYP i) becomes (HYP i+1)
  known      the first (KNOWN h) cites another axiom of the document
  drop_pfap  the first (PPFAP f a) loses its argument
  tmap_db    the first variable free in a (PTMAP p t) argument, (DB k),
             becomes (DB k+1)
  plam_prop  the first (PLAM P d) assumes (IMP P P) instead of P

Each mutant is the export up to the mutated theorem's QED, replayed by
mg_replay under a work budget; the kernel must stop at that theorem, refusing
it or running out of budget (neither grants authority).  The same export without
the mutation is replayed first as a control, and must go through.  A mutant
the kernel accepts is listed: it is either a kernel defect or a mutation
that happens to keep the proof valid; a term mutant whose argument has the
same beta normal form as the original is reported as equivalent, any other
accepted mutant as one to look at.

usage: proof_mutants.py --replay MG_REPLAY --root EXAMPLES --reference DIR [--docs N] [--jobs N] [FILE...]
"""
import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from compare_elab_rows import documents, key  # noqa: E402

PROFILE_FLAGS = {"-hf": "hf", "-mizar": "mizar", "-hoas": "hoas", "-setmm": "setmm"}
OPERATORS = ("hyp", "known", "drop_pfap", "tmap_db", "plam_prop")


def tokenize(text):
    return re.findall(r'"(?:[^"\\]|\\.)*"|\(|\)|[^\s()]+', text)


def parse(text):
    stack = [[]]
    for tok in tokenize(text):
        if tok == "(":
            stack.append([])
        elif tok == ")":
            done = stack.pop()
            stack[-1].append(done)
        else:
            stack[-1].append(tok)
    return stack[0][0]


def show(t):
    if isinstance(t, str):
        return t
    return "(" + " ".join(show(x) for x in t) + ")"


def first(t, pred, path=()):
    """The path of the first subterm (pre-order) satisfying pred."""
    if pred(t):
        return path
    if isinstance(t, list):
        for i, x in enumerate(t):
            p = first(x, pred, path + (i,))
            if p is not None:
                return p
    return None


def get(t, path):
    for i in path:
        t = t[i]
    return t


def put(t, path, new):
    if not path:
        return new
    t = list(t)
    t[path[0]] = put(t[path[0]], path[1:], new)
    return t


def is_head(name):
    return lambda t: isinstance(t, list) and t and t[0] == name


def free_db(t, depth=0, path=()):
    """The path of the first (DB k) free in t (pre-order)."""
    if not isinstance(t, list) or not t:
        return None
    if t[0] == "DB":
        return path if int(t[1]) >= depth else None
    inner = depth + 1 if t[0] in ("LAM", "ALL") else depth
    for i, x in enumerate(t):
        if i == 0:
            continue
        # a binder's type (position 1) is outside its scope
        d = depth if t[0] in ("LAM", "ALL") and i == 1 else inner
        p = free_db(x, d, path + (i,))
        if p is not None:
            return p
    return None


TERM_HEADS = {"DB", "TMH", "PRIM", "AP", "LAM", "ALL", "IMP", "TPAP"}


def shift(t, d, c=0):
    if t[0] == "DB":
        k = int(t[1])
        return ["DB", str(k + d)] if k >= c else t
    if t[0] in ("LAM", "ALL"):
        return [t[0], t[1], shift(t[2], d, c + 1)]
    if t[0] in ("AP", "IMP"):
        return [t[0], shift(t[1], d, c), shift(t[2], d, c)]
    if t[0] == "TPAP":
        return [t[0], shift(t[1], d, c), t[2]]
    return t


def subst(t, j, v):
    """t with (DB j) replaced by v, the variables above j lowered by one."""
    if t[0] == "DB":
        k = int(t[1])
        return shift(v, j) if k == j else (["DB", str(k - 1)] if k > j else t)
    if t[0] in ("LAM", "ALL"):
        return [t[0], t[1], subst(t[2], j + 1, v)]
    if t[0] in ("AP", "IMP"):
        return [t[0], subst(t[1], j, v), subst(t[2], j, v)]
    if t[0] == "TPAP":
        return [t[0], subst(t[1], j, v), t[2]]
    return t


def heads_ok(t):
    return not isinstance(t, list) or (t[0] in TERM_HEADS and
                                       all(heads_ok(x) for x in t[1:] if isinstance(x, list)
                                           and x and x[0] in TERM_HEADS | {"AP"}))


def beta(t, fuel=None):
    """The beta normal form of a term, or None past a step bound."""
    fuel = fuel if fuel is not None else [100000]
    fuel[0] -= 1
    if fuel[0] < 0:
        return None
    if t[0] == "AP":
        f = beta(t[1], fuel)
        if f is None:
            return None
        if f[0] == "LAM":
            return beta(subst(f[2], 0, t[2]), fuel)
        x = beta(t[2], fuel)
        return None if x is None else ["AP", f, x]
    if t[0] in ("LAM", "ALL"):
        b = beta(t[2], fuel)
        return None if b is None else [t[0], t[1], b]
    if t[0] == "IMP":
        a, b = beta(t[1], fuel), beta(t[2], fuel)
        return None if a is None or b is None else ["IMP", a, b]
    if t[0] == "TPAP":
        f = beta(t[1], fuel)
        return None if f is None else ["TPAP", f, t[2]]
    return t


def mutate(op, proof, other_known):
    """The mutated proof term, or None when the operator does not apply."""
    if op == "hyp":
        p = first(proof, is_head("HYP"))
        return None if p is None else put(proof, p, ["HYP", str(int(get(proof, p)[1]) + 1)])
    if op == "known":
        p = first(proof, is_head("KNOWN"))
        if p is None:
            return None
        h = get(proof, p)[1]
        alt = next((k for k in other_known if k != h), None)
        return None if alt is None else put(proof, p, ["KNOWN", alt])
    if op == "drop_pfap":
        p = first(proof, is_head("PPFAP"))
        return None if p is None else put(proof, p, get(proof, p)[1])
    if op == "tmap_db":
        p = first(proof, lambda t: is_head("PTMAP")(t) and free_db(t[2]) is not None)
        if p is None:
            return None
        node = get(proof, p)
        q = free_db(node[2])
        arg = put(node[2], q, ["DB", str(int(get(node[2], q)[1]) + 1)])
        return put(proof, p, ["PTMAP", node[1], arg])
    if op == "plam_prop":
        p = first(proof, is_head("PLAM"))
        if p is None:
            return None
        node = get(proof, p)
        return put(proof, p, ["PLAM", ["IMP", node[1], node[1]], node[2]])
    raise ValueError(op)


def replay(tool, profile, path, work):
    try:
        p = subprocess.run([tool, profile, str(path), "--work", str(work)], capture_output=True,
                           timeout=600)
    except subprocess.TimeoutExpired:
        return "timeout", None, ""
    out = (p.stdout + p.stderr).decode("utf-8", "replace")
    status = re.search(r"import status=(\S+)", out)
    stop = re.search(r"stopped at line (\d+): (.*)", out)
    return (status.group(1) if status else "none"), (int(stop.group(1)) if stop else None), \
        (stop.group(2) if stop else "")


def doc_jobs(rel, lines, profile, per_doc):
    """(rel, name, op, profile, row index, end index, theorem line, row text)
    for the chosen theorems: the control (row unchanged) and each mutant."""
    proofs = [i for i, l in enumerate(lines) if l.startswith("(PROOF ")]
    if not proofs:
        return []
    known = ['"%s"' % parse(l)[2].strip('"') for l in lines if l.startswith("(AXIOM ")]
    if per_doc <= 2 or len(proofs) <= per_doc:
        chosen = sorted({proofs[0], proofs[-1]}) if per_doc <= 2 else proofs
    else:
        chosen = sorted({proofs[(len(proofs) - 1) * k // (per_doc - 1)] for k in range(per_doc)})
    jobs = []
    for i in chosen:
        row = parse(lines[i])
        name = row[1].strip('"')
        thm = max(k for k in range(i) if lines[k].startswith("(THM "))
        thm_line = int(parse(lines[thm])[-1])
        end = next(k for k in range(i, len(lines)) if lines[k] == "(QED)")
        jobs.append((rel, name, "control", profile, i, end, thm_line, lines[i]))
        for op in OPERATORS:
            m = mutate(op, row[2], known)
            if m is not None:
                jobs.append((rel, name, op, profile, i, end, thm_line, show(["PROOF", row[1], m])))
    return jobs


def term_equivalent(original_row, mutated_row):
    """Whether a tmap_db mutant's argument has the original's beta normal form."""
    a, b = parse(original_row)[2], parse(mutated_row)[2]
    p = first(a, lambda t: is_head("PTMAP")(t) and free_db(t[2]) is not None)
    if p is None:
        return False
    x, y = get(a, p)[2], get(b, p)[2]
    if not (heads_ok(x) and heads_ok(y)):
        return False
    nx, ny = beta(x), beta(y)
    return nx is not None and nx == ny


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--replay", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--reference", required=True)
    ap.add_argument("--work", default="runtime/mg-elab/mutants")
    ap.add_argument("--docs", type=int, default=0, help="every k-th document with proofs (0: all)")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--budget", type=int, default=100000000,
                    help="the kernel's work budget per replay (mg_replay --work)")
    ap.add_argument("--per-doc", type=int, default=2,
                    help="theorems mutated per document, evenly spaced (2: the first and the last)")
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    ref = pathlib.Path(args.reference)
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    docs = []
    for rel, dargs in documents(args.root, args.files):
        k = key(rel)
        status, sexpr = ref / (k + ".status"), ref / (k + ".sexpr")
        if not status.exists() or status.read_text().strip() != "0":
            continue
        text = sexpr.read_text()
        if "(PROOF " not in text:
            continue
        profile = next((PROFILE_FLAGS[a] for a in dargs if a in PROFILE_FLAGS), "egal")
        docs.append((rel, text.split("\n"), profile))
    if args.docs:
        step = max(1, len(docs) // args.docs)
        docs = docs[::step][:args.docs]
    lines_of = {rel: lines for rel, lines, _ in docs}
    jobs = []
    for rel, lines, profile in docs:
        jobs.extend(doc_jobs(rel, lines, profile, args.per_doc))
    tally = {"control_ok": 0, "control_failed": 0, "refused": 0, "exhausted": 0, "equivalent": 0,
             "accepted": 0, "elsewhere": 0}
    notes = []

    def run(numbered):
        n, (rel, name, op, profile, i, end, thm_line, row) = numbered
        lines = lines_of[rel]
        path = work / ("%06d.%s.%s.%s.sexpr" % (n, key(rel), name, op))
        path.write_text("\n".join(lines[:i] + [row] + lines[i + 1:end + 1]) + "\n")
        status, stop, why = replay(args.replay, profile, path, args.budget)
        if op == "control":
            verdict = "control_ok" if status == "ok" else "control_failed"
        elif status == "ok":
            verdict = "equivalent" if op == "tmap_db" and term_equivalent(lines[i], row) else "accepted"
        elif stop == thm_line and name in why:
            # the kernel's budget running out also grants no authority
            verdict = "exhausted" if why.endswith(": exhausted") else "refused"
        else:
            verdict = "elsewhere"
        if verdict in ("control_ok", "refused", "exhausted", "equivalent"):
            path.unlink()
        return rel, name, op, verdict, status, stop, why, path

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for rel, name, op, verdict, status, stop, why, path in pool.map(run, enumerate(jobs)):
            tally[verdict] += 1
            if verdict == "control_failed":
                notes.append("CONTROL %s %s: %s %s (%s)" % (rel, name, status, why, path))
            elif verdict == "accepted":
                notes.append("ACCEPTED %s %s %s (%s)" % (rel, name, op, path))
            elif verdict == "elsewhere":
                notes.append("ELSEWHERE %s %s %s: line %s %s (%s)" % (rel, name, op, stop, why[:120], path))
    for n in notes:
        print(n)
    print("(MegalodonProofMutants docs=%d theorems=%d %s)" % (
        len(docs), sum(1 for j in jobs if j[2] == "control"),
        " ".join("%s=%d" % kv for kv in tally.items())))
    return 1 if tally["accepted"] or tally["elsewhere"] or tally["control_failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
