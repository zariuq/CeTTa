#!/usr/bin/env python3
"""First-answer transform cursors agree with materializing proposal traversal."""

import os
from pathlib import Path
import random
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "runtime"
REFERENCE = "CETTA_TRANSFORM_CURSOR_REFERENCE"


def source(lines):
    return "\n".join(lines) + "\n"


def run(binary, text, name, reference=False, stats=False, timeout=120,
        extra_env=None):
    path = RUNTIME / f"transform_stream_{name}.metta"
    path.write_text(text)
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    if reference:
        env[REFERENCE] = "1"
    else:
        env.pop(REFERENCE, None)
    command = [binary, "--lang", "petta", "--profile", "extended"]
    if stats:
        command.append("--emit-runtime-stats")
    command.append(str(path))
    proc = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                          text=True, timeout=timeout)
    counters = {}
    for line in proc.stderr.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
    return proc, counters


def normalized(text):
    text = re.sub(r"#\d+", "#VAR", text)
    return re.sub(r"<space 0x[0-9a-f]+>", "<space>", text)


def diverges(binary, text, name):
    fast, _ = run(binary, text, name)
    slow, _ = run(binary, text, name + "_reference", reference=True)
    return (fast.returncode != slow.returncode or
            normalized(fast.stdout) != normalized(slow.stdout) or
            bool(fast.returncode) or normalized(fast.stderr) != normalized(slow.stderr)), fast, slow


def shrink(binary, text):
    """Drop store rows while the same fast/reference divergence survives."""
    lines = text.splitlines()
    changed = True
    while changed:
        changed = False
        for i, line in enumerate(list(lines)):
            if not line.startswith("!(add-atom "):
                continue
            candidate = lines[:i] + lines[i + 1:]
            bad, _, _ = diverges(binary, source(candidate), "transform_shrink")
            if bad:
                lines = candidate
                changed = True
                break
    return source(lines)


def differential(binary, text, name):
    bad, fast, slow = diverges(binary, text, name)
    if not bad:
        return
    minimized = shrink(binary, text)
    (RUNTIME / "transform_stream_minimal_divergence.metta").write_text(minimized)
    raise AssertionError(
        f"cursor/reference divergence in {name}\n"
        f"fast rc={fast.returncode} out={fast.stdout!r} err={fast.stderr!r}\n"
        f"reference rc={slow.returncode} out={slow.stdout!r} err={slow.stderr!r}\n"
        f"minimal candidate: {minimized}")


def join_program(n, mode="take", template="claimed"):
    lines = ["!(bind! &join (new-space))"]
    lines += [f"!(add-atom &join (left {i}))" for i in range(n)]
    lines += [f"!(add-atom &join (right {i}))" for i in range(n)]
    lines += ["!(reset-runtime-stats!)",
              f"!(once (transform &join (, ({mode} (left $x)) ({mode} (right $y))) "
              f"({template} $x $y)))"]
    return source(lines)


