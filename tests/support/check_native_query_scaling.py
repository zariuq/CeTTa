#!/usr/bin/env python3
"""A rule deriving a relation by a join runs without per-row unification,
and a lookup reads the rows its key selects, not the space.

Usage: check_native_query_scaling.py CETTA_RUNTIME_STATS_BINARY [--reference]

A function whose one equation matches a two-way join of `edge` rows and adds
`(trans x z)` into a set space is collected whole, over random graphs at two
sizes.  The native query path must answer every row of the join, and the
unification attempts of the whole run must stay within a bound that does not
depend on the size: the ordinary path makes one per row.

Then a function collecting one customer's orders is called for every
customer, at two sizes four times apart.  The native path must answer every
call, and the rows it reads (its index loops' candidates and the rows a
relational plan prepares from) may grow at most six times: a lookup that
read every row of the space would make them grow sixteen times.

Last, an odd-even sort collects each phase's swaps and then makes them
through `(let P (superpose L) B)`, at 30 and 100 cells.  The native path
must answer every swap and make each of its four effects, whether the
collection and the spread run apart or as one region read on the
collection's snapshot, the run may
make at most four unification attempts per phase however many swaps a phase
makes (the ordinary path makes several per swap), and settling the removed
rows' marks may read at most six rows for each row removed, however many
batches of additions the phases append: a settle the marks start reads at most
two rows for each it removes, readers that need settled rows start a few
more, and a space settled at every batch reads all its rows each time, tens
of rows for each row removed.  The same work stays within four passes over
every row the run can hold, its initial rows and two for each swap; a dense
representation can compact fewer rows more often.  The larger sort runs
again with its conjunction written in reverse, and may read at most twice
the rows the conjunction as written reads: the join orders the patterns by
what the bindings fix and, among patterns fixed alike, by the rows their
probes meet, whatever order they are written in.

Then cells (p i) and rows holding constructor patterns, (step (p $u)
(q $u)) and (step (q $u) (r $u)), as Hexlife's neighbour relation holds
them, at 200 and 800 cells: each cell's successors are counted, its
two-step successors are counted, and its successors are collected.  The
rows meet each ground cell one way, so the run makes no more unification
attempts at 800 cells than the bound: the ordinary path unifies each cell
with each row.

With --reference the ordinary path runs, and the check is expected to fail.
"""
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = ((80, 400), (320, 1600))
BOUND = 16

PROGRAM = """{facts}
!(bind! &t (new-space set))
(= (rule-trans) (match &self (, (edge $x $y) (edge $y $z)) (add-atom &t (trans $x $z))))
!(size-atom (collapse (rule-trans)))
!(size-atom (collapse (match &t (trans $x $z) t)))
"""

CUSTOMERS = (200, 800)
ORDERS = 5
GROWTH = 6

LOOKUPS = """{facts}
(= (orders-of $c) (collapse (match &self (order $c $o) $o)))
!(size-atom (collapse (match &self (customer $c) (size-atom (orders-of $c)))))
"""

CELLS = (30, 100)
SORT_CONJUNCTION = "(, (parity $i $p) (succ $i $si) (A $i $e) (A $si $se) (lt $se $e))"
SORT_REVERSED = "(, (lt $se $e) (A $si $se) (A $i $e) (succ $i $si) (parity $i $p))"
SETTLE_ROWS_PER_REMOVED = 6


def sort_settle_work_limit(initial_rows, swaps):
    # Initial storage includes the dialect's prelude; each swap removes two
    # occurrences. The initial length observation does not enumerate rows.
    # Preserve the original four-full-pass budget, expressed in actual row
    # visits. At most initial_rows + 2*swaps rows can exist in this run;
    # many small compactions may cost less than four passes over that set.
    return 4 * (initial_rows + 2 * swaps)

SORT = """{facts}
(= (phase $p)
   (let $swaps (collapse (match &self (, (parity $i $p) (succ $i $si) (A $i $e) (A $si $se) (lt $se $e))
                                (swap $i $e $si $se)))
     (size-atom (collapse (let (swap $i $e $si $se) (superpose $swaps)
       (let* (($a (remove-atom &self (A $i $e))) ($b (remove-atom &self (A $si $se)))
              ($c (add-atom &self (A $i $se))) ($d (add-atom &self (A $si $e))))
         t))))))
(= (rounds $k) (if (== $k 0) done (let* (($o (phase odd)) ($e (phase even))) (rounds (- $k 1)))))
!(space-len &self)
!(rounds {cells})
!(size-atom (collapse (match &self (, (A $i $e) (A $si $se) (succ $i $si) (lt $se $e)) bad)))
!(size-atom (collapse (match &self (A $i $e) t)))
"""


OPEN_CELLS = (200, 800)

