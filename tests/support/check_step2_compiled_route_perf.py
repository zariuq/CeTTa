#!/usr/bin/env python3
"""End-to-end measurement of automatic compiled evaluation against the
interpreter baseline (`CETTA_TCALC_EVAL=0`) in both HE and PeTTa across all
9 named programs:
  1. count           (space data count + recursive integer count)
  2. nrev-300        (benchmarks/battery/nrev.metta.in)
  3. queens-8        (benchmarks/battery/queens.metta.in)
  4. bfib-26         (doubly recursive symbolic Fibonacci)
  5. fib-34          (doubly recursive integer Fibonacci)
  6. pln-chainer     (data-heavy PLN deduction + list/HO revision chainer)
  7. metamath-check  (data-heavy Hilbert/Metamath proof verifier with lists, case & HO)
  8. graph-reach     (indexed DAG multiset + cyclic presence-only reachability)
  9. join-heavy      (4-way conjunctive match in unjoined written order)

Automatic route choice rule:
  - Prepared-pure machine runs linear/single-result pure calls (`count-down`,
    `nrev-300`) directly.
  - One-shot single-atom full scans (`count-items`) stay on the interpreter /
    search machine in both dialects with zero compilation overhead.
  - Compiled tcalc SLG with demand-built argument-key indexes & join planning runs:
      * PeTTa: same-SCC branching recursion (`bfib-26`, `fib-34`) OR
               data-matching calls with recursion, multi-leg joins, or
               higher-order list batching (`pln-chainer`, `metamath-check`,
               `graph-reach`, `join-heavy`), plus presence-only `unique`.
               Non-overlapping backtracking (`queens-8`) stays on PeTTa's
               search machine with zero compilation overhead.
      * HE:    any recursive function (`queens-8`, `bfib-26`, `fib-34`) OR
               data-matching calls with recursion, multi-leg joins, or
               higher-order list batching (`pln-chainer`, `metamath-check`,
               `graph-reach`, `join-heavy`), plus `unique`.

Verifies exact multiset agreement on every program in both dialects and asserts
that every entry head compiles cleanly under `tcalc:space-program`.
"""

import collections
import os
import statistics
import subprocess
import sys
import tempfile
import time


def peano(n: int) -> str:
    s = "Z"
    for _ in range(n):
        s = f"(S {s})"
    return s


def parse_sexp_items(text: str) -> list[str]:
    s = text.strip()
    if not (s.startswith("(") and s.endswith(")")):
        return [s]
    inner = s[1:-1].strip()
    if not inner:
        return []
    items = []
    depth = 0
    in_str = False
    esc = False
    start = None
    for i, ch in enumerate(inner):
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            if start is None:
                start = i
            in_str = True
        elif ch == "(":
            if start is None:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0 and start is not None:
                items.append(inner[start : i + 1])
                start = None
        elif ch.isspace():
            if depth == 0 and start is not None:
                items.append(inner[start:i])
                start = None
        else:
            if start is None:
                start = i
    if start is not None:
        items.append(inner[start:])
    return items


def run_timed(
    cetta: str, lang: str, code: str, env_overrides: dict[str, str], trials: int = 5
) -> tuple[float, list[str]]:
    env = os.environ.copy()
    env.update(env_overrides)
    with tempfile.NamedTemporaryFile(
        "w", suffix=".metta", delete=False, encoding="utf-8"
    ) as tf:
        tf.write(code)
        path = tf.name
    times = []
    last_lines: list[str] = []
    try:
        for _ in range(trials):
            t0 = time.perf_counter()
            proc = subprocess.run(
                [cetta, "--lang", lang, "--quiet", path],
                capture_output=True,
                text=True,
                timeout=60,
                env=env,
            )
            dt = (time.perf_counter() - t0) * 1000.0
            if proc.returncode != 0:
                raise RuntimeError(
                    f"cetta --lang {lang} env={env_overrides} failed:\n"
                    f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
                )
            times.append(dt)
            lines = []
            for raw in proc.stdout.splitlines():
                s = raw.strip()
                if not s:
                    continue
                if lang == "he" and s.startswith("[") and s.endswith("]"):
                    s = s[1:-1].strip()
                lines.append(s)
            last_lines = lines
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass
    return statistics.median(times), last_lines


