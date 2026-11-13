#!/usr/bin/env python3
"""Drains of a space grow with the space, not with its square.

Usage: check_space_read_scaling.py CETTA_RUNTIME_STATS_BINARY

A drain takes one job at a time and records it done, as an MM2 rule set or a
work queue does: by transform (native and authored), and by a loop over
`once` of a match.  Each firing reads the first job, removes it and adds a
record.  At two sizes, each drain answers every job, and its work per firing
stays within a bound that does not depend on the size: the candidate rows
its reads offer, the rows contractions of removed rows move, and the rows
copied for readers pinned to the space when it changed.  A read that
prepared every candidate, a contraction per removal, or a reader copying the
space per removal, makes one of them grow with the space.

Keyed shapes run at sizes on both sides of the one at which a space forms
relations, in PeTTa and in HE: a key that stands after a variable, read,
taken and removed in a full priority space, and a number as the key in HE,
which the space's general indexes select where the order's front does not;
a keyed take drain beside one row no search of the order places (a list
value with an open tail, a partial application) or places without a key (70
variables, a variable 600 deep); and takes of stored atoms that hold a
variable.  A read that walked every row of its front, a search that gave up
for one stranger, or a take that gathered its whole front, exceeds a bound.

Readers of a priority space that stay open across removals and additions
walk its order once: the rows an ordered walk reaches or passes stay within
a bound per row of the space.  An outer reader removing what it reads with
an inner reader doing the same over other rows; and one reader that, at its
first answer, removes the rows it has yet to read and adds as many that
stand after them.  A walk that began again from its last row at each step
passed the removed or the added rows once per step.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (300, 1200)
BOUNDS = {
    # Rows a read by head is offered.
    "space-head-index-row": 8,
    # Rows an ordered walk reaches before the row it answers with, and the
    # comparisons of its searches: a walk from the least row, or candidates
    # gathered and sorted at each read, grows with the space.
    "space-order-walk-row": 8,
    "space-order-compare": 400,
    "match-native-candidates": 8,
    "space-mark-settle-row": 8,
    "space-pinned-occurrences-detached-row": 0,
    "space-order-copy-row": 0,
    "space-stable-mask-source-row": 8,
}
# Drains whose reads by head are not bounded per firing, with the reason.
# The authored library is ordinary MeTTa and lists every match of a source
# before it fires one.  With a `let` among its sources a transform proposes
# every candidate before it validates any (the library's rule, which the
# native engine keeps), so each call reads every job.
UNBOUNDED = {
    "authored transform": ("space-head-index-row",),
    "transform with a computed source": ("space-head-index-row",),
}
# Work that must not grow with the drain at all.
ONCE = {
    "space-order-rebuild": 2,
}

TRANSFORM = """!(import! &self (library lib_transform))
!(bind! &q (new-space))
{adds}
(= (drain $q $k) (let $r (collapse (once (transform $q (take (job $j)) (done $j) $q)))
   (if (== $r ()) $k (drain $q (+ $k 1)))))
!(drain &q 0)
!(size-atom (collapse (match &q (done $j) x)))
"""

LOOP = """!(bind! &q (new-space))
{adds}
(= (drain $q $k) (let $js (collapse (once (match $q (job $j) $j)))
   (if (== $js ()) $k
     (let $j (car-atom $js)
       (let $_ (remove-atom $q (job $j))
         (let $_2 (add-atom $q (done $j)) (drain $q (+ $k 1))))))))
!(drain &q 0)
!(size-atom (collapse (match &q (done $j) x)))
"""

# The same drain answering its caller: the reader of the candidates is still
# open when the firing takes its job.
CALLER = """!(import! &self (library lib_transform))
!(bind! &q (new-space))
{adds}
(= (drain $q $k) (let $r (collapse (once (transform $q (take (job $j)) (done $j))))
   (if (== $r ()) $k (drain $q (+ $k 1)))))
!(drain &q 0)
!(+ {size} (size-atom (collapse (match &q (job $j) x))))
"""

# A source that computes keeps the reader open across the firing as well.
LET = """!(import! &self (library lib_transform))
!(bind! &q (new-space))
{adds}
(= (drain $q $k) (let $r (collapse (once (transform $q (, (take (job $j)) (let $d (done $j))) $d $q)))
   (if (== $r ()) $k (drain $q (+ $k 1)))))
