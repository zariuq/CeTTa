#!/usr/bin/env python3
"""Cross-oracle conformance checker for lib_random_closure.metta (--lang petta).

Compares CeTTa's pure-MeTTa random-closure engine against both:
  1. closure_response.py's Prepared / Engine rational evaluator, and
  2. closure_response.py's enumerate_probability world-enumeration oracle.

Verifies exact rational equality on rc:query-exact, strict IEEE-754 interval
enclosure and accuracy tolerances on rc:query, family-count agreement,
impossible-evidence detection, rare-query BigInt fallback, and incremental
local component scenario replacement.
"""

from __future__ import annotations

import argparse
from decimal import Decimal
from fractions import Fraction
import importlib.util
import os
from pathlib import Path
import random
import re
import subprocess
import sys
from typing import Any, Sequence


def load_reference_module(explicit_path: str | None):
    candidates: list[Path] = []
    if explicit_path:
        candidates.append(Path(explicit_path))
    env_path = os.environ.get("CLOSURE_RESPONSE_PY")
    if env_path:
        candidates.append(Path(env_path))
    repo_root = Path(__file__).resolve().parents[2]
    for ancestor in repo_root.parents:
        candidate = (
            ancestor
            / "Mettapedia"
            / "lean"
            / "mettapedia"
            / "scripts"
            / "benchmarks"
            / "closure_response.py"
        )
        if candidate.is_file():
            candidates.append(candidate)
            break
    for path in candidates:
        if path.is_file():
            spec = importlib.util.spec_from_file_location("closure_response", path)
            if spec and spec.loader:
                module = importlib.util.module_from_spec(spec)
                sys.modules["closure_response"] = module
                spec.loader.exec_module(module)
                return module
    raise FileNotFoundError(
        "Could not locate closure_response.py; pass --reference-py or set CLOSURE_RESPONSE_PY."
    )


def metta_tuple(items: Sequence[str]) -> str:
    return "(" + " ".join(items) + ")"


def metta_rat(f: Fraction) -> str:
    return f"(Rat {f.numerator} {f.denominator})"


def metta_ev(e: bool | None) -> str:
    if e is None:
        return "none"
    return "true" if e else "false"


def metta_local_state(state) -> str:
    probs = metta_tuple([metta_rat(p) for p in state.probabilities])
    evs = metta_tuple([metta_ev(e) for e in state.evidence])
    return f"(RC:LocalState {probs} {evs})"


def metta_rule(rule) -> str:
    return f"(RC:Rule {rule.head} {metta_tuple(rule.body)})"


def metta_component(comp) -> str:
    return (
        f"(RC:Component {comp.name} "
        f"{metta_tuple(comp.choices)} "
        f"{metta_tuple(comp.private)} "
        f"{metta_tuple([metta_rule(r) for r in comp.rules])})"
    )


def metta_program(prog) -> str:
    return (
        f"(RC:Program {metta_tuple(prog.boundary)} "
        f"{metta_tuple(prog.shared)} "
        f"{metta_tuple([metta_component(c) for c in prog.components])})"
    )


def seed_atoms_from_mask(boundary: Sequence[str], mask: int) -> tuple[str, ...]:
    return tuple(atom for i, atom in enumerate(boundary) if (mask >> i) & 1)


def metta_query(boundary: Sequence[str], seed_mask: int, target_idx: int) -> str:
    seed_tuple = metta_tuple(seed_atoms_from_mask(boundary, seed_mask))
    return f"(RC:Query {seed_tuple} {boundary[target_idx]})"


TOKEN_RE = re.compile(r"\(|\)|[^\s()]+")


def parse_sexpr(text: str) -> Any:
    tokens = TOKEN_RE.findall(text)
    pos = 0

    def read_one() -> Any:
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        if tok == "(":
            out = []
            while tokens[pos] != ")":
                out.append(read_one())
            pos += 1
            return out
        return tok

    results = []
    while pos < len(tokens):
        results.append(read_one())
    return results