def build_programs(root: str) -> list[tuple[str, str, str, bool, dict[str, str]]]:
    """Returns list of (name, entry_head, metta_code, is_multiset_output, expected_route)."""
    # 1. count: recursive counter + space-data count
    count_code = "\n".join(
        [f"(item {i})" for i in range(1, 201)]
        + [
            "(= (count-down $n) (if (<= $n 0) 0 (+ 1 (count-down (- $n 1)))))",
            "(= (count-items) (match &self (item $x) 1))",
            "!(count-down 1000)",
            "!(collapse (count-items))",
        ]
    )

    # 2. naive reverse (verbatim benchmarks/battery/nrev.metta.in)
    with open(os.path.join(root, "benchmarks/battery/nrev.metta.in")) as f:
        nrev_code = f.read().replace("@N@", "300")

    # 3. queens (verbatim benchmarks/battery/queens.metta.in wrapped in collapse for multiset compare)
    with open(os.path.join(root, "benchmarks/battery/queens.metta.in")) as f:
        queens_raw = f.read().replace("@N@", "8")
    queens_code = queens_raw.replace(
        "!(place 8 (upto 8) Nil)", "!(collapse (place 8 (upto 8) Nil))"
    )

    # 4a. doubly recursive symbolic bfib(26)
    bfib_code = f"""
(= (inc Zero) (B1 Zero))
(= (inc (B0 $x)) (B1 $x))
(= (inc (B1 $x)) (B0 (inc $x)))
(= (badd Zero $y) $y)
(= (badd (B0 $x) Zero) (B0 $x))
(= (badd (B1 $x) Zero) (B1 $x))
(= (badd (B0 $x) (B0 $y)) (B0 (badd $x $y)))
(= (badd (B0 $x) (B1 $y)) (B1 (badd $x $y)))
(= (badd (B1 $x) (B0 $y)) (B1 (badd $x $y)))
(= (badd (B1 $x) (B1 $y)) (B0 (inc (badd $x $y))))
(= (bfib Z) Zero)
(= (bfib (S Z)) (B1 Zero))
(= (bfib (S (S $n))) (badd (bfib (S $n)) (bfib $n)))
!(bfib {peano(26)})
"""

    # 4b. doubly recursive integer fib(34)
    fib_code = """
(= (fib $n) (if (<= $n 1) $n (+ (fib (- $n 1)) (fib (- $n 2)))))
!(fib 34)
"""

    # 5. Data-heavy PLN chainer: 120 concepts, 250+ implications, indexed lookups,
    #    overlapping DAG deduction + higher-order foldl-atom revision over target lists
    pln_facts = []
    for i in range(1, 121):
        pln_facts.append(f"(concept-tv n{i} (stv 0.8 0.9))")
    for i in range(1, 120):
        pln_facts.append(f"(imp-tv n{i} n{i+1} (stv 0.85 0.8))")
        if i + 2 <= 120:
            pln_facts.append(f"(imp-tv n{i} n{i+2} (stv 0.75 0.7))")
        if i % 5 == 0 and i + 3 <= 120:
            pln_facts.append(f"(imp-tv n{i} n{i+3} (stv 0.7 0.65))")
    pln_code = "\n".join(pln_facts) + """
(= (c2w $c) (/ $c (- 1 $c)))
(= (w2c $w) (/ $w (+ $w 1)))
(= (tv-ded (stv $sB $cB) (stv $sC $cC) (stv $sAB $cAB) (stv $sBC $cBC))
   (let* (($sAC (+ (* $sAB $sBC) (/ (* (* (- 1 $sAB) (- $sC (* $sB $sBC))) 1.0) (- 1 $sB))))
          ($cAC (* (* $cAB $cBC) 0.9)))
     (stv $sAC $cAC)))
(= (tv-rev (stv $f1 $c1) (stv $f2 $c2))
   (let* (($w1 (c2w $c1)) ($w2 (c2w $c2)) ($w (+ $w1 $w2))
          ($f (/ (+ (* $w1 $f1) (* $w2 $f2)) $w)) ($c (w2c $w)))
     (stv $f $c)))
(= (pln-chain $src $dst 0) (empty))
(= (pln-chain $src $dst $d)
   (if (<= $d 0) (empty)
       (match &self (imp-tv $src $dst $tv) $tv)))
(= (pln-chain $src $dst $d)
   (if (<= $d 1) (empty)
       (match &self (imp-tv $src $mid $tv1)
         (let* (($tvB (match &self (concept-tv $mid $t) $t))
                ($tvC (match &self (concept-tv $dst $t) $t))
                ($tv2 (pln-chain $mid $dst (- $d 1))))
           (tv-ded $tvB $tvC $tv1 $tv2)))))
(= (pln-score-list $pairs)
   (foldl-atom $pairs (stv 0.5 0.5) $acc $pair
     (let ($s $t) $pair
       (match &self (imp-tv $s $t $tv)
         (tv-rev $acc $tv)))))
!(collapse (pln-chain n1 n18 12))
!(pln-score-list ((n1 n2) (n2 n3) (n3 n4) (n4 n5) (n5 n6) (n6 n7) (n7 n8) (n8 n9) (n9 n10) (n10 n11) (n11 n12) (n12 n13)))
"""

    # 6. Data-heavy Metamath proof check: 400+ axioms in &self, substitution helper
    #    over expression lists (`case` + `map-atom`), DAG proof checking, and
    #    batch verification over proof trees
    mm_facts = [
        "(mm-axiom ax-k1 (Imp A (Imp B A)))",
        "(mm-axiom ax-k2 (Imp A (Imp (Imp B A) A)))",
        "(mm-axiom ax-s (Imp (Imp A (Imp (Imp B A) A)) (Imp (Imp A (Imp B A)) (Imp A A))))",
        "(mm-axiom ax-base P0)",
    ]
    for i in range(0, 400):
        mm_facts.append(f"(mm-axiom ax-p{i} (Imp P{i} P{i+1}))")
    mm_code = "\n".join(mm_facts) + """
(= (mm-subst $v $r $form)
   (case $form
     (((Imp $a $b) (Imp (mm-subst $v $r $a) (mm-subst $v $r $b)))
      ($x (if (== $x $v) $r $x)))))
(= (mm-check (Ax $name))
   (match &self (mm-axiom $name $stmt) $stmt))
(= (mm-check (Inst $prf $v $r))
   (mm-subst $v $r (mm-check $prf)))
(= (mm-check (MP $p-imp $p-ant))
   (let* (($f-imp (mm-check $p-imp))
          ($f-ant (mm-check $p-ant)))
     (case $f-imp
       (((Imp $a $b) (if (== $a $f-ant) $b (empty)))))))
(= (mm-check-all $prfs)
   (map-atom $prfs $p (mm-check $p)))
(= (mm-verify-many $n $prf)
   (if (<= $n 0) (mm-check $prf)
       (let $_ (mm-check $prf) (mm-verify-many (- $n 1) $prf))))

!(mm-verify-many 1200 (MP (Ax ax-p7) (MP (Ax ax-p6) (MP (Ax ax-p5) (MP (Ax ax-p4) (MP (Ax ax-p3) (MP (Ax ax-p2) (MP (Ax ax-p1) (MP (Ax ax-p0) (Ax ax-base))))))))))
!(mm-check-all ((MP (MP (Ax ax-s) (Ax ax-k2)) (Ax ax-k1)) (Inst (MP (MP (Ax ax-s) (Ax ax-k2)) (Ax ax-k1)) A P42) (MP (Ax ax-p1) (MP (Ax ax-p0) (Ax ax-base)))))
"""

    # 7. Graph reachability: 180-node layered diamond DAG (multiset exact path
    #    reachability to a target node with overlapping subproblems) + cyclic
    #    reachability under presence-only `unique`
    graph_facts = []
    for i in range(0, 180):
        if i + 1 < 180:
            graph_facts.append(f"(edge v{i} v{i+1})")
        if i + 2 < 180:
            graph_facts.append(f"(edge v{i} v{i+2})")
        if i % 4 == 0 and i + 3 < 180:
            graph_facts.append(f"(edge v{i} v{i+3})")
    # Add a separate cyclic graph on c0..c39 for presence-only unique reachability
    for i in range(0, 40):
        graph_facts.append(f"(cedge c{i} c{(i+1)%40})")
        graph_facts.append(f"(cedge c{i} c{(i+3)%40})")
    graph_code = "\n".join(graph_facts) + """
(= (reach-to $x $dst 0) (if (== $x $dst) found (empty)))
(= (reach-to $x $dst $k)
   (if (<= $k 0) (empty)
       (match &self (edge $x $y) (reach-to $y $dst (- $k 1)))))
(= (creach $x) $x)
(= (creach $x) (match &self (cedge $x $y) (creach $y)))
!(collapse (reach-to v0 v24 14))
!(collapse (unique (creach c0)))
"""

    # 8. Join-heavy program: 5-way conjunctive match written in unjoined order
    #    (cousin + role filter) over a 6-ary hierarchy (~400 nodes, 240+ facts),
    #    exercised both as a direct 5-way join and recursively across 3 cousin hops
    join_facts = []
    nxt = 1
    for p in range(0, 65):
        for _ in range(5):
            join_facts.append(f"(parent p{p} p{nxt})")
            nxt += 1
    for i in range(0, nxt):
        role = "scholar" if (i % 3 != 0) else "builder"
        join_facts.append(f"(role p{i} {role})")
    join_code = "\n".join(join_facts) + """
(= (cousin-step $a)
   (match &self (, (parent $x $a) (parent $y $b) (parent $g $x) (parent $g $y) (role $b scholar))
     $b))
(= (cousin-hop $a 0) $a)
(= (cousin-hop $a $k)
   (if (<= $k 0) (empty)
       (let $b (cousin-step $a) (cousin-hop $b (- $k 1)))))
!(collapse (cousin-step p40))
!(collapse (cousin-hop p40 3))
"""

    return [
        ("count", "count-items", count_code, True, {"he": "pp+interp", "petta": "pp+search"}),
        ("nrev-300", "len", nrev_code, False, {"he": "prepared-pure", "petta": "prepared-pure"}),
        ("queens-8", "place", queens_code, True, {"he": "tcalc-slg", "petta": "search-machine"}),
        ("bfib-26", "bfib", bfib_code, False, {"he": "tcalc-slg", "petta": "tcalc-slg"}),
        ("fib-34", "fib", fib_code, False, {"he": "tcalc-slg", "petta": "tcalc-slg"}),
        ("pln-chainer", "pln-chain", pln_code, True, {"he": "tcalc-slg+idx", "petta": "tcalc-slg+idx"}),
        ("metamath-check", "mm-verify-many", mm_code, False, {"he": "tcalc-slg+idx", "petta": "tcalc-slg+idx"}),
        ("graph-reach", "reach-to", graph_code, True, {"he": "tcalc-slg+idx", "petta": "tcalc-slg+idx"}),
        ("join-heavy", "cousin-hop", join_code, True, {"he": "tcalc-join+idx", "petta": "tcalc-join+idx"}),
    ]


