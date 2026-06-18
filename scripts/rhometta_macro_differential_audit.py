#!/usr/bin/env python3
"""Differential soundness audit for the rhometta quiet-frontier macro step.

The general principle (applies to ANY observable-preserving optimization):
run a generated corpus through BOTH the optimized strategy and the un-optimized
REFERENCE oracle on the same binary, and assert the observables coincide.  Here
the observable is the may-set of quiescent residuals of `rhometta:run`, the
optimization is the quiet-frontier macro step, and the reference is the exact
interleaving exploration (forced by CETTA_RHO_NO_MACRO=1).

A divergence on any program is a soundness violation of the optimization on that
program -- found without knowing in advance *which* interaction breaks it.  The
corpus is ADVERSARIAL on purpose: it does not just replay the first bug class.
It saturates the mutable-resource surface (shared spaces, state cells, identity
allocation) AND hides each effect behind a user-defined equation, because a
syntactic safety check that only looks at the payload head is defeated exactly
there.  It also pins NAMED fixtures for the scary classes so they are always
checked regardless of the random seed.  Failures are shrunk to a minimal case.

Usage: rhometta_macro_differential_audit.py [--n N] [--seed S] [--cetta PATH]
       [--timeout SECONDS]
Exit 0 = no divergence (optimization sound on the corpus); 1 = divergence found.
"""
import argparse, os, random, re, subprocess, sys, tempfile
from typing import NamedTuple

# Payload palette.  `pure` reads only &self/local; the rest touch shared mutable
# resources (space &g, state cell &c) or allocate identity -- exactly what a
# sibling-isolating transactional evaluator must neutralize for the macro to
# stay sound.
PURE = [
    "(+ 1 2)",
    "(superpose (1 2))",
    "(match &self (k $v) $v)",
    "(let $s (new-state 0) (let $_ (change-state! $s 1) (get-state $s)))",
    "(let $s (match &self (holder-space-ground $x) $x) (let $_ (add-atom $s injected) (get-atoms $s)))",
    "(let $s (match &self (holder-state-ground $x) $x) (let $_ (change-state! $s 1) (get-state $s)))",
]
EFFECT = [
    "(match &g (foo) found)",                # shared-space read
    "(remove-atom &g (foo))",                # shared-space write
    "(add-atom &g (bar))",                   # shared-space write
    "(new-space)",                           # identity allocation
    "(new-state 7)",                         # identity allocation
    "(change-state! &c 1)",                  # shared state-cell write
    "(get-state &c)",                        # shared state-cell read
    "(superpose ((match &g (foo) hit) (remove-atom &g (foo))))",
]
PALETTE = PURE + EFFECT

PREAMBLE = """!(import! &self rhometta)
!(bind! &g (new-space))
!(add-atom &g (foo))
!(bind! &c (new-state 0))
!(add-atom &self (k v1))
!(add-atom &self (k v2))
!(let $sub (new-space)
   (let $_ (add-atom $sub base)
     (add-atom &self (holder-space-ground $sub))))
!(let $st (new-state 0)
   (add-atom &self (holder-state-ground $st)))
(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))
"""

class AuditCase(NamedTuple):
    prelude: str
    expr: str


def maybe_wrap(wrappers, name, expr, wrap):
    if not wrap:
        return expr
    wrappers.append(f"(= ({name}) {expr})")
    return f"({name})"


def quiet_farm_case(specs):
    """specs: list of (payload_str, wrap_bool).  A wrapped payload is hidden
    behind a fresh user equation `(= (wK) payload)` and the cell calls `(wK)` --
    this defeats any head-only syntactic safety check, so the snapshot must hold
    structurally.  One quiet (rho:val $y) cell per spec, on distinct channels, so
    C1 (no contention) and C2 (quiet) hold and the macro is eligible."""
    wrappers, cells = [], []
    for i, (pay, wrap) in enumerate(specs):
        call = maybe_wrap(wrappers, f"w{i}", pay, wrap)
        cells.append(
            f"  (rho:par (rho:send (chan {i}) (rhometta:eval {call}))"
            f" (rho:recv (chan {i}) $y{i} (rho:val $y{i})))")
    return AuditCase(
        PREAMBLE + "\n".join(wrappers) + ("\n" if wrappers else ""),
        "(rho:par\n" + "\n".join(cells) + ")",
    )


