#!/usr/bin/env python3
"""Old-versus-new ordinary-language performance qualification.

The candidate and historical executable run the same generated ordinary MeTTa
programs.  Every output is derived here, independently of either executable.
There is no positive regression allowance and no offset between workloads.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass

EVENTS = ("instructions:u", "cycles:u", "branches:u", "branch-misses:u")


@dataclass(frozen=True)
class Case:
    name: str
    dialect: str
    profile: str
    source: str
    expected: str
    # Extra randomized-layout repetitions for measured address-sensitive cases.
    # Fixed-layout execution always uses one repetition.
    layout_repeats: int = 1


def unit(dialect):
    return "true" if dialect == "petta" else "[()]"


def answer(dialect, value):
    return value if dialect == "petta" else f"[{value}]"


def mutation_program(dialect, operations):
    lines = ["!(bind! &s (new-space))"]
    expected = [] if dialect == "petta" else [unit(dialect)]
    removals = {}
    for operation in operations:
        lines.append(f"!({operation})")
        if operation.startswith("remove-atom") and dialect == "petta":
            seen = removals.get(operation, 0)
            removals[operation] = seen + 1
            expected.append("false" if "missing" in operation or "absent" in operation or seen
                            else "true")
        else:
            expected.append(unit(dialect))
    return "\n".join(lines) + "\n", "\n".join(expected) + "\n"


def mutation_case(name, dialect, profile, operations):
    source, expected = mutation_program(dialect, operations)
    # The default 6 rounds × 2 seeds × 8 gives 96 balanced pairs per batch:
    # measured layout variance requires that scale for these small HE cases.
    layout_repeats = 8 if dialect == "he" and name in {
        "remove-absent", "duplicate-open"} else 1
    return Case(f"{name}-{dialect}-{profile}", dialect, profile, source, expected,
                layout_repeats)


def add_program(dialect, rows, final):
    lines = ["!(bind! &s (new-space))"]
    expected = [] if dialect == "petta" else [unit(dialect)]
    for row in rows:
        lines.append(f"!(add-atom &s {row})")
        expected.append(unit(dialect))
    lines.append(f"!{final}")
    return "\n".join(lines) + "\n", expected


def finish_query(dialect, expression):
    if dialect == "petta":
        return f"(msort (collapse {expression}))"
    return f"(sort-atom (collapse {expression}))"


def query_case(name, dialect, profile, rows, final, result):
    source, expected = add_program(dialect, rows, final if isinstance(final, str) else final[0])
    finals = [final] if isinstance(final, str) else list(final)
    results = [result] if isinstance(result, str) else list(result)
    assert len(finals) == len(results)
    for extra_final in finals[1:]:
        source += f"!{extra_final}\n"
    expected.extend(results)
    return Case(f"{name}-{dialect}-{profile}", dialect, profile, source,
                "\n".join(expected) + "\n")


def recursion_source(dialect, n, effectful=False):
    prefix = []
    if effectful:
        prefix.append("!(bind! &fold-out (new-space))")
    prefix += [
        "(= (ordinary-sum $n) (if (<= $n 0) 0 (+ $n (ordinary-sum (- $n 1)))))",
        "(= (ordinary-fold-step $x $acc) " +
        ("(let $_ (add-atom &fold-out (fold-seen $x)) " if effectful else "") +
        "(+ $acc $x)" + (")" if effectful else "") + ")",
    ]
    return prefix


def functional_case(name, dialect, profile, n, effectful=False):
    values = list(range(1, n + 1))
    source = recursion_source(dialect, n, effectful)
    expected = []
    if effectful and dialect == "he":
        expected.append(unit(dialect))
    source.append(f"!(ordinary-sum {n})")
    expected.append(answer(dialect, str(n * (n + 1) // 2)))
    if dialect == "he" and profile == "default":
        source.append("!(ordinary-sum 0)")
        expected.append("[0]")
        return Case(f"{name}-{dialect}-{profile}", dialect, profile,
                    "\n".join(source) + "\n", "\n".join(expected) + "\n")
    literal = "(" + " ".join(map(str, values)) + ")"
    if dialect == "petta":
        source.append(f"!(foldl ordinary-fold-step {literal} 0)")
        fold_result = str(sum(values))
    else:
        step = ("(chain (ordinary-fold-step $b $a) $r $r)" if effectful
                else "(eval (ordinary-fold-step $b $a))")
        source.append("!(foldl-atom-in-space " + literal +
                      " 0 $a $b " + step + " (context-space))")
        fold_result = str(sum(values))
    if effectful:
        source.append("!" + finish_query(dialect,
            "(match &fold-out $x $x)"))
        expected.append(answer(dialect,
            "(" + " ".join(f"(fold-seen {value})" for value in values) + ")"))
    else:
        source.append("!(ordinary-sum 0)")
        expected.append(answer(dialect, "0"))
    expected.insert(-1, answer(dialect, fold_result))
    return Case(f"{name}-{dialect}-{profile}", dialect, profile,
                "\n".join(source) + "\n", "\n".join(expected) + "\n")


def allocation_case(dialect, profile, n):
    source = """(= (ordinary-allocate $n)
   (if (<= $n 0) done
       (let $s (new-space)
           (let $_ (add-atom $s (held $n (f $n)))
               (ordinary-allocate (- $n 1))))))