def strip_top_queries(code: str) -> str:
    """Remove top-level `!(...)` evaluation forms while keeping all definitions and data."""
    out = []
    i = 0
    n = len(code)
    while i < n:
        ch = code[i]
        if ch == ";":
            j = code.find("\n", i)
            if j < 0:
                break
            out.append(code[i : j + 1])
            i = j + 1
            continue
        if ch == "!" and (i == 0 or code[i - 1] in "\n\r \t"):
            j = i + 1
            while j < n and code[j] in " \t":
                j += 1
            if j < n and code[j] == "(":
                depth = 0
                in_str = False
                esc = False
                while j < n:
                    c = code[j]
                    if in_str:
                        if esc:
                            esc = False
                        elif c == "\\":
                            esc = True
                        elif c == '"':
                            in_str = False
                    else:
                        if c == '"':
                            in_str = True
                        elif c == "(":
                            depth += 1
                        elif c == ")":
                            depth -= 1
                            if depth == 0:
                                j += 1
                                break
                    j += 1
                i = j
                continue
        out.append(ch)
        i += 1
    return "".join(out)


def verify_compiled_program(cetta: str, lang: str, head: str, code: str) -> None:
    check_code = (
        "!(import! &self tcalc)\n"
        + strip_top_queries(code)
        + f"\n!(let $p (tcalc:space-program &self {head}) (car-atom $p))\n"
    )
    _, lines = run_timed(cetta, lang, check_code, {}, trials=1)
    if lang == "petta" and lines and lines[0] == "true":
        lines = lines[1:]
    if not lines or lines[-1] != "(store multiset)":
        raise AssertionError(
            f"{lang} head '{head}' did NOT compile under tcalc:space-program: {lines}"
        )