def mixed_contention_case(quiet_specs, send_payload, wrap_send):
    wrappers, cells = [], []
    for i, (pay, wrap) in enumerate(quiet_specs):
        call = maybe_wrap(wrappers, f"mq{i}", pay, wrap)
        cells.append(
            f"  (rho:par (rho:send (chan {i}) (rhometta:eval {call}))"
            f" (rho:recv (chan {i}) $y{i} (rho:val keep-{i})))")
    send_call = maybe_wrap(wrappers, "mixed-contention-send", send_payload, wrap_send)
    cells.extend(
        [
            f"  (rho:send (chan {len(quiet_specs)}) (rhometta:eval {send_call}))",
            f"  (rho:recv (chan {len(quiet_specs)}) $x (rho:val left))",
            f"  (rho:recv (chan {len(quiet_specs)}) $x (rho:val right))",
        ]
    )
    return AuditCase(
        PREAMBLE + "\n".join(wrappers) + ("\n" if wrappers else ""),
        "(rho:par\n" + "\n".join(cells) + ")",
    )


def mixed_unsafe_case(quiet_specs, unsafe_payload, wrap_unsafe):
    wrappers, cells = [], []
    for i, (pay, wrap) in enumerate(quiet_specs):
        call = maybe_wrap(wrappers, f"muq{i}", pay, wrap)
        cells.append(
            f"  (rho:par (rho:send (chan {i}) (rhometta:eval {call}))"
            f" (rho:recv (chan {i}) $y{i} (rho:val keep-{i})))")
    unsafe_call = maybe_wrap(wrappers, "mixed-unsafe-send", unsafe_payload, wrap_unsafe)
    cells.append(
        f"  (rho:par (rho:send (chan {len(quiet_specs)}) (rhometta:eval {unsafe_call}))"
        f" (rho:recv (chan {len(quiet_specs)}) $z (rho:val $z)))"
    )
    return AuditCase(
        PREAMBLE + "\n".join(wrappers) + ("\n" if wrappers else ""),
        "(rho:par\n" + "\n".join(cells) + ")",
    )


def superpose_range(start, count):
    items = " ".join(str(start + i) for i in range(count))
    return f"(superpose ({items}))"


def mixed_child_cap_case():
    return AuditCase(
        PREAMBLE,
        f"""(rho:par
  (rho:send (chan 0) (rhometta:eval {superpose_range(0, 257)}))
  (rho:recv (chan 0) $x (rho:val $x))
  (rho:send (chan 1) (rhometta:eval {superpose_range(1000, 257)}))
  (rho:recv (chan 1) $y (rho:val $y)))""",
    )


def contention_case(send_payload, wrap_send):
    wrappers = []
    send_call = maybe_wrap(wrappers, "contention-send", send_payload, wrap_send)
    expr = f"""(rho:par
  (rho:send (chan 0) (rhometta:eval {send_call}))
  (rho:recv (chan 0) $x (rho:val left))
  (rho:recv (chan 0) $x (rho:val right)))"""
    return AuditCase(
        PREAMBLE + "\n".join(wrappers) + ("\n" if wrappers else ""),
        expr,
    )