def acceptance(binary):
    owned_peaks = []
    accounted_peaks = []
    # TfFire holds one pointer to the all-extension binding image.  Each of
    # the join's two levels holds two occurrence cursors, and each cursor a
    # pointer to the composition of a space with dependencies.
    expected_owned_peak = 3968
    for n in (200, 400):
        proc, counters = run(binary, join_program(n), f"join_{n}", stats=True)
        if proc.returncode != 0 or proc.stdout.splitlines()[-1] != "(claimed 0 0)":
            raise AssertionError(f"first join answer failed for n={n}: "
                                 f"{proc.stdout!r}; {proc.stderr!r}")
        expected = {
            "transform-cursor-open": 1,
            "transform-cursor-close": 1,
            "transform-cursor-candidate": 1,
            "transform-cursor-scan-row": 2,
            "transform-cursor-proposal-alloc": 10,
        }
        for key, value in expected.items():
            if counters.get(key) != value:
                raise AssertionError(f"{key}: expected {value}, got {counters.get(key)} "
                                     f"for {n * n} answers")
        peak = counters.get("transform-cursor-accounted-bytes-peak", 0)
        owned = counters.get("transform-cursor-owned-bytes-peak", 0)
        if peak == 0 or owned == 0 or peak < owned:
            raise AssertionError(f"invalid cursor byte accounting: total={peak}, owned={owned}")
        # The cursor's own vectors, and with them what its readers' view
        # keeps: the list of the rows removed since the take froze it, not a
        # copy of the rows, so neither grows with the space.
        # The fixed firing record includes the matching-frame loan and
        # prepared destination bookkeeping. Vectors retain the same bounds.
        expected_peaks = (expected_owned_peak, 4096)
        if (owned, peak) != expected_peaks:
            raise AssertionError(
                f"cursor-owned vector accounting changed for n={n}: "
                f"expected {expected_peaks}, got {(owned, peak)}")
        owned_peaks.append(owned)
        accounted_peaks.append(peak)
        print(f"PASS: once over {n * n} answers: one candidate, two scanned rows, "
              f"owned peak {owned} bytes, entry-inclusive accounted peak {peak} bytes")
    if owned_peaks[0] != owned_peaks[1]:
        raise AssertionError(f"cursor-owned peak grew from n to 2n: {owned_peaks}")
    if accounted_peaks[1] != accounted_peaks[0]:
        raise AssertionError(
            "the taking case kept rows for its readers: the accounted peak "
            f"grew from n to 2n: {accounted_peaks}")

    n = 100
    lines = ["!(bind! &s (new-space))"]
    lines += [f"!(add-atom &s (item {i}))" for i in range(n)]
    lines += ["!(reset-runtime-stats!)",
              "!(once (transform &s (, (read (item $x)) (take (item $x))) (got $x)))",
              "!(remaining (size-atom (collapse (match &s $x $x))))"]
    proc, counters = run(binary, source(lines), "long_rejected_prefix", stats=True)
    if proc.returncode != 0 or proc.stdout.splitlines()[-2:] != ["()", "(remaining 100)"]:
        raise AssertionError(f"rejected-prefix observation changed: {proc.stdout!r}; {proc.stderr!r}")
    for key, expected in (("transform-cursor-candidate", n),
                          ("transform-cursor-open", 1),
                          ("transform-cursor-close", 1)):
        if counters.get(key) != expected:
            raise AssertionError(f"long rejected prefix {key}: expected {expected}, got {counters.get(key)}")
    print(f"PASS: {n} rejected candidates are fully observed without changing the store")

    proc, counters = run(binary, """!(bind! &s (new-space))
!(reset-runtime-stats!)
!(once (transform &s (take (missing $x)) (got $x)))
""", "empty_counter_gate", stats=True)
    if proc.returncode != 0 or proc.stdout.splitlines()[-1] != "()":
        raise AssertionError(f"empty producer answer changed: {proc.stdout!r}")
    if counters.get("transform-cursor-candidate") != 0 or counters.get("transform-cursor-scan-row") != 0:
        raise AssertionError(f"empty producer performed candidate work: {counters}")
    if counters.get("transform-cursor-open") != counters.get("transform-cursor-close"):
        raise AssertionError("empty producer leaked its cursor")

    lines = ["!(bind! &s (new-space))"]
    lines += [f"!(add-atom &s (left {i}))" for i in range(200)]
    lines += [f"!(add-atom &s (right {i}))" for i in range(200, 400)]
    lines += ["!(reset-runtime-stats!)",
              "!(once (transform &s (, (read (left $x)) (read (right $x))) (claimed $x)))"]
    proc, counters = run(binary, source(lines), "no_proposal_accounting", stats=True)
    if proc.returncode != 0:
        raise AssertionError(f"zero-proposal traversal failed: {proc.stdout!r}; {proc.stderr!r}")
    for key, expected in (("transform-cursor-open", 1),
                          ("transform-cursor-close", 1),
                          ("transform-cursor-candidate", 0),
                          ("transform-cursor-scan-row", 40200)):
        if counters.get(key) != expected:
            raise AssertionError(f"zero-proposal {key}: expected {expected}, got {counters.get(key)}")
    owned = counters.get("transform-cursor-owned-bytes-peak", 0)
    accounted = counters.get("transform-cursor-accounted-bytes-peak", 0)
    expected_peaks = (expected_owned_peak, expected_owned_peak)
    if (owned, accounted) != expected_peaks:
        raise AssertionError(
            f"zero-proposal high-water accounting changed: expected {expected_peaks}, "
            f"got {(owned, accounted)}")
    print(f"PASS: zero proposals retain the failed-match owned peak {owned} bytes "
          f"and entry-inclusive peak {accounted} bytes")

    effectful_resume = """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(reset-runtime-stats!)
!(select 1 (transform &s (take (item $x))
  (let $_ (add-atom &s (item c)) (empty))))
!(remaining (collapse (match &s $x $x)))
"""
    proc, counters = run(binary, effectful_resume, "effectful_resume_gate", stats=True)
    if proc.returncode != 0 or proc.stdout.splitlines()[-1] != "(remaining ((item c) (item c)))":
        raise AssertionError(f"later-insert snapshot observation changed: {proc.stdout!r}")
    for key, expected in (("transform-cursor-open", 1),
                          ("transform-cursor-close", 1),
                          ("transform-cursor-candidate", 2)):
        if counters.get(key) != expected:
            raise AssertionError(f"effectful resume {key}: expected {expected}, got {counters.get(key)}")
    print("PASS: later inserts stay outside the captured two-candidate view through exhaustion")

    small_full_drain = """!(bind! &s (new-space))
!(add-atom &s (p a))
!(add-atom &s (p b))
!(add-atom &s (q 1))
!(add-atom &s (q 2))
!(reset-runtime-stats!)
!(all (collapse (transform &s (, (take (p $x)) (take (q $y))) (pair $x $y))))
!(remaining (size-atom (collapse (match &s $x $x))))
"""
    proc, counters = run(binary, small_full_drain, "small_full_drain_policy", stats=True)
    if proc.returncode != 0 or proc.stdout.splitlines()[-2:] != [
            "(all ((pair a 1) (pair b 2)))", "(remaining 0)"]:
        raise AssertionError(f"small eager drain changed the bag: {proc.stdout!r}")
    for key in ("transform-cursor-open", "transform-cursor-close",
                "transform-cursor-candidate"):
        if counters.get(key) != 0:
            raise AssertionError("small public collection did not retain its eager specialization")

    lines = ["!(bind! &s (new-space))"]
    lines += [f"!(add-atom &s (p {i}))" for i in range(8)]
    lines += [f"!(add-atom &s (q {i}))" for i in range(8)]
    lines += ["!(reset-runtime-stats!)",
              "!(all (collapse (transform &s (, (take (p $x)) (take (q $y))) (pair $x $y))))",
              "!(remaining (size-atom (collapse (match &s $x $x))))"]
    proc, counters = run(binary, source(lines), "full_drain_counter_gate", stats=True)
    expected_answers = "(all (" + " ".join(
        f"(pair {i} {i})" for i in range(8)) + "))"
    if proc.returncode != 0 or proc.stdout.splitlines()[-2:] != [
            expected_answers, "(remaining 0)"]:
        raise AssertionError(f"full cursor drain changed the bag: {proc.stdout!r}")
    for key, expected in (("transform-cursor-open", 1),
                          ("transform-cursor-close", 1),
                          ("transform-cursor-candidate", 64)):
        if counters.get(key) != expected:
            raise AssertionError(f"full drain {key}: expected {expected}, got {counters.get(key)}")
    print("PASS: small public drains stay eager; the cursor exhausts 64 proposals and eight diagonal firings")

    lines = ["!(bind! &s (new-space))"]
    lines += [f"!(add-atom &s (item {i}))" for i in range(100)]
    lines += ["!(reset-runtime-stats!)",
              "!(once (transform &s (take (item $x)) (item c) &s))",
              "!(remaining (size-atom (collapse (match &s $x $x))))",
              "!(copies (size-atom (collapse (match &s (item c) $x))))"]
    proc, counters = run(binary, source(lines), "destination_write_detaches_none", stats=True)
    if proc.returncode != 0 or proc.stdout.splitlines()[-2:] != [
            "(remaining 100)", "(copies 1)"]:
        raise AssertionError(f"self-destination first answer changed the store: {proc.stdout!r}; {proc.stderr!r}")
    for key, expected in (("transform-cursor-open", 1),
                          ("transform-cursor-close", 1),
                          ("transform-cursor-candidate", 1),
                          ("space-pinned-occurrences-detach", 0),
                          ("space-pinned-occurrences-detached-row", 0)):
        if counters.get(key) != expected:
            raise AssertionError(f"destination write {key}: expected {expected}, got {counters.get(key)}")
    print("PASS: a first-answer self-destination write closes its reader without detaching 99 unread rows")