def run_interleaved(
    cetta: str, lang: str, code: str, trials: int = 5
) -> tuple[tuple[float, list[str]], tuple[float, list[str]], tuple[float, list[str]]]:
    """Run interp, auto, and always interleaved round-robin to cancel transient
    background load skew across the three configurations."""
    modes = [
        {"CETTA_TCALC_EVAL": "0"},
        {"CETTA_TCALC_EVAL": "1"},
        {"CETTA_TCALC_EVAL": "always"},
    ]
    times: list[list[float]] = [[], [], []]
    outs: list[list[str]] = [[], [], []]
    with tempfile.NamedTemporaryFile(
        "w", suffix=".metta", delete=False, encoding="utf-8"
    ) as tf:
        tf.write(code)
        path = tf.name
    try:
        for _ in range(trials):
            for mi, overrides in enumerate(modes):
                env = os.environ.copy()
                env.update(overrides)
                t0 = time.perf_counter()
                proc = subprocess.run(
                    [cetta, "--lang", lang, "--quiet", path],
                    capture_output=True,
                    text=True,
                    timeout=60,
                    env=env,
                )
                dt = (time.perf_counter() - t0) * 1000.0
                if proc.returncode != 0:
                    raise RuntimeError(
                        f"cetta --lang {lang} env={overrides} failed:\n"
                        f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
                    )
                times[mi].append(dt)
                lines = []
                for raw in proc.stdout.splitlines():
                    s = raw.strip()
                    if not s:
                        continue
                    if lang == "he" and s.startswith("[") and s.endswith("]"):
                        s = s[1:-1].strip()
                    lines.append(s)
                outs[mi] = lines
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass
    return (
        (statistics.median(times[0]), outs[0]),
        (statistics.median(times[1]), outs[1]),
        (statistics.median(times[2]), outs[2]),
    )