def chaining_case(seed_payload, wrap_seed, side_payload, wrap_side):
    wrappers = []
    seed_call = maybe_wrap(wrappers, "chain-seed", seed_payload, wrap_seed)
    side_call = maybe_wrap(wrappers, "chain-side", side_payload, wrap_side)
    expr = f"""(rho:par
  (rho:send (chan 0) (rhometta:eval {seed_call}))
  (rho:recv (chan 0) $x (rho:send (chan 1) rho:nil))
  (rho:recv (chan 1) $y (rho:val chained))
  (rho:send (chan 2) (rhometta:eval {side_call}))
  (rho:recv (chan 2) $z (rho:val side)))"""
    return AuditCase(
        PREAMBLE + "\n".join(wrappers) + ("\n" if wrappers else ""),
        expr,
    )


def render_run_source(case):
    return case.prelude + f"!(collapse (rhometta:run {case.expr}))\n"


def render_stats_source(case):
    stats_queries = "\n".join(
        f"!(match &stats (runtime-counter {name} $n) (counter {name} $n))"
        for name in (
            "rho-quiet-macro-applied",
            "rho-quiet-macro-full-fire",
            "rho-quiet-macro-partial-fire",
            "rho-quiet-macro-bail-exact",
            "rho-quiet-macro-fallback-contention",
            "rho-quiet-macro-fallback-nonquiet",
            "rho-quiet-macro-fallback-unsafe-payload",
            "rho-quiet-macro-fallback-child-cap",
        )
    )
    return (
        case.prelude
        + "!(reset-runtime-stats!)\n"
        + f"!(collapse (rhometta:run {case.expr}))\n"
        + "!(bind! &stats (runtime-stats!))\n"
        + stats_queries
        + "\n"
    )

# Named fixtures for the dangerous classes -- always checked, seed-independent.
# Each effect appears both directly and hidden behind an equation (wrap=True).
def fixture_case(name):
    specs = {
        "direct-foreign-rw":  [("(match &g (foo) found)", False), ("(remove-atom &g (foo))", False)],
        "wrapped-foreign-rw": [("(match &g (foo) found)", True),  ("(remove-atom &g (foo))", True)],
        "wrapped-foreign-add":[("(match &g (foo) found)", True),  ("(add-atom &g (bar))", True)],
        "direct-new-space":   [("(new-space)", False), ("(+ 1 2)", False)],
        "wrapped-new-space":  [("(new-space)", True),  ("(superpose (1 2))", False)],
        "direct-state-rw":    [("(change-state! &c 1)", False), ("(get-state &c)", False)],
        "wrapped-state-rw":   [("(change-state! &c 1)", True),  ("(get-state &c)", True)],
        "wrapped-new-state":  [("(new-state 9)", True), ("(match &self (k $v) $v)", False)],
        "local-state-scratch":[("(let $s (new-state 0) (let $_ (change-state! $s 1) (get-state $s)))", False),
                               ("(+ 1 2)", False)],
        "ground-space-direct":[("(let $s (match &self (holder-space-ground $x) $x) (let $_ (add-atom $s injected) (get-atoms $s)))", False),
                               ("(+ 1 2)", False)],
        "ground-space-wrapped":[("(let $s (match &self (holder-space-ground $x) $x) (let $_ (add-atom $s injected) (get-atoms $s)))", True),
                                ("(superpose (1 2))", False)],
        "ground-state-direct":[("(let $s (match &self (holder-state-ground $x) $x) (let $_ (change-state! $s 1) (get-state $s)))", False),
                               ("(+ 1 2)", False)],
        "ground-state-wrapped":[("(let $s (match &self (holder-state-ground $x) $x) (let $_ (change-state! $s 1) (get-state $s)))", True),
                                ("(match &self (k $v) $v)", False)],
        "ground-space-owned":[("(let $s (match &self (holder-space-ground $x) $x) (let $_ (add-atom $s injected) $s))", False),
                              ("(+ 1 2)", False)],
        "ground-state-owned":[("(let $s (match &self (holder-state-ground $x) $x) (let $_ (change-state! $s 1) $s))", False),
                              ("(match &self (k $v) $v)", False)],
        "mixed-hidden":       [("(superpose ((match &g (foo) hit) (remove-atom &g (foo))))", True),
                               ("(match &self (k $v) $v)", False)],
        "triple-hidden":      [("(remove-atom &g (foo))", True), ("(change-state! &c 5)", True),
                               ("(match &g (foo) found)", True)],
    }
    if name in specs:
        return quiet_farm_case(specs[name])
    if name == "contention-direct":
        return contention_case("(+ 1 2)", False)
    if name == "contention-wrapped":
        return contention_case("(superpose (1 2))", True)
    if name == "chaining-direct":
        return chaining_case("(+ 1 2)", False, "(match &self (k $v) $v)", False)
    if name == "chaining-wrapped":
        return chaining_case("(superpose (1 2))", True, "(match &g (foo) found)", True)
    if name == "partial-contention-direct":
        return mixed_contention_case(
            [("(+ 1 2)", False), ("(match &self (k $v) $v)", False)],
            "(+ 1 2)",
            False,
        )
    if name == "partial-contention-wrapped":
        return mixed_contention_case(
            [("(superpose (1 2))", True), ("(+ 2 3)", False)],
            "(superpose (1 2))",
            True,
        )
    if name == "mixed-unsafe-direct":
        return mixed_unsafe_case(
            [("(+ 1 2)", False), ("(match &self (k $v) $v)", False)],
            "(new-space)",
            False,
        )
    if name == "mixed-unsafe-wrapped":
        return mixed_unsafe_case(
            [("(superpose (1 2))", False), ("(+ 2 3)", False)],
            "(new-state 7)",
            True,
        )
    if name == "mixed-child-cap":
        return mixed_child_cap_case()
    raise KeyError(name)