def first_firing_demand(binary):
    start = """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
"""
    take = "(transform &s (take (item $x)) (empty))"
    claim = "(= (claim $space) (transform $space (take (item $x)) (empty)))\n"
    first_take = ["(result ())", "(remaining ((item b)))"]
    cases = {
        "empty_once_template": (
            "!(result (collapse (once (transform &s (take (item $x)) (empty)))))",
            ["(result ())", "(remaining ((item b)))"]),
        "once_forwarded_let": (
            f"!(result (collapse (once (let $r {take} $r))))", first_take),
        "once_forwarded_chain": (
            f"!(result (collapse (once (chain {take} $r $r))))", first_take),
        "once_returned_call": (
            claim + "!(result (collapse (once (claim &s))))", first_take),
        "once_forwarded_call": (
            claim + "(= (wrap $space) (let $r (claim $space) $r))\n"
            "!(result (collapse (once (wrap &s))))", first_take),
        "once_returned_if_branch": (
            claim + "!(result (collapse (once (if True (claim &s) other))))",
            first_take),
        "once_forwarded_let_star": (
            f"!(result (collapse (once (let* (($a 1) ($r {take})) $r))))", first_take),
        "once_forwarded_unify_branch": (
            f"!(result (collapse (once (unify a a {take} other))))", first_take),
        "once_forwarded_unify_else_branch": (
            f"!(result (collapse (once (unify a b other {take}))))", first_take),
        "once_does_not_limit_an_earlier_let_star_binding": (
            "(= (reply a) (empty)) (= (reply b) later)\n"
            "!(result (collapse (once (let* (($r (transform &s (take (item $x)) "
            "(reply $x))) ($a 1)) $r))))",
            ["(result (later))", "(remaining ())"]),
        "once_returned_runtime_dispatch": (
            "!(import! &self (library lib_transform))\n"
            "(= (claim $space $op) ($op $space (take (item $x)) (empty)))\n"
            "!(result (collapse (once (claim &s transform))))", first_take),
        "once_returned_call_in_equation": (
            claim + "(= (wrap $space) (once (claim $space)))\n"
            "!(result (collapse (wrap &s)))", first_take),
        "once_tries_later_alternatives_after_answerless_firing": (
            claim + "(= (claim $space) (transform $space (take (item $x)) (got $x)))\n"
            "!(result (collapse (once (claim &s))))",
            ["(result ((got b)))", "(remaining ())"]),
        "once_retries_rejected_firing": (
            "(= (claim $space) (transform $space (take (missing $x)) (empty)))\n"
            + claim + "!(result (collapse (once (claim &s))))", first_take),
        "once_does_not_limit_earlier_work": (
            "(= (reply a) (empty)) (= (reply b) later)\n"
            "!(result (collapse (once (let $r (transform &s (take (item $x)) (reply $x)) "
            "(Box $r)))))",
            ["(result ((Box later)))", "(remaining ())"]),
        "select_keeps_answer_demand": (
            claim + "!(result (collapse (select 1 (claim &s))))",
            ["(result ())", "(remaining ())"]),
        "empty_once_packet": (
            "!(result (collapse (once (transform &s (take (item $x)) (,)))))",
            ["(result ())", "(remaining ((item b)))"]),
        "empty_all_extension": (
            "!(result (collapse (once (transform &s "
            "(, (take (item $x)) (all (missing $y))) (got $x)))))",
            ["(result ())", "(remaining ((item b)))"]),
        "first_valid_firing": (
            "!(add-atom &s (item b))\n"
            "!(result (collapse (once (transform &s "
            "(, (take (item $x)) (take (item $x))) (empty)))))",
            ["(result ())", "(remaining ((item a)))"]),
        "later_answer_after_take": (
            "(= (reply a) (empty)) (= (reply b) later)\n"
            "!(result (collapse (once (transform &s (take (item $x)) (reply $x)))))",
            ["(result ())", "(remaining ((item b)))"]),
        "later_answer_in_same_firing": (
            "!(result (collapse (once (transform &s (take (item $x)) (, (empty) (got $x))))))",
            ["(result ((got a)))", "(remaining ((item b)))"]),
        "once_in_compiled_equation": (
            "(= (claim $space) (once (transform $space (take (item $x)) (empty))))\n"
            "!(result (collapse (claim &s)))",
            ["(result ())", "(remaining ((item b)))"]),
        "once_in_transaction": (
            "!(result (transaction (collapse (once (transform &s (take (item $x)) (empty))))))",
            ["(result ())", "(remaining ((item b)))"]),
        "once_forwarded_in_transaction": (
            f"!(result (transaction (collapse (once (let $r {take} $r)))))",
            first_take),
        "answerless_transaction_rolls_back": (
            "!(result (collapse (transaction (once (transform &s (take (item $x)) (empty))))))",
            ["(result ())", "(remaining ((item a) (item b)))"]),
        "read_once_keeps_answer_demand": (
            "(= (reply a) (empty)) (= (reply b) later)\n"
            "!(result (collapse (once (transform &s (item $x) (reply $x)))))",
            ["(result (later))", "(remaining ((item a) (item b)))"]),
        "returned_read_keeps_answer_demand": (
            "(= (reply a) (empty)) (= (reply b) later)\n"
            "(= (look $space) (transform $space (item $x) (reply $x)))\n"
            "!(result (collapse (once (look &s))))",
            ["(result (later))", "(remaining ((item a) (item b)))"]),
    }
    for name, (body, expected) in cases.items():
        program = start + body + "\n!(remaining (collapse (match &s $x $x)))\n"
        for machine in ("0", "1"):
            for reference in (False, True):
                proc, _ = run(binary, program, name, reference=reference,
                              extra_env={"CETTA_PETTA_SEARCH_MACHINE": machine})
                got = proc.stdout.splitlines()[-2:]
                if proc.returncode or got != expected:
                    raise AssertionError(
                        f"{name}, PeTTa machine={machine}, reference cursor={reference}: "
                        f"expected {expected}, got {got}, stderr {proc.stderr!r}")
    print(f"PASS: {len(cases)} first-firing cases cover returned calls, answerless takes "
          "and ordinary answer demand")

    authored = ("!(import! &self (library lib_transform_specs))\n" + start +
                "(= (claim $space) (tfs:run-first $space (take (item $x)) (empty) tfs:caller))\n"
                "!(result (collapse (let $r (claim &s) $r)))\n"
                "!(remaining (collapse (match &s $x $x)))\n")
    proc, _ = run(binary, authored, "authored_first_packet")
    got = proc.stdout.splitlines()[-2:]
    if proc.returncode or got != first_take:
        raise AssertionError(f"authored first packet: expected {first_take}, got {got}, "
                             f"stderr {proc.stderr!r}")