def parse_exact_answer(sexpr: Any) -> Fraction | None:
    if sexpr == "RC:ImpossibleEvidence":
        return None
    if (
        isinstance(sexpr, list)
        and len(sexpr) == 2
        and sexpr[0] == "RC:ExactAnswer"
        and isinstance(sexpr[1], list)
        and len(sexpr[1]) == 3
        and sexpr[1][0] == "Rat"
    ):
        return Fraction(int(sexpr[1][1]), int(sexpr[1][2]))
    raise AssertionError(f"Unexpected exact answer sexpr: {sexpr!r}")


def verify_interval_answer(sexpr: Any, expected: Fraction | None, label: str) -> None:
    if expected is None:
        if sexpr != "RC:ImpossibleEvidence":
            raise AssertionError(f"{label}: expected ImpossibleEvidence, got {sexpr!r}")
        return
    if not (isinstance(sexpr, list) and len(sexpr) == 4 and sexpr[0] == "RC:Answer"):
        raise AssertionError(f"{label}: malformed RC:Answer: {sexpr!r}")
    interval_expr, exact_opt, fb_str = sexpr[1], sexpr[2], sexpr[3]
    if not (isinstance(interval_expr, list) and len(interval_expr) == 3 and interval_expr[0] == "RC:Interval"):
        raise AssertionError(f"{label}: malformed RC:Interval: {interval_expr!r}")
    lo_str, hi_str = interval_expr[1], interval_expr[2]
    lo_f = float(lo_str)
    hi_f = float(hi_str)
    used_fb = fb_str == "true"
    if lo_f > hi_f or lo_f < 0.0 or hi_f > 1.0:
        raise AssertionError(f"{label}: invalid bounds [{lo_f}, {hi_f}]")
    lo_q = Fraction(lo_f)
    hi_q = Fraction(hi_f)
    # Zero-slack enclosure check in BOTH exact IEEE-754 binary64 and printed decimal!
    if not (lo_q <= expected <= hi_q):
        raise AssertionError(
            f"{label}: binary64 interval [{lo_f!r}, {hi_f!r}] does not enclose exact ~{float(expected):.17g}"
        )
    lo_dec_q = Fraction(Decimal(lo_str))
    hi_dec_q = Fraction(Decimal(hi_str))
    if not (lo_dec_q <= expected <= hi_dec_q):
        raise AssertionError(
            f"{label}: printed decimal interval [{lo_str}, {hi_str}] does not enclose exact ~{float(expected):.17g}"
        )
    width = hi_f - lo_f
    if not used_fb:
        if width > 1.0e-12 or lo_f <= 0.0 or width > 1.0e-10 * lo_f:
            raise AssertionError(
                f"{label}: non-fallback interval [{lo_f}, {hi_f}] violated accuracy gate (width={width})"
            )
    else:
        if not (isinstance(exact_opt, list) and len(exact_opt) == 3 and exact_opt[0] == "Rat"):
            raise AssertionError(f"{label}: fallback answer missing exact Rat: {sexpr!r}")
        fb_rat = Fraction(int(exact_opt[1]), int(exact_opt[2]))
        if fb_rat != expected:
            raise AssertionError(f"{label}: fallback Rat {fb_rat} != expected {expected}")