NAMED_FIXTURES = [
    "direct-foreign-rw",
    "wrapped-foreign-rw",
    "wrapped-foreign-add",
    "direct-new-space",
    "wrapped-new-space",
    "direct-state-rw",
    "wrapped-state-rw",
    "wrapped-new-state",
    "local-state-scratch",
    "ground-space-direct",
    "ground-space-wrapped",
    "ground-state-direct",
    "ground-state-wrapped",
    "ground-space-owned",
    "ground-state-owned",
    "mixed-hidden",
    "triple-hidden",
    "contention-direct",
    "contention-wrapped",
    "chaining-direct",
    "chaining-wrapped",
    "partial-contention-direct",
    "partial-contention-wrapped",
    "mixed-unsafe-direct",
    "mixed-unsafe-wrapped",
    "mixed-child-cap",
]

def run_text(cetta, src, macro_on, timeout):
    env = dict(os.environ)
    if not macro_on:
        env["CETTA_RHO_NO_MACRO"] = "1"
    env.setdefault("ASAN_OPTIONS", "")
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(src); path = f.name
    try:
        proc = subprocess.run([cetta, "--quiet", "--profile", "he-extended",
                               "--lang", "he", path], capture_output=True,
                              text=True, env=env, timeout=timeout)
    finally:
        os.unlink(path)
    return proc


_RUNTIME_STATS_AVAILABLE = None


def runtime_stats_available(cetta, timeout):
    global _RUNTIME_STATS_AVAILABLE
    if _RUNTIME_STATS_AVAILABLE is not None:
        return _RUNTIME_STATS_AVAILABLE
    proc = run_text(
        cetta,
        "!(reset-runtime-stats!)\n"
        "!(bind! &stats (runtime-stats!))\n"
        "!(match &stats (runtime-counter rho-quiet-macro-applied $n)"
        " (counter rho-quiet-macro-applied $n))\n",
        True,
        timeout,
    )
    _RUNTIME_STATS_AVAILABLE = (
        proc.returncode == 0 and "counter rho-quiet-macro-applied" in proc.stdout
    )
    return _RUNTIME_STATS_AVAILABLE


