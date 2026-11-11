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
    # LP64 layout audit: compared with the reference implementation's 3896
    # bytes, our cursor embeds a 16-byte larger Arena and each of its two
    # levels has 304 additional bytes of occurrence-cursor ownership state.
    # This is fixed storage, independent of the number of candidate rows.
    expected_owned = 3896 + 16 + 2 * 304
    owned_peaks = []
    accounted_peaks = []
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
        expected_peaks = {
            200: (expected_owned, expected_owned + 1704),
            400: (expected_owned, expected_owned + 3304),
        }
        if (owned, peak) != expected_peaks[n]:
            raise AssertionError(
                f"cursor-owned vector accounting changed for n={n}: "
                f"expected {expected_peaks[n]}, got {(owned, peak)}")
        owned_peaks.append(owned)
        accounted_peaks.append(peak)
        print(f"PASS: once over {n * n} answers: one candidate, two scanned rows, "
              f"owned peak {owned} bytes, entry-inclusive accounted peak {peak} bytes")
    if owned_peaks[0] != owned_peaks[1]:
        raise AssertionError(f"cursor-owned peak grew from n to 2n: {owned_peaks}")
    if accounted_peaks[1] <= accounted_peaks[0]:
        raise AssertionError("the taking case did not expose its detached entry-row retention")

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
    if (owned, accounted) != (expected_owned, expected_owned):
        raise AssertionError(
            f"zero-proposal high-water accounting changed: expected {(expected_owned, expected_owned)}, "
            f"got {(owned, accounted)}")
    print(f"PASS: zero proposals retain the failed-match owned peak {owned} bytes "
          f"and entry-inclusive peak {accounted} bytes")

    effectful_resume = """!(bind! &s (new-space))
!(add-atom &s (item a))
!(add-atom &s (item b))
!(reset-runtime-stats!)
!(once (transform &s (take (item $x))
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
            "(written ((seen (quote a) (quote 1))))":
        raise AssertionError(
            f"late cursor decline replayed or lost earlier effects: "
            f"{proc.stdout!r}; {proc.stderr!r}")
    if proc.stderr.count("transform-cursor-test-clone-refusal 3") != 1:
        raise AssertionError(f"the focused clone refusal did not fire exactly once: {proc.stderr!r}")
    print("PASS: a late cursor decline preserves one answerless effect prefix without fallback replay")


def main():
    if len(sys.argv) not in (2, 3, 4):
        sys.exit("usage: check_transform_stream_control.py CETTA_BINARY [PROGRAMS] [FAULT_BINARY]")
    binary = os.path.abspath(sys.argv[1])
    count = int(sys.argv[2]) if len(sys.argv) >= 3 else 24
    acceptance(binary)
    named_fixtures(binary)
    generated_corpus(binary, count)
    early_close(binary)
    if len(sys.argv) == 4:
        late_cursor_decline(os.path.abspath(sys.argv[3]))


if __name__ == "__main__":
    main()