def first_firing_reference(binary):
    """Native first-firing demand answers, and leaves the store, as the
    authored reference does. The reference writes tfs:run-first for each
    transform whose result returns to a once, and tfs:run for every other."""
    start = """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
"""
    native = (lambda space, sources, template:
              f"(transform {space} {sources} {template})",) * 2
    authored = (lambda space, sources, template:
                f"(tfs:run-first {space} {sources} {template} tfs:caller)",
                lambda space, sources, template:
                f"(tfs:run {space} {sources} {template} tfs:caller)")
    take = "(take (item $x))"
    cases = {
        "direct": (lambda F, S:
            f"!(result (collapse (once {F('&s', take, '(empty)')})))",
            ["(result ())", "(remaining ((item b)))"]),
        "direct_answer": (lambda F, S:
            f"!(result (collapse (once {F('&s', take, '(got $x)')})))",
            ["(result ((got a)))", "(remaining ((item b)))"]),
        "let_wrapped": (lambda F, S:
            f"!(result (collapse (once (let $r {F('&s', take, '(empty)')} $r))))",
            ["(result ())", "(remaining ((item b)))"]),
        "function_wrapped": (lambda F, S:
            f"(= (claim $sp) {F('$sp', take, '(empty)')})\n"
            "!(result (collapse (once (claim &s))))",
            ["(result ())", "(remaining ((item b)))"]),
        "source_once": (lambda F, S:
            f"!(result (collapse {S('&s', '(once (take (item $x)))', '(empty)')}))",
            ["(result ())", "(remaining ((item b)))"]),
        "if_branch": (lambda F, S:
            f"(= (pick $c $sp) (if $c {F('$sp', take, '(empty)')} nothing))\n"
            "!(result (collapse (once (pick True &s))))",
            ["(result ())", "(remaining ((item b)))"]),
        "nested_function": (lambda F, S:
            f"(= (inner $sp) {F('$sp', take, '(empty)')})\n"
            "(= (outer $sp) (inner $sp))\n"
            "!(result (collapse (once (outer &s))))",
            ["(result ())", "(remaining ((item b)))"]),
        "chain": (lambda F, S:
            f"!(result (collapse (once (chain {F('&s', take, '(empty)')} $r $r))))",
            ["(result ())", "(remaining ((item b)))"]),
        "let_star_identity": (lambda F, S:
            f"!(result (collapse (once (let* (($a 1) ($r {F('&s', take, '(empty)')})) $r))))",
            ["(result ())", "(remaining ((item b)))"]),
        "unify_branch": (lambda F, S:
            f"!(result (collapse (once (unify a a {F('&s', take, '(empty)')} other))))",
            ["(result ())", "(remaining ((item b)))"]),
        "let_star_earlier_binding": (lambda F, S:
            f"!(result (collapse (once (let* (($r {S('&s', take, '(empty)')}) ($a 1)) $r))))",
            ["(result ())", "(remaining ())"]),
        "superpose_then_value": (lambda F, S:
            f"!(result (collapse (once (superpose ({F('&s', take, '(empty)')} done)))))",
            ["(result (done))", "(remaining ((item b)))"]),
        "two_transforms_then_value": (lambda F, S:
            "!(result (collapse (once (superpose ("
            f"{F('&s', '(take (item a))', '(empty)')} "
            f"{F('&s', '(take (item b))', '(empty)')} done)))))",
            ["(result (done))", "(remaining ())"]),
        "answerless_equation_then_fallback": (lambda F, S:
            f"(= (act $sp) {F('$sp', take, '(empty)')})\n"
            "(= (act $sp) fallback)\n"
            "!(result (collapse (once (act &s))))",
            ["(result (fallback))", "(remaining ((item b)))"]),
        "fallback_equation_first": (lambda F, S:
            "(= (act $sp) fallback)\n"
            f"(= (act $sp) {F('$sp', take, '(empty)')})\n"
            "!(result (collapse (once (act &s))))",
            ["(result (fallback))", "(remaining ((item a) (item b)))"]),
        "later_equation_answers": (lambda F, S:
            f"(= (claim $sp) {F('$sp', take, '(empty)')})\n"
            f"(= (claim $sp) {F('$sp', take, '(got $x)')})\n"
            "!(result (collapse (once (claim &s))))",
            ["(result ((got b)))", "(remaining ())"]),
        "let_uses_result": (lambda F, S:
            f"!(result (collapse (once (let $r {S('&s', take, '(empty)')} (seen $r)))))",
            ["(result ())", "(remaining ())"]),
        "collapse_inside": (lambda F, S:
            f"!(result (collapse (once (let $all (collapse {S('&s', take, '(got $x)')}) $all))))",
            ["(result (((got a) (got b))))", "(remaining ())"]),
        "select_one": (lambda F, S:
            f"!(result (collapse (select 1 {S('&s', take, '(empty)')})))",
            ["(result ())", "(remaining ())"]),
        "read_only_once": (lambda F, S:
            f"!(result (collapse (once {S('&s', '(item $x)', '(saw $x)')})))",
            ["(result ((saw a)))", "(remaining ((item a) (item b)))"]),
    }
    tail = "\n!(remaining (collapse (match &s $x $x)))\n"
    for name, (body, expected) in cases.items():
        native_program = start + body(*native) + tail
        authored_program = ("!(import! &self (library lib_transform_specs))\n" +
                            start + body(*authored) + tail)
        for machine in ("0", "1"):
            for reference in (False, True):
                where = f"{name}, PeTTa machine={machine}, reference cursor={reference}"
                env = {"CETTA_PETTA_SEARCH_MACHINE": machine}
                ours, _ = run(binary, native_program, "ffr_" + name,
                              reference=reference, extra_env=env)
                theirs, _ = run(binary, authored_program, "ffr_authored_" + name,
                                reference=reference, extra_env=env)
                got = ours.stdout.splitlines()[-2:]
                specified = theirs.stdout.splitlines()[-2:]
                if theirs.returncode or specified != expected:
                    raise AssertionError(f"{where}: the authored reference gave {specified}, "
                                         f"expected {expected}, stderr {theirs.stderr!r}")
                if ours.returncode or got != specified:
                    raise AssertionError(f"{where}: native gave {got}, the authored "
                                         f"reference {specified}, stderr {ours.stderr!r}")
    print(f"PASS: {len(cases)} once forms answer and leave the store as the authored "
          "reference on both evaluator routes and both cursor modes")