def runtime_counter(out, name):
    match = re.search(rf"\(counter {re.escape(name)} ([0-9]+)\)", out)
    if not match:
        return None
    return int(match.group(1))

def split_top(s):
    """Split a paren-tuple body into top-level residual strings (the may-set)."""
    res, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1; cur += ch
        elif ch == ")":
            depth -= 1; cur += ch
            if depth == 0 and cur.strip():
                res.append(cur.strip()); cur = ""
        elif depth == 0 and ch.isspace():
            if cur.strip():
                res.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        res.append(cur.strip())
    return res

def mayset(out):
    """Normalize the last `[(...)]` line to a sorted multiset of residuals."""
    line = ""
    for ln in out.splitlines():
        ln = ln.strip()
        if ln.startswith("[") and "rho:" in ln:
            line = ln
    if not line:
        return None
    line = re.sub(r"0x[0-9a-fA-F]+", "0xPTR", line)
    line = re.sub(r"\$[A-Za-z0-9_#]+", "$V", line)
    inner = line[line.find("[") + 1 : line.rfind("]")].strip()
    if inner.startswith("(") and inner.endswith(")"):
        inner = inner[1:-1]
    return sorted(split_top(inner))

def diverges(cetta, specs, timeout):
    case = quiet_farm_case(specs)
    a = mayset(run_text(cetta, render_run_source(case), True, timeout).stdout)
    b = mayset(run_text(cetta, render_run_source(case), False, timeout).stdout)
    return (a != b), a, b


def diverges_case(cetta, case, timeout):
    a = mayset(run_text(cetta, render_run_source(case), True, timeout).stdout)
    b = mayset(run_text(cetta, render_run_source(case), False, timeout).stdout)
    return (a != b), a, b


def stats_expectation(name):
    if name.startswith("contention-"):
        return {
            "rho-quiet-macro-applied": 0,
            "rho-quiet-macro-partial-fire": 0,
            "rho-quiet-macro-bail-exact": 1,
            "rho-quiet-macro-fallback-contention": 1,
        }
    if name.startswith("chaining-"):
        return {
            "rho-quiet-macro-partial-fire": 1,
            "rho-quiet-macro-fallback-nonquiet": 1,
        }
    if name.startswith("partial-contention-"):
        return {
            "rho-quiet-macro-partial-fire": 1,
            "rho-quiet-macro-fallback-contention": 1,
        }
    if name in ("direct-new-space", "wrapped-new-space", "wrapped-new-state",
                "mixed-unsafe-direct", "mixed-unsafe-wrapped"):
        return {
            "rho-quiet-macro-partial-fire": 0,
            "rho-quiet-macro-bail-exact": 1,
            "rho-quiet-macro-fallback-unsafe-payload": 1,
        }
    if name == "mixed-child-cap":
        return {
            "rho-quiet-macro-partial-fire": 1,
            "rho-quiet-macro-fallback-child-cap": 1,
        }
    return None


def stats_match(cetta, name, case, timeout):
    expected = stats_expectation(name)
    if expected is None or not runtime_stats_available(cetta, timeout):
        return True, None
    proc = run_text(cetta, render_stats_source(case), True, timeout)
    if proc.returncode != 0:
        return False, f"runtime-stats run failed: {proc.stderr.strip() or proc.stdout.strip()}"
    for counter_name, minimum in expected.items():
        value = runtime_counter(proc.stdout, counter_name)
        if value is None:
            return False, f"missing runtime counter {counter_name}"
        if minimum == 0 and value != 0:
            return False, f"expected {counter_name}=0, saw {value}"
        if minimum > 0 and value < minimum:
            return False, f"expected {counter_name}>={minimum}, saw {value}"
    return True, None

def shrink(cetta, specs, timeout):
    """Greedily drop cells while the divergence persists."""
    changed = True
    while changed and len(specs) > 2:
        changed = False
        for i in range(len(specs)):
            cand = specs[:i] + specs[i+1:]
            if len(cand) >= 2 and diverges(cetta, cand, timeout)[0]:
                specs = cand; changed = True; break
    return specs