"""
    source += f"!(ordinary-allocate {n})\n"
    return Case(f"allocation-reclamation-{dialect}-{profile}", dialect, profile,
                source, "done\n" if dialect == "petta" else "[done]\n")


def cases(scale):
    n = 12000 * scale
    small = 600 * scale
    folds = 120 * scale
    for dialect in ("he", "petta"):
        profiles = ("default", "extended")
        for profile in profiles:
            yield mutation_case("add-only", dialect, profile,
                                [f"add-atom &s (row {i})" for i in range(n)])
            yield mutation_case("add-remove-present", dialect, profile,
                                [f"add-atom &s (row {i})" for i in range(n)] +
                                [f"remove-atom &s (row {i})" for i in range(n - 1, -1, -1)])
            yield mutation_case("remove-absent", dialect, profile,
                                [f"add-atom &s (row {i})" for i in range(small)] +
                                [f"remove-atom &s (missing {i})" for i in range(small)])
        for profile in ("extended",):
            yield mutation_case("duplicate-open", dialect, profile,
                                ["add-atom &s (edge $x 1)" for _ in range(small)] +
                                ["remove-atom &s (edge $x 1)" for _ in range(small)])
            rows = [f"(row {i} {i % 17})" for i in range(small)]
            yield query_case("match-selective", dialect, profile, rows,
                             finish_query(dialect, "(match &s (row 17 $v) $v)"),
                             "(0)" if dialect == "petta" else "[(0)]")
            yield query_case("match-first", dialect, profile, rows,
                             "(once (match &s (row 17 $v) $v))",
                             answer(dialect, "0"))
            yield query_case("match-broad-collection", dialect, profile, rows,
                             finish_query(dialect, "(match &s (row $k 3) $k)"),
                             "(" + " ".join(str(i) for i in range(3, small, 17)) + ")"
                             if dialect == "petta" else
                             "[(" + " ".join(str(i) for i in range(3, small, 17)) + ")]")
            joined = " ".join(f"({i} {i % 11})" for i in range(small))
            yield query_case("join", dialect, profile,
                             [f"(link {i} {i + 1})" for i in range(small)] +
                             [f"(right {i + 1} {i % 11})" for i in range(small)],
                             finish_query(dialect,
                                 "(match &s (, (link $x $y) (right $y $z)) ($x $z))"),
                             f"({joined})" if dialect == "petta" else f"[({joined})]")
            effect_results = " ".join("true" if dialect == "petta" else "()"
                                      for _ in range(small))
            yield query_case("collection-effectful", dialect, profile,
                             [f"(item {i})" for i in range(small)],
                             [finish_query(dialect,
                                 "(match &s (item $x) (add-atom &s (seen $x)))"),
                              finish_query(dialect, "(match &s (seen $x) $x)")],
                             [f"({effect_results})" if dialect == "petta"
                              else f"[({effect_results})]",
                              answer(dialect, "(" + " ".join(str(i) for i in range(small)) + ")")])
            yield functional_case("recursion-fold", dialect, profile, folds)
            yield functional_case("effectful-fold", dialect, profile, folds, True)
            yield allocation_case(dialect, profile, folds)


def correctness_cases():
    for dialect in ("he", "petta"):
        for profile in ("default", "extended"):
            yield mutation_case("control-mixed", dialect, profile, [
                "add-atom &s (row 1)",
                "add-atom &s (row 1)",
                "remove-atom &s (absent 1)",
                "remove-atom &s (row 1)",
                "remove-atom &s (row 1)",
                "add-atom &s (edge $x 1)",
                "remove-atom &s (edge $x 1)",
            ])
            yield query_case("control-selective", dialect, profile,
                             ["(row 17 0)", "(row 18 1)"],
                             finish_query(dialect, "(match &s (row 17 $v) $v)"),
                             answer(dialect, "(0)"))
            yield query_case("control-first", dialect, profile,
                             ["(row 17 0)", "(row 18 1)"],
                             "(once (match &s (row 17 $v) $v))",
                             "[(once 0)]" if dialect == "he" and profile == "default"
                             else answer(dialect, "0"))
        yield query_case("control-join", dialect, "extended",
                         ["(link 0 1)", "(right 1 2)"],
                         finish_query(dialect,
                             "(match &s (, (link $x $y) (right $y $z)) ($x $z))"),
                         answer(dialect, "((0 2))"))
        yield query_case("control-collection-effectful", dialect, "extended",
                         ["(item 0)", "(item 1)"],
                         [finish_query(dialect,
                             "(match &s (item $x) (add-atom &s (seen $x)))"),
                          finish_query(dialect, "(match &s (seen $x) $x)")],
                         [answer(dialect, "(true true)" if dialect == "petta" else "(() ())"),
                          answer(dialect, "(0 1)")])
        yield functional_case("control-recursion-fold", dialect, "extended", 3)
        yield functional_case("control-effectful-fold", dialect, "extended", 3, True)
        allocation = allocation_case(dialect, "extended", 3)
        yield Case(f"control-allocation-reclamation-{dialect}-extended",
                   allocation.dialect, allocation.profile,
                   allocation.source, allocation.expected)


def command(binary, case, source):
    result = [str(binary), "--lang", case.dialect]
    if case.profile != "default":
        result += ["--profile", case.profile]
    result.append(str(source))
    return result


def parse_perf(stderr):
    counters = {}
    for line in stderr.splitlines():
        fields = line.split(";")
        if len(fields) > 2 and fields[2] in EVENTS:
            try:
                counters[fields[2]] = int(fields[0])
            except ValueError:
                pass
    return counters


def execute(binary, case, source, seed, perf=True, fixed_layout=False):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("CETTA_")}
    env["CETTA_PLACEMENT_KEY"] = str(seed)
    argv = command(binary, case, source)
    if perf:
        argv = ["perf", "stat", "--no-big-num", "-x", ";", "-e",
                ",".join(EVENTS), "--"] + argv
    if fixed_layout:
        argv = ["setarch", os.uname().machine, "-R"] + argv
    started = time.monotonic()
    run = subprocess.run(argv, capture_output=True, text=True, env=env, timeout=300)
    elapsed = time.monotonic() - started
    return {"returncode": run.returncode, "stdout": run.stdout,
            "stderr": run.stderr, "seconds": elapsed,
            "counters": parse_perf(run.stderr) if perf else {}}


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def compare_samples(case, baseline, candidate):
    """Validate one matched pair; timing decisions use the whole balanced batch."""
    if baseline["returncode"] != 0 or candidate["returncode"] != 0:
        return "fail", "nonzero execution"
    if digest(baseline["stdout"]) != digest(case.expected):
        return "fail", "baseline output differs from the independent expectation"
    if digest(candidate["stdout"]) != digest(case.expected):
        return "fail", "candidate output differs from the independent expectation"
    for event in EVENTS:
        if not isinstance(baseline["counters"].get(event), int):
            return "unavailable", f"baseline missing {event}"
        if not isinstance(candidate["counters"].get(event), int):
            return "unavailable", f"candidate missing {event}"
    return "pass", "outputs and counters available"


def median_interval(deltas, replications=10000):
    """A fixed-seed percentile bootstrap interval for the median paired delta."""
    if not deltas:
        return None
    if len(deltas) == 1:
        return deltas[0], deltas[0]
    rng = random.Random(20261110)
    n = len(deltas)
    medians = sorted(statistics.median(deltas[rng.randrange(n)] for _ in range(n))
                     for _ in range(replications))
    return medians[int(.025 * replications)], medians[min(int(.975 * replications),
                                                         replications - 1)]


# How many sample batches a workload gets before its instruction count stays
# undecided.
MAX_BATCHES = 4


def interval(deltas):
    median = statistics.median(deltas)
    lower, upper = median_interval(deltas)
    return {"median": median, "lower_95": lower, "upper_95": upper,
            "samples": len(deltas)}


def instruction_verdict(deltas):
    """Instruction counts decide, and exactly.

    The interval of the median paired delta at or below zero passes; wholly
    above zero fails; anything else is undecided, never a pass.
    """
    if not deltas:
        return "unavailable", None
    detail = interval(deltas)
    verdict = ("pass" if detail["upper_95"] <= 0 else
               "fail" if detail["lower_95"] > 0 else "inconclusive")
    return verdict, detail


# Cycles and wall time of different binaries differ by where their code and
# data land, whatever the source says.  A time loss is one measured beyond
# this share of the baseline's own median, and for wall time this many
# seconds; instruction counts take no such allowance.
TIME_RESOLUTION_SHARE = 0.02
WALL_RESOLUTION_SECONDS = 0.001


def time_verdict(deltas, baseline, floor=0.0):
    """Cycles or wall time fail on a loss measured beyond the resolution:
    an interval wholly above it."""
    if not deltas:
        return "unavailable", None
    detail = interval(deltas)
    detail["resolution"] = max(floor,
                               TIME_RESOLUTION_SHARE * statistics.median(baseline))
    return ("fail" if detail["lower_95"] > detail["resolution"] else "pass"), detail


def qualify_pairs(case, pairs):
    deltas = {"instructions:u": [], "cycles:u": [], "seconds": []}
    before = {"cycles:u": [], "seconds": []}
    pair_verdicts = []
    for baseline, candidate in pairs:
        verdict, reason = compare_samples(case, baseline, candidate)
        pair_verdicts.append((verdict, reason))
        if verdict == "pass":
            deltas["seconds"].append(candidate["seconds"] - baseline["seconds"])
            before["seconds"].append(baseline["seconds"])
            before["cycles:u"].append(baseline["counters"]["cycles:u"])
            for event in ("instructions:u", "cycles:u"):
                deltas[event].append(candidate["counters"][event] -
                                     baseline["counters"][event])
    if any(verdict == "fail" for verdict, _ in pair_verdicts):
        return "fail", {"pair_validation": pair_verdicts}
    if any(verdict == "unavailable" for verdict, _ in pair_verdicts):
        return "unavailable", {"pair_validation": pair_verdicts}
    instructions, instruction_detail = instruction_verdict(deltas["instructions:u"])
    cycles, cycle_detail = time_verdict(deltas["cycles:u"], before["cycles:u"])
    seconds, seconds_detail = time_verdict(deltas["seconds"], before["seconds"],
                                           WALL_RESOLUTION_SECONDS)
    evidence = {"instructions:u": instruction_detail, "cycles:u": cycle_detail,
                "seconds": seconds_detail}
    overall = "fail" if "fail" in (instructions, cycles, seconds) else instructions
    return overall, evidence


def self_test():
    case = Case("control", "he", "extended", "", "expected\n")
    def sample(output="expected\n", instructions=100, cycles=100, seconds=1.0):
        counters = {event: instructions for event in EVENTS}
        counters["cycles:u"] = cycles if instructions is not None else None
        return {"returncode": 0, "stdout": output, "seconds": seconds,
                "counters": counters}
    batches = [
        ("fewer instructions", [(99, 99, .9)], "pass"),
        ("exact equality", [(100, 100, 1.0)], "pass"),
        ("instruction rise", [(101, 99, .9)], "fail"),
        ("cycle loss with fewer instructions", [(99, 105, .9)], "fail"),
        ("cycle difference within the resolution", [(99, 101, .9)], "pass"),
        ("wall-time loss beyond the resolution", [(99, 99, 1.5)], "fail"),
        ("wall-time difference within the resolution", [(99, 99, 1.01)], "pass"),
        ("instruction noise around zero", [(99, 100, 1.0), (101, 100, 1.0), (100, 100, 1.0)],
         "inconclusive"),
        ("instruction rise within noise", [(101, 100, 1.0), (103, 100, 1.0), (99, 100, 1.0)],
         "inconclusive"),
        ("cycle noise around zero", [(99, 99, 1.0), (99, 101, 1.0), (99, 100, 1.0)], "pass"),
        ("cycle loss measured", [(99, 105, 1.0), (99, 106, 1.0), (99, 104, 1.0)], "fail"),
    ]
    for name, candidates, expected in batches:
        actual = qualify_pairs(case, [
            (sample(), sample(instructions=count, cycles=cycles, seconds=seconds))
            for count, cycles, seconds in candidates])[0]
        if actual != expected:
            print(f"FAIL: performance-gate self-test {name}: {actual}, expected {expected}")
            return 1
        print(f"PASS: performance-gate self-test {name}: {actual}")
    controls = [
        ("output mismatch", sample(), sample(output="wrong\n", instructions=99), "fail"),
        ("unavailable counters", sample(), sample(instructions=None), "unavailable"),
    ]
    for name, baseline, candidate, expected in controls:
        actual = qualify_pairs(case, [(baseline, candidate)])[0]
        if actual != expected:
            print(f"FAIL: performance-gate self-test {name}: {actual}, expected {expected}")
            return 1
        print(f"PASS: performance-gate self-test {name}: {actual}")
    return 0


def own_make_environment():
    """This environment without a calling make's variables, which reach it
    through MAKEFLAGS and as exported variables: a baseline revision builds
    in its own default configuration."""
    env = dict(os.environ)
    overrides = env.get("MAKEFLAGS", "").partition(" -- ")[2]
    for word in re.split(r"(?<!\\) ", overrides):
        name = re.match(r"([A-Za-z_][A-Za-z0-9_]*)=", word)
        if name:
            env.pop(name.group(1), None)
    for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "MAKELEVEL"):
        env.pop(key, None)
    return env


def build_baseline(root, revision, work):
    full = subprocess.run(["git", "rev-parse", revision], cwd=root,
                          capture_output=True, text=True, check=True).stdout.strip()
    directory = work / f"source-{full}"
    binary = directory / "cetta"
    stamp = directory / ".ordinary-baseline-built"
    if binary.is_file() and stamp.is_file() and stamp.read_text() == full + "\n":
        return binary
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    archive = work / f"{full}.tar"
    subprocess.run(["git", "archive", "--format=tar", f"--output={archive}", full],
                   cwd=root, check=True)
    subprocess.run(["tar", "-xf", archive, "-C", directory], check=True)
    archive.unlink()
    subprocess.run(["make", "-s", "BUILD=python", "ENABLE_RUNTIME_STATS=0",
                    "BIN=cetta", "-j3"], cwd=directory, check=True,
                   env=own_make_environment())
    stamp.write_text(full + "\n")
    return binary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--baseline-rev", default="d938ce41")
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--work", type=Path, required=False)
    parser.add_argument("--rounds", type=int, default=6,
                        help="matched pairs per seed; an even count balances execution order")
    parser.add_argument("--seeds", type=int, nargs="+", default=[173, 911])
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--only", action="append")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--correctness-only", action="store_true")
    parser.add_argument("--fixed-layout", action="store_true")
    parser.add_argument("--prefer-fixed-layout", action="store_true",
                        help="fixed layout where the machine supports it")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.correctness_only:
        if not args.candidate:
            parser.error("--candidate is required with --correctness-only")
        failures = 0
        with tempfile.TemporaryDirectory(prefix="ordinary-controls-") as temporary:
            for case in correctness_cases():
                source = Path(temporary) / f"{case.name}.metta"
                source.write_text(case.source)
                run = execute(args.candidate.resolve(), case, source, 0, perf=False)
                if run["returncode"] or run["stdout"] != case.expected:
                    failures += 1
                    print(f"FAIL: {case.name}: rc={run['returncode']}\n"
                          f"expected:\n{case.expected}actual:\n{run['stdout']}"
                          f"stderr:\n{run['stderr']}")
                else:
                    print(f"PASS: {case.name}")
        return 1 if failures else 0
    if not args.candidate or not args.work:
        parser.error("--candidate and --work are required outside --self-test")
    if args.rounds < 2 or args.rounds % 2 or args.scale < 1:
        parser.error("--rounds must be positive and even (for balanced order), and --scale positive")
    if shutil.which("perf") is None:
        print("UNAVAILABLE: perf is required; no performance result is inferred")
        return 2
    if args.fixed_layout or args.prefer_fixed_layout:
        probe = subprocess.run(["setarch", os.uname().machine, "-R", "/bin/true"],
                               capture_output=True) if shutil.which("setarch") else None
        supported = probe is not None and not probe.returncode
        if args.fixed_layout and not supported:
            print("UNAVAILABLE: fixed-layout execution was requested but is unsupported")
            return 2
        args.fixed_layout = supported
    root = args.root.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    candidate = args.candidate.resolve()
    try:
        baseline = args.baseline.resolve() if args.baseline else \
            build_baseline(root, args.baseline_rev, work)
        if not baseline.is_file():
            raise FileNotFoundError(f"baseline executable is not a file: {baseline}")
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"UNAVAILABLE: ordinary-performance baseline is unusable: {error}")
        return 2
    report = {"candidate": str(candidate), "baseline": str(baseline),
              "candidate_sha256": hashlib.sha256(candidate.read_bytes()).hexdigest(),
              "baseline_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
              "rounds": args.rounds, "seeds": args.seeds,
              "max_batches": MAX_BATCHES,
              "fixed_layout": args.fixed_layout,
              "method": "alternating matched pairs; median paired delta; fixed-seed percentile bootstrap; instructions: at or below zero passes, above zero fails, otherwise resampled then undecided; cycles: a measured loss fails; a failure must repeat on fresh samples; wall time reported",
              "cases": {}}
    overall = "pass"
    selected = [case for case in cases(args.scale)
                if not args.only or any(word in case.name for word in args.only)]
    if not selected:
        parser.error("--only selected no workloads; refusing a vacuous pass")
    with tempfile.TemporaryDirectory(prefix="workloads-", dir=work) as temporary:
        for case in selected:
            source = Path(temporary) / f"{case.name}.metta"
            source.write_text(case.source)
            rows = []

            def batch():
                """One balanced batch of matched pairs, or the reason none was taken."""
                pairs = []
                for seed in args.seeds:
                    # Warm and validate both executables before every matched batch.
                    for label, binary in (("baseline", baseline), ("candidate", candidate)):
                        warm = execute(binary, case, source, seed, perf=False,
                                       fixed_layout=args.fixed_layout)
                        if warm["returncode"] or warm["stdout"] != case.expected:
                            return None, f"{label} warm-up failed independent output validation"
                    rounds = args.rounds * (1 if args.fixed_layout else case.layout_repeats)
                    for turn in range(rounds):
                        order = (("baseline", baseline), ("candidate", candidate))
                        if turn % 2:
                            order = tuple(reversed(order))
                        pair = {}
                        for label, binary in order:
                            pair[label] = execute(binary, case, source, seed,
                                                  fixed_layout=args.fixed_layout)
                        pairs.append((pair["baseline"], pair["candidate"]))
                        verdict, reason = compare_samples(case, pair["baseline"], pair["candidate"])
                        rows.append({"seed": seed, "turn": turn, "validation": verdict,
                                     "validation_reason": reason,
                                     "baseline_seconds": pair["baseline"]["seconds"],
                                     "candidate_seconds": pair["candidate"]["seconds"],
                                     "baseline_stdout_sha256": digest(pair["baseline"]["stdout"]),
                                     "candidate_stdout_sha256": digest(pair["candidate"]["stdout"]),
                                     "baseline": pair["baseline"]["counters"],
                                     "candidate": pair["candidate"]["counters"]})
                return pairs, None

            def decide():
                """Sample until the instruction count is decided, at most MAX_BATCHES."""
                pairs = []
                for _ in range(MAX_BATCHES):
                    more, failure = batch()
                    if failure:
                        return "fail", {"reason": failure}
                    pairs += more
                    verdict, evidence = qualify_pairs(case, pairs)
                    if verdict != "inconclusive":
                        break
                return verdict, evidence

            verdict, evidence = decide()
            if verdict == "fail" and "reason" not in evidence and \
                    "pair_validation" not in evidence:
                # Thirty workloads and two counters give chance failures: a
                # measured loss counts when fresh samples show it again.
                first = evidence
                verdict, evidence = decide()
                evidence["unrepeated_failure" if verdict != "fail" else "first_failure"] = first
            if verdict == "fail":
                overall = "fail"
            elif verdict != "pass" and overall == "pass":
                overall = verdict
            report["cases"][case.name] = {"source_sha256": digest(case.source),
                                          "expected_sha256": digest(case.expected),
                                          "layout_repeats": 1 if args.fixed_layout else case.layout_repeats,
                                          "verdict": verdict, "evidence": evidence,
                                          "runs": rows}
            (work / "ordinary-performance.json").write_text(json.dumps(report, indent=2) + "\n")
            def shown(metric, form):
                detail = evidence.get(metric)
                return (f"{format(detail['median'], form)} [{format(detail['lower_95'], form)}, "
                        f"{format(detail['upper_95'], form)}]" if detail else "unavailable")
            print(f"{verdict.upper()}: {case.name}: instructions {shown('instructions:u', '+')}, "
                  f"cycles {shown('cycles:u', '+')}, seconds {shown('seconds', '+.9f')}",
                  flush=True)
    print(f"{overall.upper()}: ordinary-performance qualification; raw samples: "
          f"{work / 'ordinary-performance.json'}")
    return 1 if overall == "fail" else 2 if overall != "pass" else 0


if __name__ == "__main__":
    sys.exit(main())