!(drain &q 0)
!(size-atom (collapse (match &q (done $j) x)))
"""

# One reader of every job, each answer removing the job it read.
SWEEP = """!(bind! &q (new-space))
{adds}
(= (sweep $q) (collapse (match $q (job $j) (let $_ (remove-atom $q (job $j)) $j))))
!(size-atom (sweep &q))
!(+ {size} (size-atom (collapse (match &q (job $j) x))))
"""

# A priority space: the least job taken by a transform that answers its
# caller, and popped while a reader of the space is open.
PRIORITY = CALLER.replace("(new-space)", "(new-space priority)")
PRIORITY_POP = """!(bind! &q (new-space priority))
{adds}
(= (sweep $q) (collapse (match $q (job $j) (let $_ (space:priority:pop $q) $j))))
!(size-atom (sweep &q))
!(+ {size} (space-len &q))
"""

# An outer reader removes every (z i) it reads; at its last answer an inner
# reader removes every (a i) it reads, with the removed (z i) behind it.
NESTED_WALK = """!(bind! &p (new-space priority))
{adds}
(= (inner) (size-atom (collapse (match &p (a $v) (let $_ (remove-atom &p (a $v)) $v)))))
!(size-atom (collapse (match &p (z $i) (let $_ (remove-atom &p (z $i)) (if (== $i {last}) (inner) skip)))))
!(+ {size} (space-len &p))
"""

# One reader of the whole order: at its first answer it removes every (b i),
# which it has yet to read, and adds as many (bb i), which stand after them
# and which it does not read.
STRIPPED_WALK = """!(bind! &p (new-space priority))
(= (strip $n) (if (== $n 0) done (let* (($_ (remove-atom &p (b $n))) ($_2 (add-atom &p (bb $n)))) (strip (- $n 1)))))
!(add-atom &p (a 0))
{adds}
!(add-atom &p (c 0))
!(- (size-atom (collapse (match &p ($k $v) (if (== $k a) (let $_ (strip {size}) $v) $v)))) 2)
!(- (space-len &p) 2)
"""

# name, program, the atoms added per unit of size, bounds per row
WALKS = (
    ("nested readers removing what they read", NESTED_WALK,
     lambda i: ("!(add-atom &p (a %d))" % i, "!(add-atom &p (z %d))" % i)),
    ("a reader whose unread rows are removed and others added", STRIPPED_WALK,
     lambda i: ("!(add-atom &p (b %d))" % i,)),
)
WALK_BOUNDS = {
    "space-order-walk-row": 12,
    "space-pinned-occurrences-detached-row": 0,
    "space-order-copy-row": 0,
}

# The once-match loop on a space that also holds as many atoms headed by a
# number and by an expression, which no read by a symbol head can match.
LOOP_BESIDE = LOOP.replace("{adds}", "{adds}\n"
    "(= (fill $n) (if (== $n 0) done (let* (($_ (add-atom &q ($n x))) "
    "($_2 (add-atom &q ((w $n) x)))) (fill (- $n 1)))))\n"
    "!(fill {size})")

# The same drain with one atom holding a variable in the space, which no
# keyed read meets: the reads are still found by a search.
PRIORITY_OPEN = PRIORITY.replace("{adds}", "{adds}\n!(add-atom &q (zz $x))")
PRIORITY_FULL = PRIORITY.replace("(new-space priority)", "(new-space priority full)")
PRIORITY_FULL_OPEN = PRIORITY_OPEN.replace("(new-space priority)",
                                           "(new-space priority full)")
# Keys that are expressions, taken by a pattern whose first element is an
# expression with a ground front.
COMPOSITE = """!(import! &self (library lib_transform))
!(bind! &q (new-space priority))
(= (fill $n) (if (== $n 0) done (let $_ (add-atom &q ((w $n) c)) (fill (- $n 1)))))
!(fill {size})
(= (drain $q $k) (let $r (collapse (once (transform $q (take ((w $a) $c)) $a)))
   (if (== $r ()) $k (drain $q (+ $k 1)))))
