#!/usr/bin/env python3
"""Bounded cost-rho causal prefixes, checked against their recorded outputs.

Each case's sequential causal prefix must print exactly as its fixture line
records; the same prefixes must come out of a four-worker run; and on terms
with competing redexes, the one-step outcomes four workers reach must be
exactly the exhaustive step frontier.
"""

from __future__ import annotations

import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from rhocalc_cost_wire import (
    A,
    AA,
    AB,
    B,
    BASE,
    COLLISION_LEFT,
    COLLISION_RIGHT,
    DELIMITER_PAIR,
    PAY,
    SEAL,
    UTF8_PAIR,
    UTF8_SINGLE,
    WRONG,
    Wire,
    bvar,
    drop,
    parse_cetta_prefix,
    parse_cetta_steps,
    proc_nil,
    purse,
    recv,
    render_signature,
    render_term,
    send,
    signature,
    signed,
    split_redex,
    term_nil,
    term_par,
    whole_redex,
    wire_node,
)
from rhocalc_cost_wire import node


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/rhocalc_cost_run/causal_prefix_cases.expected"


@dataclass(frozen=True)
class Case:
    name: str
    fuel: int
    term: Wire
    search_fuel: int = 10_000


@dataclass(frozen=True)
class BranchCase:
    name: str
    term: Wire


def make_cases() -> list[Case]:
    nested_receiver_body = signed(
        recv(BASE, term_par([drop(bvar(0)), drop(bvar(1))])),
        SEAL,
    )
    cases: list[Case] = [
        Case("whole-single", 1, term_par([whole_redex(PAY, A), purse(PAY, [A])])),
        Case("whole-compound-split", 1,
             term_par([whole_redex(PAY, AB), purse(PAY, [A]), purse(PAY, [B])])),
        Case("whole-compound-combined", 1,
             term_par([whole_redex(PAY, AB), purse(PAY, [AB])])),
        Case("split-endpoints-split", 1,
             term_par([*split_redex(PAY, A, B), purse(PAY, [A]), purse(PAY, [B])])),
        Case("split-endpoints-combined", 1,
             term_par([*split_redex(PAY, A, B), purse(PAY, [AB])])),
        Case("dequotation-through-comm", 1,
             term_par([whole_redex(PAY, A, body=drop(bvar(0))), purse(PAY, [A])])),
        Case("nested-receiver-depth-shift", 1,
             term_par([
                 whole_redex(PAY, A, body=nested_receiver_body),
                 purse(PAY, [A]),
             ])),
        Case("quote-drop-nearness", 1,
             term_par([
                 whole_redex(PAY, A),
                 purse(node("quote", [drop(PAY)]), [A]),
             ])),
        Case("quoted-alpha-syntax", 1,
             term_par([
                 whole_redex(
                     node("quote", [signed(recv(BASE, drop(bvar(0))), SEAL)]),
                     A,
                 ),
                 purse(
                     node("quote", [signed(recv(BASE, drop(bvar(0))), SEAL)]),
                     [A],
                 ),
             ])),
        Case("quiescent-zero-fuel", 0, term_nil()),
        Case("live-zero-fuel", 0,
             term_par([whole_redex(PAY, A), purse(PAY, [A])])),
        Case("hard-no-cover-search-exhausted", 1,
             term_par([whole_redex(PAY, A)]), 1),
        Case("two-independent-locations", 2,
             term_par([
                 whole_redex(PAY, A), purse(PAY, [A]),
                 whole_redex(WRONG, B), purse(WRONG, [B]),
             ])),
        Case("repeated-equal-firings", 2,
             term_par([
                 whole_redex(PAY, A), whole_redex(PAY, A),
                 purse(PAY, [A]), purse(PAY, [A]),
             ])),
        Case("key-collision-left-funded", 1,
             term_par([
                 whole_redex(PAY, COLLISION_LEFT),
                 purse(PAY, [signature("ab")]),
                 purse(PAY, [signature("c")]),
             ])),
        Case("key-collision-left-not-right", 1,
             term_par([
                 whole_redex(PAY, COLLISION_LEFT),
                 purse(PAY, [signature("a")]),
                 purse(PAY, [signature("bc")]),
             ])),
        Case("key-collision-right-funded", 1,
             term_par([
                 whole_redex(PAY, COLLISION_RIGHT),
                 purse(PAY, [signature("a")]),
                 purse(PAY, [signature("bc")]),
             ])),
        Case("utf8-single-atom", 1,
             term_par([
                 whole_redex(PAY, UTF8_SINGLE),
                 purse(PAY, [UTF8_SINGLE]),
             ])),
        Case("utf8-compound-split", 1,
             term_par([
                 whole_redex(PAY, UTF8_PAIR),
                 purse(PAY, [signature("é")]),
                 purse(PAY, [signature("λ")]),
             ])),
        Case("delimiter-shaped-atoms", 1,
             term_par([
                 whole_redex(PAY, DELIMITER_PAIR),
                 purse(PAY, [signature("a,b")]),
                 purse(PAY, [signature("a:bc")]),
             ])),
    ]

    purse_patterns: list[tuple[str, list[Wire]]] = [
        ("none", []),
        ("pay-a", [purse(PAY, [A])]),
        ("pay-b", [purse(PAY, [B])]),
        ("wrong-a", [purse(WRONG, [A])]),
        ("pay-a-b-split", [purse(PAY, [A]), purse(PAY, [B])]),
        ("pay-ab", [purse(PAY, [AB])]),
        ("cross-a-b", [purse(PAY, [A]), purse(WRONG, [B])]),
        ("pay-a-a-duplicate", [purse(PAY, [A]), purse(PAY, [A])]),
        ("pay-a-with-tail", [purse(PAY, [A, B])]),
    ]
    demands = [("a", A), ("ab", AB), ("aa", AA)]
    for order in ("recv-send", "send-recv"):
        for demand_name, demand in demands:
            for purses_name, purses in purse_patterns:
                term = term_par([whole_redex(PAY, demand, order), *purses])
                base = f"matrix-whole-{order}-{demand_name}-{purses_name}"
                cases.append(Case(f"{base}-fuel0", 0, term))
                cases.append(Case(f"{base}-fuel1", 1, term))

    split_demands = [("a-b", A, B), ("a-a", A, A)]
    for demand_name, recv_sig, send_sig in split_demands:
        for purses_name, purses in purse_patterns:
            term = term_par([*split_redex(PAY, recv_sig, send_sig), *purses])
            cases.append(Case(f"matrix-split-{demand_name}-{purses_name}", 1, term))

    return cases