OPEN = """{facts}
(step (p $u) (q $u))
(step (q $u) (r $u))
(step (r $u) $u)
!(size-atom (collapse (match &self (, (cell $c) (step $c $d)) $d)))
!(size-atom (collapse (match &self (, (cell $c) (step $c $d) (step $d $e)) $e)))
!(let $h (collapse (match &self (, (cell $c) (step $c $d)) (pair $c $d))) (size-atom $h))
"""


def sort_program(cells, rng):
    values = list(range(cells))
    rng.shuffle(values)
    facts = [f"(A {i} v{values[i]:05d})" for i in range(cells)]
    facts += [f"(succ {i} {i + 1})" for i in range(cells)]
    facts += [f"(parity {i} {'even' if i % 2 == 0 else 'odd'})" for i in range(cells)]
    facts += [f"(lt v{a:05d} v{b:05d})" for b in range(cells) for a in range(b)]
    swaps = 0
    for _ in range(cells):
        for parity in (1, 0):
            for i in range(cells - 1):
                if i % 2 == parity and values[i + 1] < values[i]:
                    values[i], values[i + 1] = values[i + 1], values[i]
                    swaps += 1
    return SORT.format(facts="\n".join(facts), cells=cells), swaps


def counters(report):
    found = {}
    for line in report.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == "runtime-counter":
            found[fields[1]] = int(fields[2])
    return found