def build_cases(cr):
    Rule, Component, Program, LocalState = cr.Rule, cr.Component, cr.Program, cr.LocalState
    cases = []

    # Case 1: 3-component cyclic service dependency with 2 shared causes & private symptoms
    prog1 = Program(
        boundary=("req", "auth_ok", "cache_hit", "resp_ok"),
        shared=("dns_ok", "tls_ok"),
        components=(
            Component(
                "gateway",
                ("gw_up",),
                ("gw_ready",),
                (
                    Rule("gw_ready", ("gw_up", "tls_ok")),
                    Rule("auth_ok", ("req", "gw_ready", "dns_ok")),
                    Rule("resp_ok", ("cache_hit", "gw_ready")),
                ),
            ),
            Component(
                "cache_tier",
                ("cache_up", "fast_path"),
                (),
                (
                    Rule("cache_hit", ("auth_ok", "cache_up")),
                    Rule("cache_hit", ("req", "fast_path", "dns_ok")),
                ),
            ),
            Component(
                "fallback_worker",
                ("worker_up",),
                (),
                (
                    Rule("cache_hit", ("auth_ok", "worker_up", "tls_ok")),
                    Rule("auth_ok", ("cache_hit", "worker_up")),
                ),
            ),
        ),
    )
    queries1 = [(1, 3), (1, 2), (0, 3), (3, 3)]
    init_states1 = [
        LocalState.make([Fraction(4, 5)]),
        LocalState.make([Fraction(2, 3), Fraction(1, 4)]),
        LocalState.make([Fraction(3, 5)]),
    ]
    init_shared1 = LocalState.make([Fraction(5, 6), Fraction(7, 8)])
    steps1 = [
        ("update", {1: LocalState.make([Fraction(2, 3), Fraction(1, 4)], [False, None])}, None),
        ("update", {0: LocalState.make([Fraction(9, 10)], [True])}, LocalState.make([Fraction(1, 2), Fraction(7, 8)])),
        # Contradictory impossible evidence via Z=0? LocalState doesn't express Z=0 directly unless prob=0 & ev=True!
        ("update", {2: LocalState.make([Fraction(0, 1)], [True])}, None),
        # Recover from Z=0 back to positive weight
        ("update", {2: LocalState.make([Fraction(3, 4)], [None])}, None),
        # Scenario change: replace cache_tier with a degraded bypass component
        (
            "replace",
            1,
            Component(
                "cache_tier_degraded",
                (" emergency_up".strip(),),
                ("priv_diag",),
                (
                    Rule("priv_diag", ("dns_ok", "emergency_up")),
                    Rule("cache_hit", ("req", "priv_diag")),
                ),
            ),
            LocalState.make([Fraction(1, 2)]),
        ),
    ]
    cases.append(("cyclic_service_3comp", True, prog1, queries1, init_states1, init_shared1, steps1))

    # Case 2: Rare 10^-100 cancellation/underflow control
    p50 = Fraction(1, 10**50)
    prog2 = Program(
        boundary=("a", "b", "c"),
        shared=(),
        components=(
            Component("r1", ("u1",), (), (Rule("b", ("a", "u1")),)),
            Component("r2", ("u2",), (), (Rule("c", ("b", "u2")),)),
        ),
    )
    cases.append(
        (
            "rare_1e_minus_100",
            True,
            prog2,
            [(1, 2), (0, 2)],
            [LocalState.make([p50]), LocalState.make([p50])],
            LocalState.make([]),
            [("update", {0: LocalState.make([Fraction(1, 3)])}, None)],
        )
    )

    # Case 3: Deterministic pseudo-random 4-component cyclic programs with updates & replacements
    rng = random.Random(20261101)
    boundary = ("b0", "b1", "b2", "b3")
    shared = ("sh0",)
    for trial in range(4):
        comps = []
        states = []
        for idx in range(4):
            n_ch = rng.choice([1, 2])
            choices = tuple(f"c{idx}_{j}" for j in range(n_ch))
            priv = (f"p{idx}_0",)
            scope_in = boundary + shared + choices + priv
            derive_pool = boundary + priv
            rules = []
            for _ in range(rng.randint(2, 4)):
                head = rng.choice(derive_pool)
                body_len = rng.randint(1, 3)
                body = tuple(rng.sample(scope_in, body_len))
                rules.append(Rule(head, body))
            comps.append(Component(f"rand_c{idx}", choices, priv, tuple(rules)))
            probs = [Fraction(rng.randint(0, 7), 8) for _ in range(n_ch)]
            states.append(LocalState.make(probs))
        prog_r = Program(boundary=boundary, shared=shared, components=tuple(comps))
        queries_r = [(1, 2), (2, 3), (0, 1), (5, 3)]
        shared_st = LocalState.make([Fraction(rng.randint(1, 5), 6)])
        upd_idx = rng.randrange(4)
        upd_n = len(comps[upd_idx].choices)
        upd_ev = [rng.choice([None, True, False]) for _ in range(upd_n)]
        upd_st = LocalState.make([Fraction(rng.randint(1, 5), 6) for _ in range(upd_n)], upd_ev)
        rep_idx = (upd_idx + 1) % 4
        rep_comp = Component(
            f"rand_c{rep_idx}_alt",
            (f"alt_ch_{trial}_{rep_idx}",),
            (),
            (
                Rule("b2", ("b0", f"alt_ch_{trial}_{rep_idx}")),
                Rule("b3", ("b2", "sh0")),
            ),
        )
        rep_st = LocalState.make([Fraction(2, 5)])
        cases.append(
            (
                f"random_cyclic_trial_{trial}",
                True,
                prog_r,
                queries_r,
                states,
                shared_st,
                [
                    ("update", {upd_idx: upd_st}, None),
                    ("replace", rep_idx, rep_comp, rep_st),
                ],
            )
        )

    # Case 4: High-component-count independent disjunction 1 - (9999/10000)^N
    # across N = 64, 256, 1024 with incremental single-component updates
    for n_or in (64, 256, 1024):
        or_comps = tuple(
            Component(f"c{i}", (f"x{i}",), (), (Rule("q", (f"x{i}",)),))
            for i in range(n_or)
        )
        or_prog = Program(boundary=("q",), shared=(), components=or_comps)
        or_states = [LocalState.make([Fraction(1, 10000)]) for _ in range(n_or)]
        cases.append(
            (
                f"adversarial_or_1e_minus_4_N{n_or}",
                False,
                or_prog,
                [(0, 0)],
                or_states,
                LocalState.make([]),
                [
                    ("update", {n_or // 3: LocalState.make([Fraction(1, 2000)])}, None),
                ],
            )
        )

    # Case 5: Nonsaturating 64-component cyclic shared-cause service family
    n_svc = 64
    svc_comps = tuple(
        Component(
            f"rep_{i}",
            (f"ov_{i}", f"db_{i}"),
            (),
            (
                Rule("auth_deg", ("traffic", f"ov_{i}")),
                Rule("storm", ("traffic", "auth_deg", f"ov_{i}", "dns_flap")),
                Rule("outage", ("storm",)),
                Rule("outage", ("traffic", "auth_deg", f"db_{i}")),
            ),
        )
        for i in range(n_svc)
    )
    svc_prog = Program(
        boundary=("traffic", "auth_deg", "storm", "outage"),
        shared=("dns_flap",),
        components=svc_comps,
    )
    svc_states = [
        LocalState.make([Fraction(1, 2 * n_svc), Fraction(1, 3 * n_svc)])
        for _ in range(n_svc)
    ]
    cases.append(
        (
            "nonsaturating_cyclic_service_N64",
            False,
            svc_prog,
            [(1, 3), (1, 2)],
            svc_states,
            LocalState.make([Fraction(1, 5)]),
            [
                ("update", {5: LocalState.make([Fraction(1, n_svc), Fraction(1, 3 * n_svc)])}, None),
            ],
        )
    )

    return cases


def run_case(cetta_bin: Path, cr, case_tuple) -> tuple[int, int]:
    name, check_enum, prog, queries, init_states, init_shared, steps = case_tuple

    # Build Python reference snapshots across initial + each step
    cur_prog = prog
    cur_states = list(init_states)
    cur_shared = init_shared
    prep = cr.Prepared(cur_prog, queries)
    eng = cr.Engine(prep, cur_states, cur_shared)

    expected_snapshots: list[tuple[int, list[Fraction | None]]] = []

    def record_snapshot():
        ans_list = []
        for smask, tidx in queries:
            rat_val, _ = eng.rational((smask, tidx))
            if check_enum:
                enum_val = cr.enumerate_probability(cur_prog, cur_states, smask, tidx, cur_shared)
                if rat_val != enum_val:
                    raise AssertionError(
                        f"{name}: Python Engine.rational {rat_val} != enumerate_probability {enum_val}"
                    )
            ans_list.append(rat_val)
        expected_snapshots.append((len(prep.families), ans_list))

    record_snapshot()

    metta_lines = [
        "!(import! &self (library lib_random_closure))",
        f"(= (case:q-step $eng) ("
        + " ".join(
            f"((rc:query-exact $eng {metta_query(prog.boundary, sm, ti)}) "
            f"(rc:query $eng {metta_query(prog.boundary, sm, ti)}))"
            for sm, ti in queries
        )
        + "))",
        "!(let* (",
        f"  ($prep (rc:prepare {metta_program(prog)} {metta_tuple([metta_query(prog.boundary, sm, ti) for sm, ti in queries])}))",
        f"  ($e0 (rc:engine-new-with-shared $prep {metta_tuple([metta_local_state(s) for s in init_states])} {metta_local_state(init_shared)}))",
    ]

    step_vars = ["$e0"]
    for s_idx, step in enumerate(steps, start=1):
        prev_var = step_vars[-1]
        cur_var = f"$e{s_idx}"
        if step[0] == "update":
            _, changed_dict, new_shared = step
            eng.update(changed_dict, new_shared)
            for k, v in changed_dict.items():
                cur_states[k] = v
            if new_shared is not None:
                cur_shared = new_shared
            upd_pairs = metta_tuple([f"({k} {metta_local_state(v)})" for k, v in changed_dict.items()])
            sh_expr = "keep" if new_shared is None else metta_local_state(new_shared)
            metta_lines.append(f"  ({cur_var} (rc:engine-update {prev_var} {upd_pairs} {sh_expr}))")
        elif step[0] == "replace":
            _, comp_idx, new_comp, new_st = step
            new_comps = list(cur_prog.components)
            new_comps[comp_idx] = new_comp
            cur_prog = cr.Program(
                boundary=cur_prog.boundary,
                shared=cur_prog.shared,
                components=tuple(new_comps),
            )
            cur_states[comp_idx] = new_st
            prep = cr.Prepared(cur_prog, queries)
            eng = cr.Engine(prep, cur_states, cur_shared)
            metta_lines.append(
                f"  ({cur_var} (rc:engine-replace-component {prev_var} {comp_idx} "
                f"{metta_component(new_comp)} {metta_local_state(new_st)}))"
            )
        else:
            raise ValueError(f"Unknown step kind: {step[0]}")
        step_vars.append(cur_var)
        record_snapshot()

    metta_lines.append(")")
    out_items = ["(rc:prepared-family-count $prep)"] + [f"(case:q-step {v})" for v in step_vars]
    metta_lines.append(f"  (CaseOut {' '.join(out_items)}))")
    script = "\n".join(metta_lines)

    proc = subprocess.run(
        [str(cetta_bin), "--lang", "petta", "-e", script],
        capture_output=True,
        text=True,
        check=True,
    )
    parsed = parse_sexpr(proc.stdout)
    case_out = None
    for item in parsed:
        if isinstance(item, list) and item and item[0] == "CaseOut":
            case_out = item
            break
    if case_out is None:
        raise AssertionError(f"{name}: missing CaseOut in CeTTa stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")

    got_fam_count = int(case_out[1])
    exp_init_fam_count = expected_snapshots[0][0]
    if got_fam_count != exp_init_fam_count:
        raise AssertionError(f"{name}: family count {got_fam_count} != reference {exp_init_fam_count}")

    checked_queries = 0
    for snap_idx, (_, exp_answers) in enumerate(expected_snapshots):
        got_step = case_out[2 + snap_idx]
        if len(got_step) != len(exp_answers):
            raise AssertionError(f"{name} step {snap_idx}: answer count mismatch")
        for q_idx, (pair_expr, exp_frac) in enumerate(zip(got_step, exp_answers)):
            exact_expr, interval_expr = pair_expr[0], pair_expr[1]
            got_exact = parse_exact_answer(exact_expr)
            if got_exact != exp_frac:
                raise AssertionError(
                    f"{name} step {snap_idx} query {q_idx}: exact {got_exact} != reference {exp_frac}"
                )
            verify_interval_answer(interval_expr, exp_frac, f"{name}[step={snap_idx},q={q_idx}]")
            checked_queries += 1

    return len(expected_snapshots), checked_queries


def run_adversarial_weighted_and_ledger_checks(cetta_bin: Path) -> int:
    # 1. Subtree underflow (10^-180 * 10^-180 -> 0.0 in f64) paired with large
    #    unnormalized sibling weights (10^70 * 10^70 = 10^140 <= 1e150), plus
    #    a second variant where sibling weights exceed 1e150 (10^95 * 10^95 = 10^190).
    #    In both cases exact P(q) = 1 - (1/2)^4 = 15/16; f64 must NEVER falsely
    #    return ImpossibleEvidence or a non-enclosing interval.
    script = """
!(import! &self (library lib_random_closure))
!(import! &self (library lib_evidence_ledger))
!(let* (($prog (RC:Program (q) ()
                 ((RC:Component c0 (x0) () ((RC:Rule q (x0))))
                  (RC:Component c1 (x1) () ((RC:Rule q (x1))))
                  (RC:Component c2 (x2) () ((RC:Rule q (x2))))
                  (RC:Component c3 (x3) () ((RC:Rule q (x3)))))))
        ($prep (rc:prepare $prog ((RC:Query () q))))
        ($ tiny (Rat 1 (rc:pow10 180)))
        ($big1 (Rat (rc:pow10 70) 1))
        ($big2 (Rat (rc:pow10 95) 1))
        ($eA (rc:engine-new $prep
               ((RC:WeightedState (($tiny $tiny)))
                (RC:WeightedState (($tiny $tiny)))
                (RC:WeightedState (($big1 $big1)))
                (RC:WeightedState (($big1 $big1))))))
        ($eB (rc:engine-new $prep
               ((RC:WeightedState (($tiny $tiny)))
                (RC:WeightedState (($tiny $tiny)))
                (RC:WeightedState (($big2 $big2)))
                (RC:WeightedState (($big2 $big2))))))
        ; Ledger boundary checks: extreme logits, retracted-ID immutability, channel collision
        ($p1 (rc:prepare (RC:Program (q) () ((RC:Component c (x) () ((RC:Rule q (x)))))) ((RC:Query () q))))
        ($b0 (ledger:bind-engine (ledger:from-prepared $p1 (((Rat 1 2))) ())))
        ((Ledger:Step $t1 $b1) (ledger:observe $b0 (ledger:hard-obs rep_id x true)))
        ((Ledger:Step $t2 $b2) (ledger:retract $b1 rep_id))
        ($mut (ledger:observe $b2 (ledger:hard-obs rep_id x false)))
        ((Ledger:Step $t3 $b3) (ledger:observe-id $b2 rep_id))
        ((Ledger:Step $tc1 $bc1)
         (ledger:observe $b0 (ledger:channel-likelihood-obs o1 ch_same x (Rat 1 1) (Rat 4 1))))
        ($col (ledger:observe $bc1 (ledger:channel-likelihood-obs o2 ch_same x (Rat 1 1) (Rat 4 1))))
        ($w0 (tensor:from-values f64 (1 1) (0.0)))
        ($x0 (tensor:from-values f64 (1) (0.0)))
        ($sp (ledger:affine-sigmoid-prob $w0 $x0 40.0))
        ($sn (ledger:affine-sigmoid-prob $w0 $x0 -40.0)))
   (AdversarialOut
     ((rc:query-exact $eA (RC:Query () q)) (rc:query $eA (RC:Query () q)))
     ((rc:query-exact $eB (RC:Query () q)) (rc:query $eB (RC:Query () q)))
     (rc:unsupported? $mut)
     $t3
     (ledger:query-exact $b3 (RC:Query () q))
     (rc:unsupported? $col)
     $sp
     $sn))
""".replace("$ tiny", "$tiny")
    proc = subprocess.run(
        [str(cetta_bin), "--lang", "petta", "-e", script],
        capture_output=True,
        text=True,
        check=True,
    )
    parsed = parse_sexpr(proc.stdout)
    adv_out = next((x for x in parsed if isinstance(x, list) and x and x[0] == "AdversarialOut"), None)
    if adv_out is None:
        raise AssertionError(f"Missing AdversarialOut:\n{proc.stdout}\n{proc.stderr}")

    expected_15_16 = Fraction(15, 16)
    for idx, label in ((1, "underflow_times_1e140"), (2, "underflow_times_1e190")):
        ex_expr, iv_expr = adv_out[idx]
        if parse_exact_answer(ex_expr) != expected_15_16:
            raise AssertionError(f"{label}: exact != 15/16: {ex_expr!r}")
        verify_interval_answer(iv_expr, expected_15_16, label)

    if adv_out[3] != "true":
        raise AssertionError(f"Expected retracted-ID mutation rejection, got {adv_out[3]!r}")
    if adv_out[4] != "Activated" or parse_exact_answer(adv_out[5]) != Fraction(1, 1):
        raise AssertionError(f"Expected observe-id reactivation to 1/1, got {adv_out[4:6]!r}")
    if adv_out[6] != "true":
        raise AssertionError(f"Expected same-channel collision rejection, got {adv_out[6]!r}")

    sp_rat = Fraction(int(adv_out[7][1]), int(adv_out[7][2]))
    sn_rat = Fraction(int(adv_out[8][1]), int(adv_out[8][2]))
    if not (Fraction(0, 1) < sn_rat < Fraction(1, 1) and Fraction(0, 1) < sp_rat < Fraction(1, 1)):
        raise AssertionError(f"Neural extreme logits escaped interior (0, 1): sn={sn_rat}, sp={sp_rat}")
    return 6


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=str, default=None, help="Path to cetta binary")
    parser.add_argument("--reference-py", type=str, default=None, help="Path to closure_response.py")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    cetta_bin = Path(args.cetta) if args.cetta else (repo_root / "cetta")
    if not cetta_bin.is_file():
        raise FileNotFoundError(f"cetta binary not found at {cetta_bin}")

    cr = load_reference_module(args.reference_py)
    cases = build_cases(cr)

    total_snapshots = 0
    total_queries = 0
    for case in cases:
        snaps, qs = run_case(cetta_bin, cr, case)
        total_snapshots += snaps
        total_queries += qs
        print(f"[PASS] {case[0]}: {snaps} snapshots, {qs} zero-slack exact+enclosure checks matched closure_response.py")

    adv_checks = run_adversarial_weighted_and_ledger_checks(cetta_bin)
    print(f"[PASS] adversarial_weighted_and_ledger_boundaries: {adv_checks} boundary & underflow checks passed")

    print(
        f"ALL PASSED: {len(cases)} multi-step models, {total_snapshots} snapshots, "
        f"{total_queries} zero-slack exact rational + IEEE-754/decimal enclosure checks "
        f"+ {adv_checks} adversarial underflow/ledger checks."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