def make_branch_cases() -> list[BranchCase]:
    done_a = signed(proc_nil(), signature("done-a"))
    done_b = signed(proc_nil(), signature("done-b"))
    payload_a = signed(proc_nil(), signature("payload-a"))
    payload_b = signed(proc_nil(), signature("payload-b"))
    return [
        BranchCase(
            "two-whole-redexes-one-purse",
            term_par([
                whole_redex(PAY, A, body=done_a),
                whole_redex(PAY, A, body=done_b),
                purse(PAY, [A]),
            ]),
        ),
        BranchCase(
            "alternative-exact-funding-covers",
            term_par([
                whole_redex(PAY, AB),
                purse(PAY, [AB]),
                purse(PAY, [A]),
                purse(PAY, [B]),
            ]),
        ),
        BranchCase(
            "two-receivers-one-sender",
            term_par([
                signed(recv(PAY, done_a), A),
                signed(recv(PAY, done_b), A),
                signed(send(PAY, payload_a), B),
                purse(PAY, [AB]),
            ]),
        ),
        BranchCase(
            "one-receiver-two-senders",
            term_par([
                signed(recv(PAY, done_a), A),
                signed(send(PAY, payload_a), B),
                signed(send(PAY, payload_b), B),
                purse(PAY, [AB]),
            ]),
        ),
    ]