!(drain &q 0)
!(+ {size} (space-len &q))
"""
COMPOSITE_FULL = COMPOSITE.replace("(new-space priority)", "(new-space priority full)")

# Reads, takes and removals keyed in ways the order's front does not decide,
# and drains beside rows no search of the order places.  Each fills its own
# store, on both sides of the size at which a space forms relations, and
# answers its size twice.
KEYED_SIZES = (1200, 5000)
FILL_LATE = "(= (fill $n) (if (== $n 0) done (let $_ (add-atom &p (k a $n)) (fill (- $n 1)))))"
# A key after a variable, (k $y K): a full space's indexes find its one row.
LATE_MATCH = """!(bind! &p (new-space priority full))
""" + FILL_LATE + """
(= (probe $k $hits) (if (== $k 0) $hits (let $r (collapse (once (match &p (k $y $k) $y))) (probe (- $k 1) (+ $hits (size-atom $r))))))
!(fill {size})
!(probe {size} 0)
!(space-len &p)
"""
LATE_TAKE = """!(import! &self (library lib_transform))
!(bind! &p (new-space priority full))
""" + FILL_LATE + """
(= (drain $k $hits) (if (== $k 0) $hits (let $r (collapse (once (transform &p (take (k $y $k)) $y))) (drain (- $k 1) (+ $hits (size-atom $r))))))
!(fill {size})
!(drain {size} 0)
!(+ {size} (space-len &p))
"""
LATE_REMOVE = """!(bind! &p (new-space priority full))
""" + FILL_LATE + """
(= (strip $k) (if (== $k 0) done (let $_ (remove-atom &p (k $y $k)) (strip (- $k 1)))))
!(fill {size})
!(space-len &p)
!(let $_ (strip {size}) (+ {size} (space-len &p)))
"""
# The same read in a full space holding lists stored as cons cells: the
# general indexes take such a row as standing for anything at its place, so
# they keep serving the read however many there are.
LATE_MATCH_ODD = """!(bind! &p (new-space priority full))
""" + FILL_LATE + """
(= (probe $k $hits) (if (== $k 0) $hits (let $r (collapse (once (match &p (k $y $k) $y))) (probe (- $k 1) (+ $hits (size-atom $r))))))
(= (odd $n) (if (== $n 0) done (let $l (cons-atom $n $t) (let $_ (add-atom &p (zz $l)) (odd (- $n 1))))))
!(fill {size})
!(odd {count})
!(probe {size} 0)
!(- (space-len &p) {count})
"""
# A number as the key: HE compares numbers by value, so the order's front
# ends before it and the indexes find the row.
NUMERIC = """!(bind! &p (new-space priority full))
(= (fill $n) (if (== $n 0) done (let* (($_ (add-atom &p (a $n))) ($_2 (add-atom &p (job $n)))) (fill (- $n 1)))))
(= (probe $k $hits) (if (== $k 0) $hits (let $r (collapse (once (match &p (job $k) $k))) (probe (- $k 1) (+ $hits (size-atom $r))))))
!(fill {size})
!(probe {size} 0)
!(- (space-len &p) {size})
"""
# The keyed take drain beside one row no search of the order places, or
# places without an ordering key.
STRANGER = """!(import! &self (library lib_transform))
!(bind! &p (new-space {kind}))
(= (fill $n) (if (== $n 0) done (let* (($_ (add-atom &p (a $n))) ($_2 (add-atom &p (job $n)))) (fill (- $n 1)))))
(= (drain $k $hits) (if (== $k 0) $hits (let $r (collapse (once (transform &p (take (job $j)) $j))) (drain (- $k 1) (+ $hits (size-atom $r))))))
!(fill {size})
{stranger}
!(drain {size} 0)
!(- (space-len &p) {extra})
"""
STRANGERS = {
    "an atom of 70 variables":
        "!(add-atom &p (zz %s))" % " ".join("$v%d" % i for i in range(70)),
    "an atom with a variable 600 deep":
        "!(add-atom &p (zz %s%s%s))" % ("(d " * 600, "$x", ")" * 600),
    "a list value with an open tail": "!(let $l (cons 1 $t) (add-atom &p (zz $l)))",
    "a partial application": "!(let $f (+ 1) (add-atom &p (zz $f $x)))",
}
# Odd rows in counts with no limit: lists stored as cons cells (an open
# tail, an improper end, a rest where a list ends inside a row of the very
# head the drain reads), partial applications.  Each count is added by a
# loop before the drain; the work per read must not grow with the count.
ODD_COUNTS = (1, 17, 100, 1000)
ODD_FORMS = {
    "lists with an open tail":
        "(let $l (cons-atom $n $t) (add-atom &p (zz $l)))",
    "lists with an improper end":
        "(let $l (cons-atom $n b) (add-atom &p (zz $l)))",
    "lists ending in a rest inside rows of the drained head":
        "(let $l (cons-atom $n $t) (add-atom &p (job $l extra)))",
    "partial applications":
        "(let $f (+ $n) (add-atom &p (zz $f $x)))",
}
ODD = """(= (odd $n) (if (== $n 0) done (let $_ {form} (odd (- $n 1)))))
!(odd {count})
"""
# Stored atoms that hold a variable, taken one at a time.
OPEN_TAKE = """!(import! &self (library lib_transform))
!(bind! &p (new-space {kind}))
(= (fill $n) (if (== $n 0) done (let $_ (add-atom &p (job $x $n)) (fill (- $n 1)))))
(= (drain $k $hits) (if (== $k 0) $hits (let $r (collapse (once (transform &p (take (job $y $z)) $z))) (drain (- $k 1) (+ $hits (size-atom $r))))))
!(fill {size})
!(drain {size} 0)
!(+ {size} (space-len &p))
"""
# name, language, program
KEYED = (
    ("full priority space, a key after a variable, read", "petta", LATE_MATCH),
    ("full priority space, a key after a variable, read (HE)", "he", LATE_MATCH),
) + tuple(
    ("full priority space, a key after a variable, read beside %d open lists"
     % count, "petta", LATE_MATCH_ODD.replace("{count}", str(count)))
    for count in (1, 17, 100, 1000)
) + (
    ("full priority space, a key after a variable, taken", "petta", LATE_TAKE),
    ("full priority space, a key after a variable, removed", "petta", LATE_REMOVE),
    ("full priority space, a number as the key (HE)", "he", NUMERIC),
) + tuple(
    ("%s, a keyed take drain beside %s" % (kind, what), "petta",
     STRANGER.replace("{kind}", kind).replace("{stranger}", line)
     .replace("{extra}", "1"))
    for kind in ("priority", "priority full")
    for what, line in STRANGERS.items()
) + tuple(
    ("%s, a keyed take drain beside %d %s" % (kind, count, what), "petta",
     STRANGER.replace("{kind}", kind).replace(
         "{stranger}", ODD.replace("{form}", form).replace("{count}", str(count)))
     .replace("{extra}", str(count)))
    for kind in ("priority", "priority full")
    for what, form in ODD_FORMS.items()
    for count in ODD_COUNTS
) + tuple(
    ("%s, takes of stored atoms that hold a variable" % kind, "petta",
     OPEN_TAKE.replace("{kind}", kind))
    for kind in ("priority", "priority full")
)
KEYED_BOUNDS = {
    "space-order-walk-row": 8,
    "space-order-compare": 400,
    "match-native-candidates": 8,
    "space-head-index-row": 8,
    "space-pinned-occurrences-detached-row": 0,
    "space-order-copy-row": 0,
}

DRAINS = (
    ("transform", TRANSFORM, {}),
    ("authored transform", TRANSFORM.replace("lib_transform)", "lib_transform_swi)"), {}),
    ("once-match loop", LOOP, {}),
    ("once-match loop beside atoms headed by numbers and expressions",
     LOOP_BESIDE, {}),
    ("transform answering its caller", CALLER, {}),
    ("transform with a computed source", LET, {}),
    ("one reader removing what it reads", SWEEP, {}),
    ("priority space, transform answering its caller", PRIORITY,
     {"CETTA_TEST_PROFILE": "extended"}),
    ("priority space, pop under an open reader", PRIORITY_POP,
     {"CETTA_TEST_PROFILE": "extended"}),
    ("priority space holding one atom with a variable", PRIORITY_OPEN,
     {"CETTA_TEST_PROFILE": "extended"}),
    ("full priority space", PRIORITY_FULL, {"CETTA_TEST_PROFILE": "extended"}),
    ("full priority space holding one atom with a variable", PRIORITY_FULL_OPEN,
     {"CETTA_TEST_PROFILE": "extended"}),
    ("priority space, keys that are expressions", COMPOSITE,
     {"CETTA_TEST_PROFILE": "extended"}),
    ("full priority space, keys that are expressions", COMPOSITE_FULL,
     {"CETTA_TEST_PROFILE": "extended"}),
)


def counters(report):
    found = {}
    for line in report.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == "runtime-counter":
            found[fields[1]] = int(fields[2])
    return found


def main():
    binary = os.path.abspath(sys.argv[1])
    failed = False
    with tempfile.TemporaryDirectory(prefix="cetta-space-reads-") as work:
        for name, source, env_extra in DRAINS:
            for size in SIZES:
                adds = "\n".join(f"!(add-atom &q (job j{i}))" for i in range(size))
                path = os.path.join(work, f"drain_{size}.metta")
                with open(path, "w") as f:
                    f.write(source.format(adds=adds, size=size))
                env = dict(os.environ)
                env.update(CETTA_PETTA_SEARCH_MACHINE="1", **env_extra)
                profile = env.pop("CETTA_TEST_PROFILE", None)
                command = [binary, "--emit-runtime-stats", "--lang", "petta"]
                if profile:
                    command += ["--profile", profile]
                p = subprocess.run(command + [path],
                                   cwd=ROOT, env=env, capture_output=True, text=True,
                                   timeout=600)
                answers = [l for l in p.stdout.splitlines() if l != "true"]
                if p.returncode != 0 or answers[-2:] != [str(size), str(size)]:
                    print(f"FAIL: {name} drain of {size}: answered {answers[-2:]}")
                    failed = True
                    continue
                found = counters(p.stdout + p.stderr)
                for counter, bound in BOUNDS.items():
                    value = found.get(counter, 0)
                    if counter in UNBOUNDED.get(name, ()):
                        continue
                    if value > bound * size:
                        print(f"FAIL: {name} drain of {size}: {counter} {value} "
                              f"exceeds {bound} per firing")
                        failed = True
                for counter, bound in ONCE.items():
                    value = found.get(counter, 0)
                    if value > bound:
                        print(f"FAIL: {name} drain of {size}: {counter} {value} "
                              f"exceeds {bound} in all")
                        failed = True
        for name, source, rows in WALKS:
            for size in SIZES:
                adds = "\n".join(line for i in range(1, size + 1) for line in rows(i))
                path = os.path.join(work, f"walk_{size}.metta")
                with open(path, "w") as f:
                    f.write(source.format(adds=adds, size=size, last=size))
                env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1")
                p = subprocess.run([binary, "--emit-runtime-stats", "--lang", "petta",
                                    "--profile", "extended", path],
                                   cwd=ROOT, env=env, capture_output=True, text=True,
                                   timeout=600)
                answers = [l for l in p.stdout.splitlines() if l != "true"]
                if p.returncode != 0 or answers[-2:] != [str(size), str(size)]:
                    print(f"FAIL: {name}, {size} rows: answered {answers[-2:]}")
                    failed = True
                    continue
                found = counters(p.stdout + p.stderr)
                for counter, bound in WALK_BOUNDS.items():
                    value = found.get(counter, 0)
                    if value > bound * size:
                        print(f"FAIL: {name}, {size} rows: {counter} {value} "
                              f"exceeds {bound} per row")
                        failed = True
                for counter, bound in ONCE.items():
                    value = found.get(counter, 0)
                    if value > bound:
                        print(f"FAIL: {name}, {size} rows: {counter} {value} "
                              f"exceeds {bound} in all")
                        failed = True
        for name, lang, source in KEYED:
            for size in KEYED_SIZES:
                path = os.path.join(work, f"keyed_{size}.metta")
                with open(path, "w") as f:
                    f.write(source.replace("{size}", str(size)))
                env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1")
                p = subprocess.run([binary, "--emit-runtime-stats", "--lang", lang,
                                    "--profile", "extended", path],
                                   cwd=ROOT, env=env, capture_output=True, text=True,
                                   timeout=600)
                answers = [l.strip("[]") for l in p.stdout.splitlines()
                           if l not in ("true", "[()]", "()", "[done]", "done")]
                if p.returncode != 0 or answers[-2:] != [str(size), str(size)]:
                    print(f"FAIL: {name}, {size} rows: answered {answers[-2:]}")
                    failed = True
                    continue
                found = counters(p.stdout + p.stderr)
                for counter, bound in KEYED_BOUNDS.items():
                    value = found.get(counter, 0)
                    if value > bound * size:
                        print(f"FAIL: {name}, {size} rows: {counter} {value} "
                              f"exceeds {bound} per read")
                        failed = True
                for counter, bound in ONCE.items():
                    value = found.get(counter, 0)
                    if value > bound:
                        print(f"FAIL: {name}, {size} rows: {counter} {value} "
                              f"exceeds {bound} in all")
                        failed = True
        if failed:
            return 1
    print(f"PASS: {len(KEYED)} keyed shapes (a key after a variable read, taken "
          f"and removed; a number as the key in HE; keyed take drains beside a "
          f"keyless row and beside 1 to 1,000 lists stored as cells or partial "
          f"applications; takes of atoms holding a variable) take bounded work "
          f"per read at {KEYED_SIZES[0]} and {KEYED_SIZES[1]} rows")
    print(f"PASS: {len(WALKS)} walks of a priority space by readers open across "
          f"removals and additions pass a bounded number of rows per row")
    print(f"PASS: {len(DRAINS)} drains (by transform into a space and answering "
          f"its caller, with a computed source, as authored, by a once-match loop, "
          f"by one reader removing what it reads, and of a priority space, strict "
          f"and full, with an atom holding a variable in it and with keys that are "
          f"expressions) take bounded work per firing at {SIZES[0]} and {SIZES[1]} "
          f"jobs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