def compiled_once_demand(binary):
    """A once that a compiled equation runs carries first-firing demand on the
    continuation that receives a transform's result unchanged, as the search
    machine's once does: through a returning equation, an identity let or
    chain, a let* whose body is its last binding, a selected if or case or
    unify branch, a sequence's or a constructor's last element, and the
    alternatives of a literal superpose, also when a program change sends a
    builtin entry's occurrence to the host. A let that computes another
    result, a let whose pattern is a structure, a let*'s earlier binding and
    an earlier constructor argument leave the transform under its own demand.
    Both routes and both cursor modes give the same answers and leave the
    same rows; on the compiled route the region runs the once itself and
    hands the host only what it does not compile."""
    items = """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
(= (pick a) (empty))
(= (pick b) (A b))
(= (id $x) $x)
(= (claim $sp) (transform $sp (take (item $x)) (pick $x)))
"""
    take = "(transform $sp (take (item $x)) (pick $x))"
    first = ["(result ())", "(remaining ((item b)))"]
    own = ["(result ((A b)))", "(remaining ())"]
    bodies = {
        "returned_call": ("(claim $sp)", first),
        "direct": (take, first),
        "identity_let": (f"(let $r {take} $r)", first),
        "identity_chain": (f"(chain {take} $r $r)", first),
        "if_branch": (f"(if True {take} other)", first),
        "case_arm": (f"(case 1 ((1 {take}) ($_ other)))", first),
        "sequence_last": (f"(progn (+ 1 1) {take})", first),
        "superpose_then_value": (f"(superpose ({take} done))",
                                 ["(result (done))", "(remaining ((item b)))"]),
        "constructor_last": (f"(Box {take})", first),
        "nested_identity_let": (f"(let $a 1 (let $r {take} $r))", first),
        "let_star_identity": (f"(let* (($a 1) ($r {take})) $r)", first),
        "let_star_one_binding": (f"(let* (($r {take})) $r)", first),
        "unify_then_branch": (f"(unify a a {take} other)", first),
        "unify_else_branch": (f"(unify a b other {take})", first),
        "unify_branch_with_bindings": (f"(unify (pair $p $q) (pair 1 2) {take} other)", first),
        "let_computes_another_result": (f"(let $r {take} (id $r))", own),
        "let_with_structured_pattern": (f"(let (A $y) {take} (A $y))", own),
        "let_star_earlier_binding": (f"(let* (($r {take}) ($a 1)) $r)", own),
        "let_star_computes_another_result": (f"(let* (($a 1) ($r {take})) (id $r))", own),
        "earlier_constructor_argument": (f"(Pair {take} x)",
                                         ["(result ((Pair (A b) x)))", "(remaining ())"]),
    }
    programs = {name: (items + f"(= (go $sp) (once {body}))\n", expected)
                for name, (body, expected) in bodies.items()}
    programs["two_bids_spend_one"] = (
        "!(bind! &s (new-space))\n!(add-atom &s (bid ann 5))\n!(add-atom &s (bid bob 7))\n"
        "(= (spend $sp) (transform $sp (take (bid $w $a)) (empty)))\n"
        "(= (go $sp) (once (spend $sp)))\n",
        ["(result ())", "(remaining ((bid bob 7)))"])
    # A program change while the region runs sends a builtin entry's whole
    # occurrence to the host, which keeps the demand its result meets.
    changed = "(let $u (add-atom &self (= (late-fn) 2)) "
    programs["builtin_entry_after_program_change"] = (
        items + f"(= (go $sp) (once {changed}(superpose ({take} done)))))\n",
        ["(result (done))", "(remaining ((item b)))"])
    programs["builtin_entry_in_constructor_after_program_change"] = (
        items + f"(= (go $sp) (once {changed}(Box (superpose ({take} done))))))\n",
        ["(result ((Box done)))", "(remaining ((item b)))"])
    programs["answerless_take_leaves_second_row"] = (
        items + "(= (go $sp) (once (transform $sp (take (item $x)) (empty))))\n",
        first)
    tail = "!(result (collapse (go &s)))\n!(remaining (collapse (match &s $x $x)))\n"
    host_goal = re.compile(r"^open-equations (?:query-body )?host (\S+)/\d+$", re.M)
    for name, (program, expected) in programs.items():
        text = program + tail
        for machine in ("0", "1"):
            for reference in (False, True):
                where = f"{name}, PeTTa machine={machine}, reference cursor={reference}"
                proc, _ = run(binary, text, "cod_" + name, reference=reference,
                              extra_env={"CETTA_PETTA_SEARCH_MACHINE": machine})
                got = proc.stdout.splitlines()[-2:]
                if proc.returncode or got != expected:
                    raise AssertionError(f"{where}: expected {expected}, got {got}, "
                                         f"stderr {proc.stderr!r}")
        proc, _ = run(binary, text, "cod_hosted_" + name, extra_env={
            "CETTA_PETTA_SEARCH_MACHINE": "1", "CETTA_OPEN_EQUATIONS_DEBUG": "host"})
        heads = host_goal.findall(proc.stderr)
        if proc.returncode or not heads or "once" in heads:
            raise AssertionError(f"{name}: the compiled route should run the once and hand the "
                                 f"host only other goals, it handed {heads}")
    print(f"PASS: {len(programs)} onces in compiled equations carry first-firing demand "
          "through returned results exactly as the search machine does, on both routes "
          "and both cursor modes")