def run_cetta(
    bin_path: str, cases: list[Case], thread_count: int = 1
) -> tuple[list[dict[str, Wire]], list[str]]:
    script = ["!(import! &self lts:rho:cost)"]
    if thread_count > 1:
        script.append(f"!(pragma! num-threads {thread_count})")
    script.extend(
        f"!(lts:rho:cost:causal-prefix {case.fuel} {case.search_fuel} "
        f"{render_term(case.term)})"
        for case in cases
    )
    proc = subprocess.run(
        [bin_path, "--profile", "extended", "--lang", "he", "/dev/stdin"],
        input="\n".join(script) + "\n",
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    prelude_len = 2 if thread_count > 1 else 1
    if (len(lines) != len(cases) + prelude_len or
            any(line != "[()]" for line in lines[:prelude_len])):
        raise RuntimeError(
            f"expected {prelude_len} setup results plus {len(cases)} prefixes, "
            f"saw {len(lines)} lines"
        )
    raw_lines = lines[prelude_len:]
    return ([{"result": parse_cetta_prefix(line)} for line in raw_lines], raw_lines)


def run_branch_outcome_checks(
    bin_path: str, cases: list[BranchCase]
) -> tuple[int, list[tuple[str, Wire, Wire]]]:
    reference_script = ["!(import! &self lts:rho:cost)"]
    reference_script.extend(
        f"!(lts:rho:cost:steps {render_term(case.term)})" for case in cases
    )
    reference_proc = subprocess.run(
        [bin_path, "--profile", "extended", "--lang", "he", "/dev/stdin"],
        input="\n".join(reference_script) + "\n",
        check=False,
        text=True,
        capture_output=True,
    )
    if reference_proc.returncode != 0:
        raise RuntimeError(reference_proc.stderr.strip() or reference_proc.stdout.strip())
    reference_lines = [
        line.strip() for line in reference_proc.stdout.splitlines() if line.strip()
    ]
    if (len(reference_lines) != len(cases) + 1 or
            reference_lines[0] != "[()]"):
        raise RuntimeError(
            f"expected one setup result plus {len(cases)} frontiers, "
            f"saw {len(reference_lines)} lines"
        )

    frontiers = [parse_cetta_steps(line) for line in reference_lines[1:]]
    repeats = [max(4, 2 * len(frontier)) for frontier in frontiers]
    threaded_script = [
        "!(import! &self lts:rho:cost)",
        "!(pragma! num-threads 4)",
    ]
    for case, repeat in zip(cases, repeats, strict=True):
        threaded_script.extend(
            f"!(lts:rho:cost:causal-prefix 1 10000 {render_term(case.term)})"
            for _ in range(repeat)
        )
    threaded_proc = subprocess.run(
        [bin_path, "--profile", "extended", "--lang", "he", "/dev/stdin"],
        input="\n".join(threaded_script) + "\n",
        check=False,
        text=True,
        capture_output=True,
    )
    if threaded_proc.returncode != 0:
        raise RuntimeError(threaded_proc.stderr.strip() or threaded_proc.stdout.strip())
    threaded_lines = [
        line.strip() for line in threaded_proc.stdout.splitlines() if line.strip()
    ]
    expected_lines = 2 + sum(repeats)
    if (len(threaded_lines) != expected_lines or
            any(line != "[()]" for line in threaded_lines[:2])):
        raise RuntimeError(
            f"expected two setup results plus {sum(repeats)} branch samples, "
            f"saw {len(threaded_lines)} lines"
        )

    cursor = 2
    replay_samples: list[tuple[str, Wire, Wire]] = []
    for case, frontier, repeat in zip(cases, frontiers, repeats, strict=True):
        if len(frontier) < 2:
            raise RuntimeError(
                f"branch case {case.name} has only {len(frontier)} exhaustive steps"
            )
        expected = {
            json.dumps(wire_node(step, "cost-step")[1][1], sort_keys=True,
                       separators=(",", ":"))
            for step in frontier
        }
        observed: set[str] = set()
        for sample_index, line in enumerate(
            threaded_lines[cursor:cursor + repeat]
        ):
            prefix = parse_cetta_prefix(line)
            replay_samples.append(
                (f"branch:{case.name}:{sample_index}", case.term, prefix)
            )
            _, fields = wire_node(prefix, "causal-prefix")
            receipt_tag, events = wire_node(fields[1], "receipt")
            if receipt_tag != "receipt" or len(events) != 1:
                raise RuntimeError(
                    f"branch sample {case.name} did not emit exactly one event: {line}"
                )
            observed.add(
                json.dumps(fields[2], sort_keys=True, separators=(",", ":"))
            )
        cursor += repeat
        if observed != expected:
            missing = sorted(expected - observed)
            extra = sorted(observed - expected)
            raise RuntimeError(
                f"threaded branch outcome set mismatch for {case.name}: "
                f"missing={missing!r}, extra={extra!r}"
            )
    return len(cases), replay_samples


def read_fixture(path: Path) -> dict[str, str]:
    recorded: dict[str, str] = {}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        name, separator, printed = line.partition("\t")
        if not separator or not printed or name in recorded:
            raise ValueError(f"{path.name}:{number}: expected one 'name<TAB>result' line per case")
        recorded[name] = printed
    return recorded


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_rhocalc_cost_prefixes.py <cetta-bin>", file=sys.stderr)
        return 2
    bin_path = sys.argv[1]
    cases = make_cases()
    branch_cases = make_branch_cases()

    boundary_exclusions = 0
    try:
        render_signature(signature(""))
    except ValueError:
        boundary_exclusions += 1
    else:
        print("the cost-rho cases accepted an empty symbol atom", file=sys.stderr)
        return 1

    try:
        recorded = read_fixture(FIXTURE)
        sequential, raw_lines = run_cetta(bin_path, cases)
        threaded, threaded_raw_lines = run_cetta(bin_path, cases, 4)
        branch_count, _ = run_branch_outcome_checks(bin_path, branch_cases)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"cost-rho causal prefix check failed: {exc}", file=sys.stderr)
        return 1

    names = [case.name for case in cases]
    if sorted(recorded) != sorted(names):
        missing = sorted(set(names) - set(recorded))
        extra = sorted(set(recorded) - set(names))
        print(f"FAIL: {FIXTURE.name} does not record exactly the cases: "
              f"{len(missing)} missing {missing[:8]!r}, "
              f"{len(extra)} extra {extra[:8]!r}", file=sys.stderr)
        return 1

    failures = 0
    for case, raw_line in zip(cases, raw_lines, strict=True):
        if raw_line == recorded[case.name]:
            continue
        failures += 1
        print(f"MISMATCH: {case.name}", file=sys.stderr)
        print(f"  recorded: {recorded[case.name]}", file=sys.stderr)
        print(f"  printed:  {raw_line}", file=sys.stderr)
        if failures == 8:
            break
    if failures:
        print(f"FAIL: {failures} cost-rho causal prefixes differ from their record",
              file=sys.stderr)
        return 1

    for case, sequential_outcome, threaded_outcome, raw_line in zip(
        cases, sequential, threaded, threaded_raw_lines, strict=True
    ):
        if threaded_outcome == sequential_outcome:
            continue
        failures += 1
        print(f"THREADED MISMATCH: {case.name}", file=sys.stderr)
        print("  sequential: " + json.dumps(
            sequential_outcome, sort_keys=True, separators=(",", ":")), file=sys.stderr)
        print("  threaded:   " + json.dumps(
            threaded_outcome, sort_keys=True, separators=(",", ":")), file=sys.stderr)
        print(f"  raw:        {raw_line}", file=sys.stderr)
        if failures == 8:
            break
    if failures:
        print(f"FAIL: {failures} threaded/sequential cost-rho prefix mismatches",
              file=sys.stderr)
        return 1

    print(
        f"PASS: {len(cases)} bounded cost-rho causal prefixes as recorded; "
        f"{len(cases)} threaded/sequential prefix cases; "
        f"{branch_count} exhaustive/threaded branch outcome sets; "
        f"{boundary_exclusions} out-of-fragment boundary case rejected"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