def main() -> int:
    cetta = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    progs = build_programs(root)

    print(
        f"{'program':<16} {'dialect':<7} {'route':<15} {'interp_ms':>10} {'auto_ms':>10} "
        f"{'always_ms':>10} {'auto_x':>8} {'agree':>6}"
    )
    print("-" * 89)

    for name, head, code, is_bag, routes in progs:
        for lang in ("he", "petta"):
            verify_compiled_program(cetta, lang, head, code)
            (t_interp, out_interp), (t_auto, out_auto), (t_always, out_always) = (
                run_interleaved(cetta, lang, code, trials=7)
            )
            assert len(out_interp) == len(out_auto) == len(out_always), (
                f"{name} {lang} line-count mismatch: interp={len(out_interp)} "
                f"auto={len(out_auto)} always={len(out_always)}"
            )
            for li in range(len(out_interp)):
                if is_bag and out_interp[li].startswith("("):
                    ci = collections.Counter(parse_sexp_items(out_interp[li]))
                    ca = collections.Counter(parse_sexp_items(out_auto[li]))
                    cw = collections.Counter(parse_sexp_items(out_always[li]))
                    assert ci == ca == cw, (
                        f"{name} {lang} bag mismatch at line {li}:\n"
                        f"  interp={out_interp[li][:120]}\n"
                        f"  auto={out_auto[li][:120]}\n"
                        f"  always={out_always[li][:120]}"
                    )
                else:
                    assert out_interp[li] == out_auto[li] == out_always[li], (
                        f"{name} {lang} mismatch at line {li}: "
                        f"interp={out_interp[li]!r} auto={out_auto[li]!r} always={out_always[li]!r}"
                    )
            sp = t_interp / t_auto if t_auto > 0 else 0.0
            print(
                f"{name:<16} {lang:<7} {routes[lang]:<15} {t_interp:>10.2f} {t_auto:>10.2f} "
                f"{t_always:>10.2f} {sp:>7.2f}x {'OK':>6}"
            )
    print(f"PASS: check_step2_compiled_route_perf (all {len(progs)} benchmarks x 2 dialects exact)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