def machine_counts(stderr):
    """Sum the PeTTa machine's own counts over each machine run."""
    totals = {}
    for line in stderr.splitlines():
        if not line.startswith("PETTA_MACHINE_STATS "):
            continue
        for item in line.split()[1:]:
            key, _, value = item.partition("=")
            if value.isdigit():
                totals[key] = totals.get(key, 0) + int(value)
    return totals


def once_demand_counters(binary):
    """Counts, not clocks, for a once over a call with several equations. Its
    later alternatives cost nothing, and a loop of such onces over one large
    argument does work linear in the loop on both evaluator routes."""
    def measure(text, name, machine):
        proc, counters = run(binary, text, name, stats=True, extra_env={
            "CETTA_PETTA_SEARCH_MACHINE": machine, "CETTA_PETTA_MACHINE_STATS": "1"})
        if proc.returncode:
            raise AssertionError(f"{name}, machine={machine}: rc={proc.returncode}, "
                                 f"stderr {proc.stderr!r}")
        own = machine_counts(proc.stderr)
        return proc.stdout.splitlines()[-1], {
            "applications": counters.get("open-equation-activation", 0) +
                            own.get("equation_branches_scheduled", 0),
            "admitted": counters.get("open-equation-term-admission-node", 0),
            "transitions": own.get("transitions", 0),
        }

    def alternatives(later, n):
        lines = ["(= (alt $x) $x)"]
        for index in range(later):
            lines.append(f"(= (alt $x) (later{index} $x 64))")
            lines.append(f"(= (later{index} $x $k) (if (== $k 0) $x (later{index} $x (- $k 1))))")
        lines.append("(= (loop $n $s) (if (== $n 0) $s (loop (- $n 1) (+ $s (once (alt $n))))))")
        lines.append(f"!(loop {n} 0)")
        return source(lines)

    for machine in ("0", "1"):
        few_out, few = measure(alternatives(1, 100), "once_later_1", machine)
        many_out, many = measure(alternatives(15, 100), "once_later_15", machine)
        _, longer = measure(alternatives(1, 200), "once_later_1_long", machine)
        if few_out != "5050" or many_out != "5050":
            raise AssertionError(f"once over alternatives, machine={machine}: "
                                 f"{few_out!r}, {many_out!r}")
        if few != many:
            raise AssertionError(f"later alternatives changed a once's work, machine={machine}: "
                                 f"one later {few}, fifteen later {many}")
        if longer["applications"] <= few["applications"]:
            raise AssertionError(f"equation applications are not counted, machine={machine}: "
                                 f"{few} for 100 calls, {longer} for 200")

    def loop(n, guard):
        return source([
            "!(bind! &s (new-space))",
            "(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (cons $n $acc))))",
            "(= (len $l) (if (== $l ()) 0 (let (cons $h $t) $l (+ 1 (len $t)))))",
            "(= (pick (cons $h $t)) $h)",
            "(= (pick $l) (len $l))",
            "(= (loop $n $big $s) (if (== $n 0) $s "
            f"(loop (- $n 1) $big (+ $s (once {guard})))))",
            f"!(let $big (build {n} ()) (loop {n} $big 0))",
        ])

    # A body that may write but runs no transform has no firing to limit. A
    # body whose untaken branch runs one keeps its once in the compiled
    # tier, so each call's argument is admitted no more than once.
    shapes = {"pure": "(pick $big)",
              "writes": "(if (== $n -1) (add-atom &self (never)) (pick $big))",
              "may_fire": "(if (== $n -1) (transform &s (take (item $x)) 0) (pick $big))"}
    for shape, guard in shapes.items():
        for machine in ("0", "1"):
            work = []
            admitted = []
            for n in (300, 600):
                out, counts = measure(loop(n, guard), f"once_loop_{shape}_{n}", machine)
                if out != str(n):
                    raise AssertionError(f"once loop {shape}, n={n}, machine={machine}: {out!r}")
                work.append(sum(counts.values()))
                admitted.append(counts["admitted"])
            if work[1] > 2 * work[0] + 16 or admitted[1] > 2 * admitted[0] + 16:
                raise AssertionError(f"once loop {shape}, machine={machine}: work grew from "
                                     f"{work[0]} to {work[1]}, admitted nodes from {admitted[0]} "
                                     f"to {admitted[1]}, when the loop and its argument doubled")
    print("PASS: a once's later alternatives cost no work, and once loops over a large "
          "argument, a transform in an untaken branch included, do work linear in the loop")