def main():
    binary = os.path.abspath(sys.argv[1])
    env = dict(os.environ)
    env.pop("CETTA_NATIVE_QUERY_REFERENCE", None)
    if "--reference" in sys.argv[2:]:
        env["CETTA_NATIVE_QUERY_REFERENCE"] = "1"
    rng = random.Random(20261027)
    with tempfile.TemporaryDirectory(prefix="cetta-native-query-scaling-") as work:
        for nodes, edges in SIZES:
            pairs = set()
            while len(pairs) < edges:
                pairs.add((rng.randrange(nodes), rng.randrange(nodes)))
            facts = "\n".join(f"(edge {x} {y})" for x, y in sorted(pairs))
            joined = sum(1 for x, y in pairs for y2, z in pairs if y == y2)
            derived = len({(x, z) for x, y in pairs for y2, z in pairs if y == y2})
            path = os.path.join(work, f"trans_{edges}.metta")
            with open(path, "w") as f:
                f.write(PROGRAM.format(facts=facts))
            p = subprocess.run([binary, "--profile", "extended", "--lang", "petta",
                                "--emit-runtime-stats", path], cwd=ROOT, env=env,
                               capture_output=True, text=True, timeout=600)
            answers = p.stdout.split()
            found = counters(p.stderr)
            if p.returncode != 0 or answers[-2:] != [str(joined), str(derived)]:
                print(f"FAIL: {edges} edges answered {answers[-2:]}, "
                      f"expected {[joined, derived]}")
                return 1
            if found.get("native-query-answer", 0) != joined:
                print(f"FAIL: over {edges} edges the native path answered "
                      f"{found.get('native-query-answer', 0)} of {joined} rows")
                return 1
            unified = found.get("unification-attempt", 0)
            if unified > BOUND:
                print(f"FAIL: over {edges} edges the run made {unified} "
                      f"unification attempts for {joined} rows, more than {BOUND}")
                return 1
        read = []
        for customers in CUSTOMERS:
            facts = "\n".join(
                [f"(customer u{c})" for c in range(customers)] +
                [f"(order u{c} o{c}_{k})" for c in range(customers)
                 for k in range(ORDERS)])
            path = os.path.join(work, f"lookups_{customers}.metta")
            with open(path, "w") as f:
                f.write(LOOKUPS.format(facts=facts))
            p = subprocess.run([binary, "--profile", "extended", "--lang", "petta",
                                "--emit-runtime-stats", path], cwd=ROOT, env=env,
                               capture_output=True, text=True, timeout=600)
            answers = p.stdout.split()
            found = counters(p.stderr)
            if p.returncode != 0 or answers[-1:] != [str(customers)]:
                print(f"FAIL: {customers} customers answered {answers[-1:]}, "
                      f"expected {customers}")
                return 1
            if found.get("native-query-admission", 0) < customers:
                print(f"FAIL: over {customers} customers the native path "
                      f"answered {found.get('native-query-admission', 0)} lookups")
                return 1
            read.append(found.get("native-query-loop-row", 0) +
                        found.get("match-relational-prepare-rows", 0))
        for cells in CELLS:
            program, swaps = sort_program(cells, rng)
            path = os.path.join(work, f"sort_{cells}.metta")
            with open(path, "w") as f:
                f.write(program)
            p = subprocess.run([binary, "--profile", "extended", "--lang", "petta",
                                "--emit-runtime-stats", path], cwd=ROOT, env=env,
                               capture_output=True, text=True, timeout=600)
            answers = p.stdout.split()
            found = counters(p.stderr)
            if p.returncode != 0 or answers[-3:] != ["done", "0", str(cells)]:
                print(f"FAIL: the sort of {cells} cells answered {answers[-3:]}")
                return 1
            if (found.get("native-query-answer", 0) < swaps or
                    found.get("native-query-effect-instance", 0) < 4 * swaps):
                print(f"FAIL: over {cells} cells the native path answered "
                      f"{found.get('native-query-answer', 0)} of {swaps} swap "
                      f"rows and made {found.get('native-query-effect-instance', 0)} "
                      f"of {4 * swaps} effects")
                return 1
            phases = 2 * cells
            if found.get("unification-attempt", 0) > 4 * phases:
                print(f"FAIL: the sort of {cells} cells made "
                      f"{found.get('unification-attempt', 0)} unification "
                      f"attempts over {phases} phases and {swaps} swaps")
                return 1
            removed = found.get("space-marked-row", 0)
            if found.get("space-mark-settle-row", 0) > SETTLE_ROWS_PER_REMOVED * removed:
                print(f"FAIL: the sort of {cells} cells settled its marks "
                      f"{found.get('space-mark-settle', 0)} times, reading "
                      f"{found.get('space-mark-settle-row', 0)} rows for "
                      f"{removed} removed")
                return 1
            settled_rows = found.get("space-mark-settle-row", 0)
            settle_limit = sort_settle_work_limit(int(answers[0]), swaps)
            if settled_rows > settle_limit:
                print(f"FAIL: the sort of {cells} cells scanned {settled_rows} "
                      f"rows settling its marks, more than {settle_limit}")
                return 1
            if cells == CELLS[-1]:
                assert program.count(SORT_CONJUNCTION) == 1
                path = os.path.join(work, f"sort_{cells}_reversed.metta")
                with open(path, "w") as f:
                    f.write(program.replace(SORT_CONJUNCTION, SORT_REVERSED))
                p = subprocess.run([binary, "--profile", "extended", "--lang",
                                    "petta", "--emit-runtime-stats", path],
                                   cwd=ROOT, env=env, capture_output=True,
                                   text=True, timeout=600)
                if p.returncode != 0 or p.stdout.split()[-3:] != answers[-3:]:
                    print(f"FAIL: the sort of {cells} cells written in reverse "
                          f"answered {p.stdout.split()[-3:]}")
                    return 1
                forward = found.get("native-query-loop-row", 0)
                reverse = counters(p.stderr).get("native-query-loop-row", 0)
                if reverse > 2 * forward:
                    print(f"FAIL: the sort of {cells} cells read {reverse} rows "
                          f"with its conjunction written in reverse, "
                          f"{forward} as written")
                    return 1
    with tempfile.TemporaryDirectory(prefix="cetta-native-query-open-") as work:
        for cells in OPEN_CELLS:
            facts = "\n".join(f"(cell (p {i}))" for i in range(cells))
            path = os.path.join(work, f"open_{cells}.metta")
            with open(path, "w") as f:
                f.write(OPEN.format(facts=facts))
            p = subprocess.run([binary, "--profile", "extended", "--lang", "petta",
                                "--emit-runtime-stats", path], cwd=ROOT, env=env,
                               capture_output=True, text=True, timeout=600)
            answers = p.stdout.split()
            expected = [str(cells), str(cells), str(cells)]
            if p.returncode != 0 or answers[-3:] != expected:
                print(f"FAIL: over {cells} cells the open rows answered "
                      f"{answers[-3:]}, expected {expected}")
                return 1
            found = counters(p.stderr)
            if found.get("unification-attempt", 0) > BOUND:
                print(f"FAIL: over {cells} cells the rows holding patterns "
                      f"took {found.get('unification-attempt', 0)} "
                      f"unification attempts")
                return 1
    if read[1] > GROWTH * max(read[0], 1):
        print(f"FAIL: the rows read grew from {read[0]} to {read[1]} when the "
              f"customers grew {CUSTOMERS[1] // CUSTOMERS[0]} times, more than "
              f"{GROWTH} times")
        return 1
    print(f"PASS: the join rule answers every row natively, with at most {BOUND} "
          f"unification attempts over {SIZES[0][1]} and {SIZES[1][1]} edges, and "
          f"lookups for {CUSTOMERS[0]} and {CUSTOMERS[1]} customers read "
          f"{read[0]} then {read[1]} rows, and sorts of {CELLS[0]} and "
          f"{CELLS[1]} cells make their swaps natively, settling their marks "
          f"with at most {SETTLE_ROWS_PER_REMOVED} rows read for each removed "
          f"and within four passes over their rows, reading as many rows "
          f"with the conjunction written in reverse, "
          f"and rows holding patterns meet "
          f"{OPEN_CELLS[0]} and {OPEN_CELLS[1]} cells one way")
    return 0


if __name__ == "__main__":
    sys.exit(main())