def report(tag, specs, sa, sb):
    print(f"[DIVERGENCE] {tag}: specs={specs}")
    print(f"  macro-on  may-set ({len(sa)}): {sa}")
    print(f"  exact-ref may-set ({len(sb)}): {sb}\n")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--cetta", default=os.path.join(
        os.path.dirname(__file__), "..", "cetta"))
    ap.add_argument("--timeout", type=float, default=60.0)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    cetta = os.path.abspath(a.cetta)
    found = 0

    # 1. Named fixtures -- the scary classes, always checked.
    for name in NAMED_FIXTURES:
        case = fixture_case(name)
        div, ma, mb = diverges_case(cetta, case, a.timeout)
        if div:
            found += 1
            report(f"fixture {name}", case.expr, ma, mb)
        stats_ok, stats_msg = stats_match(cetta, name, case, a.timeout)
        if not stats_ok:
            found += 1
            print(f"[DIVERGENCE] fixture {name}: runtime-stats expectation failed")
            print(f"  {stats_msg}\n")

    # 2. Random corpus -- quiet independent frontiers plus mixed families that
    #    should now exhibit partial fire rather than whole-frontier bail.
    for t in range(a.n):
        family = rng.choices(
            ("quiet", "contention", "chaining", "mixed-contention", "mixed-unsafe"),
            weights=(0.55, 0.1, 0.15, 0.1, 0.1),
            k=1,
        )[0]
        if family == "quiet":
            k = rng.randint(2, 4)
            specs = [(rng.choice(PALETTE), rng.random() < 0.4) for _ in range(k)]
            case = quiet_farm_case(specs)
            shrunk_specs = specs
        elif family == "contention":
            specs = [(rng.choice(PALETTE), rng.random() < 0.5)]
            case = contention_case(specs[0][0], specs[0][1])
            shrunk_specs = specs
        elif family == "chaining":
            specs = [
                (rng.choice(PALETTE), rng.random() < 0.5),
                (rng.choice(PALETTE), rng.random() < 0.5),
            ]
            case = chaining_case(specs[0][0], specs[0][1], specs[1][0], specs[1][1])
            shrunk_specs = specs
        elif family == "mixed-contention":
            specs = [
                (rng.choice(PURE), rng.random() < 0.4),
                (rng.choice(PURE), rng.random() < 0.4),
                (rng.choice(PALETTE), rng.random() < 0.5),
            ]
            case = mixed_contention_case(specs[:2], specs[2][0], specs[2][1])
            shrunk_specs = specs
        else:
            specs = [
                (rng.choice(PURE), rng.random() < 0.4),
                (rng.choice(PURE), rng.random() < 0.4),
                (rng.choice(EFFECT), rng.random() < 0.5),
            ]
            case = mixed_unsafe_case(specs[:2], specs[2][0], specs[2][1])
            shrunk_specs = specs
        div, ma, mb = diverges_case(cetta, case, a.timeout)
        if div:
            found += 1
            if found <= 3 and family == "quiet":
                shrunk_specs = shrink(cetta, specs, a.timeout)
                shrunk_case = quiet_farm_case(shrunk_specs)
                _, sa, sb = diverges_case(cetta, shrunk_case, a.timeout)
                report(f"random trial {t} ({family})", shrunk_specs, sa, sb)
            elif found <= 3:
                report(f"random trial {t} ({family})", case.expr, ma, mb)

    total = a.n + len(NAMED_FIXTURES)
    print(f"=== {found}/{total} programs diverged (macro-set != exact-set); "
          f"{len(NAMED_FIXTURES)} named fixtures + {a.n} random ===")
    return 1 if found else 0

if __name__ == "__main__":
    sys.exit(main())