def named_fixtures(binary):
    fixtures = {
        "rejected_prefix": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item a))
!(once (transform &s (, (read (item $x)) (take (item $x))) (got $x)))
!(remaining (collapse (match &s $x $x)))
""",
        "empty_producer": """!(bind! &s (new-space))
!(once (transform &s (take (missing $x)) (got $x)))
""",
        "empty_first_public_proposal": """!(bind! &e (new-space))
!(bind! &s (new-space))
!(add-atom &s (q a))
!(tf:propose &e ((take (p $x)) (take &s (q $y))) (pair $x $y))
""",
        "duplicates": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item a))
!(once (transform &s (take (item $x)) (got $x)))
!(remaining (size-atom (collapse (match &s $row $row))))
""",
        "captured_binding": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(let $who a (once (transform &s (take (item $who)) (got $who))))
!(remaining (collapse (match &s $x $x)))
""",
        "destination": """!(bind! &s (new-space))
!(bind! &d (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(once (transform &s (take (item $x)) (done $x) &d))
!(source-left (collapse (match &s $x $x)))
!(destination (collapse (match &d $x $x)))
""",
        "read_join": """!(bind! &s (new-space))
!(add-atom &s (p a))
!(add-atom &s (p b))
!(add-atom &s (q 1))
!(add-atom &s (q 2))
!(once (transform &s (, (read (p $x)) (read (q $y))) (pair $x $y)))
!(remaining (size-atom (collapse (match &s $row $row))))
""",
        "full_drain": """!(bind! &s (new-space))
!(add-atom &s (p a))
!(add-atom &s (p b))
!(add-atom &s (q 1))
!(add-atom &s (q 2))
!(all (collapse (transform &s (, (take (p $x)) (take (q $y))) (pair $x $y))))
!(remaining (size-atom (collapse (match &s $row $row))))
""",
        "open_rows": """!(bind! &s (new-space))
!(add-atom &s (item $stored))
!(add-atom &s (item a))
!(once (transform &s (take (item $x)) (got $x)))
!(remaining (collapse (match &s $x $x)))
""",
        "generation_let_barrier": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(= (choose) a)
!(once (transform &s (, (let $x (choose)) (take (item $x))) (got $x)))
!(remaining (collapse (match &s $x $x)))
""",
        "answerless_nested_firings": """!(bind! &s (new-space))
!(bind! &d (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(add-atom &d (job x))
!(add-atom &d (job y))
!(once (transform &s (take (item $x))
  (let $_ (once (transform &d (take (job $j)) (done $j))) (empty))))
!(source-left (collapse (match &s $x $x)))
!(nested-left (collapse (match &d $x $x)))
""",
        "later_insert": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(once (transform &s (take (item $x))
  (let $_ (add-atom &s (item c)) (empty))))
!(remaining (collapse (match &s $x $x)))
""",
        "later_delete": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(once (transform &s (take (item $x))
  (let $_ (remove-atom &s (item b)) (empty))))
!(remaining (collapse (match &s $x $x)))
""",
        "later_reinsert": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(once (transform &s (take (item $x))
  (let $_ (remove-atom &s (item b))
    (let $_ (add-atom &s (item b)) (empty)))))
!(remaining (collapse (match &s $x $x)))
""",
        "irrelevant_write": """!(bind! &s (new-space))
!(bind! &d (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(once (transform &s (take (item $x))
  (let $_ (add-atom &d (unrelated $x)) (empty))))
!(source-left (collapse (match &s $x $x)))
!(other (collapse (match &d $x $x)))
""",
        "function_destination": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(= (sink $x) (done $x))
!(once (transform &s (take (item $x)) (routed $x) sink))
!(remaining (collapse (match &s $x $x)))
""",
        "invalid_destination_after_take": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(once (transform &s (take (item $x)) (done $x) 42))
!(remaining (collapse (match &s $x $x)))
""",
        "dynamic_source_barrier": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(once (transform &s (, (let $space &s) (take $space (item $x))) (got $x)))
!(remaining (collapse (match &s $x $x)))
""",
        "source_once_barrier": """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(once (transform &s (once (take (item $x))) (got $x)))
!(remaining (collapse (match &s $x $x)))
""",
    }
    for name, text in fixtures.items():
        differential(binary, text, name)
    print(f"PASS: {len(fixtures)} named first-answer/effect/barrier fixtures agree with eager traversal")


def generated_corpus(binary, count):
    rng = random.Random(20261110)
    values = ["a", "b", "0", "1"]
    for case in range(count):
        lines = ["!(bind! &s (new-space))", "!(bind! &d (new-space))",
                 "(= (wrapped-effect $v) (let $_ (add-atom &d (wrapped $v)) (empty)))",
                 "(= (wrapped-value $v) (let $_ (add-atom &d (seen $v)) (wrapped-result $v)))"]
        predicates = ["p", "q", "r"]
        for predicate in predicates:
            for _ in range(rng.randrange(0, 5)):
                value = rng.choice(values + ["$stored"])
                lines.append(f"!(add-atom &s ({predicate} {value}))")
                if rng.randrange(5) == 0:
                    lines.append(f"!(add-atom &s ({predicate} {value}))")
        conjuncts = []
        legs = rng.randrange(1, 4)
        for leg in range(legs):
            predicate = predicates[leg]
            mode = rng.choice(["read", "take"])
            variable = ["$x", "$y", "$z"][leg]
            if leg and rng.randrange(4) == 0:
                variable = "$x"
            conjuncts.append(f"({mode} ({predicate} {variable}))")
        query = conjuncts[0] if len(conjuncts) == 1 else "(, " + " ".join(conjuncts) + ")"
        template = rng.choice(["got", "pair", "unknown-result",
                               "wrapped-effect", "wrapped-value"])
        destination = " &d" if rng.randrange(4) == 0 else ""
        consumer = rng.choice(["once", "once", "once", "collapse-once"])
        expression = f"(once (transform &s {query} ({template} $x){destination}))"
        if consumer == "collapse-once":
            expression = f"(collapse {expression})"
        lines.append(f"!{expression}")
        lines.append("!(remaining (collapse (match &s $row $row)))")
        lines.append("!(written (collapse (match &d $row $row)))")
        differential(binary, source(lines), f"generated_{case}")
    print(f"PASS: {count} seeded eligible-fragment programs preserve the complete observable transcript")


def early_close(binary):
    lines = ["!(bind! &s (new-space))"]
    lines += [f"!(add-atom &s (left {i}))" for i in range(200)]
    lines += [f"!(add-atom &s (right {i}))" for i in range(200)]
    lines.append("!(reset-runtime-stats!)")
    lines += ["!(once (transform &s (, (read (left $x)) (read (right $y))) "
              "(claimed $x $y)))" for _ in range(100)]
    proc, counters = run(binary, source(lines), "early_close", stats=True)
    if proc.returncode != 0 or proc.stdout.splitlines()[-1] != "(claimed 0 0)":
        raise AssertionError(f"early-close answers failed: {proc.stdout!r}; {proc.stderr!r}")
    for key in ("transform-cursor-open", "transform-cursor-close",
                "transform-cursor-candidate"):
        if counters.get(key) != 100:
            raise AssertionError(f"{key}: expected 100, got {counters.get(key)}")
    if counters.get("transform-cursor-scan-row") != 200:
        raise AssertionError("early-close scans did not stop at the two-row prefix")
    print("PASS: 100 early closes balance ownership and retain only the two-row scan prefix")


def late_cursor_decline(binary):
    proc, _ = run(binary, """!(bind! &s (new-space))
!(bind! &log (new-space))
!(add-atom &s (p a))
!(add-atom &s (p b))
!(add-atom &s (q 1))
!(once (transform &s (, (read (p $x)) (read (q $y)))
  (let $_ (add-atom &log (seen $x $y)) (empty))))
!(written (collapse (match &log $row $row)))
""", "late_cursor_decline", extra_env={
        "CETTA_TEST_TRANSFORM_CURSOR_CLONE_FAIL_AT": "3"})
    if proc.returncode != 0 or proc.stdout.splitlines()[-1] != \
            "(written ((seen a 1)))":
        raise AssertionError(
            f"late cursor decline replayed or lost earlier effects: "
            f"{proc.stdout!r}; {proc.stderr!r}")
    if proc.stderr.count("transform-cursor-test-clone-refusal 3") != 1:
        raise AssertionError(f"the focused clone refusal did not fire exactly once: {proc.stderr!r}")
    print("PASS: a late cursor decline preserves one answerless effect prefix without fallback replay")

    proc, _ = run(binary, """!(bind! &s (new-space))
!(add-atom &s (p a))
!(add-atom &s (p b))
!(add-atom &s (q 1))
!(add-atom &s (noise 0))
!(add-atom &s (noise 1))
!(add-atom &s (noise 2))
!(add-atom &s (noise 3))
!(add-atom &s (noise 4))
!(catch (collapse (transform &s (, (take (p $x)) (read (q $y))) (,))))
!(remaining (collapse (match &s (p $row) $row)))
""", "zero_output_take_decline", extra_env={
        "CETTA_TEST_TRANSFORM_CURSOR_CLONE_FAIL_AT": "3"})
    if proc.returncode != 0 or proc.stdout.splitlines()[-1] != \
            "(remaining (b))":
        raise AssertionError(f"an answerless take was replayed: {proc.stdout!r}; {proc.stderr!r}")
    if proc.stderr.count("transform-cursor-test-clone-refusal 3") != 1:
        raise AssertionError(f"the answerless-take refusal did not fire: {proc.stderr!r}")
    print("PASS: a failed collected continuation cannot replay a take with zero publications")


def main():
    if len(sys.argv) not in (2, 3, 4):
        sys.exit("usage: check_transform_stream_control.py CETTA_BINARY [PROGRAMS] [FAULT_BINARY]")
    binary = os.path.abspath(sys.argv[1])
    count = int(sys.argv[2]) if len(sys.argv) >= 3 else 24
    acceptance(binary)
    first_firing_demand(binary)
    first_firing_reference(binary)
    compiled_once_demand(binary)
    once_demand_counters(binary)
    named_fixtures(binary)
    generated_corpus(binary, count)
    early_close(binary)
    if len(sys.argv) == 4:
        late_cursor_decline(os.path.abspath(sys.argv[3]))


if __name__ == "__main__":
    main()
