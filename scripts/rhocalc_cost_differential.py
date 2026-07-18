#!/usr/bin/env python3
"""Bounded CeTTa/Lean differential checks for the pure cost-rho wire boundary.

The comparison exercises two independently implemented reducers over the same
versioned wire inputs.  It is conformance testing, not a proof about compiled C.
"""

from __future__ import annotations

import copy
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from mettapedia_paths import discover_mettapedia_root
from rhocalc_tiny_semantics import SExpr, parse_sexpr, sexpr_to_text


SCHEMA = "cetta.cost-rho.causal-prefix.v2"
RECEIPT_REPLAY_SCHEMA = "cetta.cost-rho.receipt-replay.v1"
Wire = dict[str, Any]


def symbol(value: str) -> Wire:
    return {"symbol": value}


def natural(value: int) -> Wire:
    return {"natural": value}


def node(tag: str, children: list[Wire]) -> Wire:
    return {"node": [tag, children]}


def signature(*atoms: str) -> Wire:
    return node("signature", [symbol(atom) for atom in sorted(atoms)])


def channel(atom: str) -> Wire:
    return signature(atom)


def proc_nil() -> Wire:
    return node("proc-nil", [])


def term_nil() -> Wire:
    return node("term-nil", [])


def proc_par(parts: list[Wire]) -> Wire:
    if not parts:
        return proc_nil()
    if len(parts) == 1:
        return parts[0]
    return node("proc-par", [parts[0], proc_par(parts[1:])])


def term_par(parts: list[Wire]) -> Wire:
    if not parts:
        return term_nil()
    if len(parts) == 1:
        return parts[0]
    return node("term-par", [parts[0], term_par(parts[1:])])


def send(surface: Wire, payload: Wire) -> Wire:
    return node("send", [surface, payload])


def recv(surface: Wire, body: Wire) -> Wire:
    return node("recv", [surface, body])


def signed(proc: Wire, spend: Wire) -> Wire:
    return node("signed", [proc, spend])


def purse(surface: Wire, stack: list[Wire]) -> Wire:
    return node("purse", [surface, node("stack", stack)])


def drop(name: Wire) -> Wire:
    return node("drop", [name])


def bvar(index: int) -> Wire:
    return node("bvar", [natural(index)])


def wire_node(wire: Wire, expected: str | None = None) -> tuple[str, list[Wire]]:
    payload = wire.get("node")
    if not isinstance(payload, list) or len(payload) != 2:
        raise ValueError(f"expected wire node, saw {wire!r}")
    tag, children = payload
    if not isinstance(tag, str) or not isinstance(children, list):
        raise ValueError(f"malformed wire node: {wire!r}")
    if expected is not None and tag != expected:
        raise ValueError(f"expected {expected!r}, saw {tag!r}")
    return tag, children


def wire_symbol(wire: Wire) -> str:
    value = wire.get("symbol")
    if not isinstance(value, str):
        raise ValueError(f"expected wire symbol, saw {wire!r}")
    if (
        not value
        or value.startswith(("rho:", "$"))
        or any(character.isspace() or character in '()"' for character in value)
    ):
        raise ValueError(f"test atom is outside the CeTTa symbol boundary: {value!r}")
    return value


def render_signature(wire: Wire) -> str:
    _, atoms = wire_node(wire, "signature")
    rendered = [wire_symbol(atom) for atom in atoms]
    if len(rendered) == 1:
        return rendered[0]
    if len(rendered) < 2:
        raise ValueError("cost signatures must be nonempty")
    return f"(rho:cost:sig-mul {' '.join(rendered)})"


def render_name(wire: Wire, binders: list[str]) -> str:
    tag, children = wire_node(wire)
    if tag == "signature":
        return render_signature(wire)
    if tag == "bvar" and len(children) == 1:
        value = children[0].get("natural")
        if not isinstance(value, int) or value < 0 or value >= len(binders):
            raise ValueError(f"unbound de Bruijn index in wire name: {wire!r}")
        return binders[value]
    if tag == "quote" and len(children) == 1:
        return f"(rho:quote {render_term(children[0], binders)})"
    raise ValueError(f"unsupported wire name: {wire!r}")


def render_proc(wire: Wire, binders: list[str]) -> str:
    tag, children = wire_node(wire)
    if tag == "proc-nil" and not children:
        return "rho:nil"
    if tag == "proc-par" and len(children) == 2:
        return (
            f"(rho:par {render_proc(children[0], binders)} "
            f"{render_proc(children[1], binders)})"
        )
    if tag == "send" and len(children) == 2:
        return (
            f"(rho:send {render_name(children[0], binders)} "
            f"{render_term(children[1], binders)})"
        )
    if tag == "recv" and len(children) == 2:
        binder = f"$wire{len(binders)}"
        return (
            f"(rho:recv {render_name(children[0], binders)} {binder} "
            f"{render_term(children[1], [binder, *binders])})"
        )
    raise ValueError(f"unsupported wire process: {wire!r}")


def render_stack(wire: Wire) -> str:
    _, frames = wire_node(wire, "stack")
    rendered = "rho:cost:stack-empty"
    for frame in reversed(frames):
        rendered = f"(rho:cost:stack-cons {render_signature(frame)} {rendered})"
    return rendered


def render_term(wire: Wire, binders: list[str] | None = None) -> str:
    if binders is None:
        binders = []
    tag, children = wire_node(wire)
    if tag == "term-nil" and not children:
        return "rho:cost:nil"
    if tag == "signed" and len(children) == 2:
        return (
            f"(rho:cost:signed {render_proc(children[0], binders)} "
            f"{render_signature(children[1])})"
        )
    if tag == "term-par" and len(children) == 2:
        return (
            f"(rho:cost:par {render_term(children[0], binders)} "
            f"{render_term(children[1], binders)})"
        )
    if tag == "drop" and len(children) == 1:
        return f"(rho:drop {render_name(children[0], binders)})"
    if tag == "purse" and len(children) == 2:
        return (
            f"(rho:cost:purse {render_name(children[0], binders)} "
            f"{render_stack(children[1])})"
        )
    raise ValueError(f"unsupported wire term: {wire!r}")


def expect_tuple(expr: SExpr, label: str) -> tuple[SExpr, ...]:
    if not isinstance(expr, tuple):
        raise ValueError(f"expected {label}, saw {expr!r}")
    return expr


def expect_atom(expr: SExpr, label: str) -> str:
    if not isinstance(expr, str):
        raise ValueError(f"expected {label}, saw {sexpr_to_text(expr)}")
    return expr


def parse_signature(expr: SExpr) -> Wire:
    if isinstance(expr, str):
        if expr.startswith("rho:") or expr.startswith("$"):
            raise ValueError(f"expected ground signature, saw {expr!r}")
        return signature(expr)
    items = expect_tuple(expr, "signature product")
    if len(items) < 3 or expect_atom(items[0], "signature head") != "rho:cost:sig-mul":
        raise ValueError(f"unsupported signature: {sexpr_to_text(expr)}")
    atoms: list[str] = []
    for item in items[1:]:
        parsed = parse_signature(item)
        _, children = wire_node(parsed, "signature")
        atoms.extend(wire_symbol(child) for child in children)
    return signature(*atoms)


def parse_name(expr: SExpr, binders: list[str]) -> Wire:
    if isinstance(expr, str):
        if expr.startswith("$"):
            try:
                return bvar(binders.index(expr))
            except ValueError as exc:
                raise ValueError(f"unbound runtime variable {expr!r}") from exc
        return parse_signature(expr)
    items = expect_tuple(expr, "name")
    if len(items) == 2 and expect_atom(items[0], "name head") == "rho:quote":
        return node("quote", [parse_term(items[1], binders)])
    return parse_signature(expr)


def parse_proc(expr: SExpr, binders: list[str]) -> Wire:
    if isinstance(expr, str):
        if expr == "rho:nil":
            return proc_nil()
        raise ValueError(f"unsupported runtime process atom: {expr!r}")
    items = expect_tuple(expr, "process")
    if not items:
        raise ValueError("empty runtime process")
    head = expect_atom(items[0], "process head")
    if head == "rho:par":
        return proc_par([parse_proc(item, binders) for item in items[1:]])
    if head == "rho:send" and len(items) == 3:
        return send(parse_name(items[1], binders), parse_term(items[2], binders))
    if head == "rho:recv" and len(items) == 4:
        binder = expect_atom(items[2], "receive binder")
        if not binder.startswith("$"):
            raise ValueError(f"invalid receive binder: {binder!r}")
        return recv(
            parse_name(items[1], binders),
            parse_term(items[3], [binder, *binders]),
        )
    raise ValueError(f"unsupported runtime process: {sexpr_to_text(expr)}")


def parse_stack(expr: SExpr) -> list[Wire]:
    if expr == "rho:cost:stack-empty":
        return []
    items = expect_tuple(expr, "cost stack")
    if len(items) != 3 or expect_atom(items[0], "stack head") != "rho:cost:stack-cons":
        raise ValueError(f"unsupported cost stack: {sexpr_to_text(expr)}")
    return [parse_signature(items[1]), *parse_stack(items[2])]


def parse_term(expr: SExpr, binders: list[str] | None = None) -> Wire:
    if binders is None:
        binders = []
    if isinstance(expr, str):
        if expr == "rho:cost:nil":
            return term_nil()
        raise ValueError(f"unsupported runtime cost atom: {expr!r}")
    items = expect_tuple(expr, "cost term")
    if not items:
        raise ValueError("empty runtime cost term")
    head = expect_atom(items[0], "cost-term head")
    if head == "rho:cost:signed" and len(items) == 3:
        return signed(parse_proc(items[1], binders), parse_signature(items[2]))
    if head == "rho:cost:par":
        return term_par([parse_term(item, binders) for item in items[1:]])
    if head == "rho:drop" and len(items) == 2:
        return drop(parse_name(items[1], binders))
    if head == "rho:cost:purse" and len(items) == 3:
        return purse(parse_name(items[1], binders), parse_stack(items[2]))
    raise ValueError(f"unsupported runtime cost term: {sexpr_to_text(expr)}")


def parse_funding(expr: SExpr) -> Wire:
    items = expect_tuple(expr, "funding contribution")
    if len(items) != 3 or expect_atom(items[0], "funding head") != "lts:rho:cost:funding":
        raise ValueError(f"unsupported funding record: {sexpr_to_text(expr)}")
    return node("funding", [parse_name(items[1], []), parse_signature(items[2])])


def parse_event(expr: SExpr) -> Wire:
    items = expect_tuple(expr, "event")
    if len(items) != 5 or expect_atom(items[0], "event head") != "lts:rho:cost:event":
        raise ValueError(f"unsupported event record: {sexpr_to_text(expr)}")
    event_id = int(expect_atom(items[1], "event id"))
    causes = expect_tuple(items[2], "event causes")
    funding = expect_tuple(items[3], "event funding")
    return node(
        "event",
        [
            natural(event_id),
            node("causes", [natural(int(expect_atom(cause, "cause id"))) for cause in causes]),
            node("funding-list", [parse_funding(entry) for entry in funding]),
            parse_signature(items[4]),
        ],
    )


def parse_cetta_prefix(line: str) -> Wire:
    if not (line.startswith("[") and line.endswith("]")):
        raise ValueError(f"unexpected CeTTa result wrapper: {line!r}")
    inner = line[1:-1].strip()
    expr = parse_sexpr(inner)
    items = expect_tuple(expr, "causal prefix")
    if len(items) != 3 or expect_atom(items[0], "prefix head") != "lts:rho:cost:prefix":
        raise ValueError(f"unsupported causal prefix: {sexpr_to_text(expr)}")
    status_atom = expect_atom(items[1], "prefix status")
    status_map = {
        "lts:rho:cost:quiescent": "quiescent",
        "lts:rho:cost:fuel-exhausted": "fuel-exhausted",
        "lts:rho:cost:search-exhausted": "search-exhausted",
    }
    if status_atom not in status_map:
        raise ValueError(f"unsupported prefix status: {status_atom!r}")
    receipt = expect_tuple(items[2], "receipt")
    if len(receipt) != 3 or expect_atom(receipt[0], "receipt head") != "lts:rho:cost:receipt":
        raise ValueError(f"unsupported receipt: {sexpr_to_text(items[2])}")
    events = expect_tuple(receipt[1], "receipt events")
    return node(
        "causal-prefix",
        [
            symbol(status_map[status_atom]),
            node("receipt", [parse_event(event) for event in events]),
            parse_term(receipt[2]),
        ],
    )


def split_cetta_results(line: str) -> list[str]:
    """Split CeTTa's `[item, item]` wrapper without splitting nested atoms."""
    if not (line.startswith("[") and line.endswith("]")):
        raise ValueError(f"unexpected CeTTa result wrapper: {line!r}")
    inner = line[1:-1].strip()
    if not inner:
        return []

    items: list[str] = []
    start = 0
    depth = 0
    quoted = False
    escaped = False
    for index, character in enumerate(inner):
        if quoted:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
            continue
        if character == '"':
            quoted = True
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth < 0:
                raise ValueError(f"unbalanced CeTTa result wrapper: {line!r}")
        elif character == "," and depth == 0:
            item = inner[start:index].strip()
            if not item:
                raise ValueError(f"empty CeTTa result item: {line!r}")
            items.append(item)
            start = index + 1
    if quoted or depth != 0:
        raise ValueError(f"unbalanced CeTTa result wrapper: {line!r}")
    item = inner[start:].strip()
    if not item:
        raise ValueError(f"empty CeTTa result item: {line!r}")
    items.append(item)
    return items


def parse_cetta_steps(line: str) -> list[Wire]:
    steps: list[Wire] = []
    for item in split_cetta_results(line):
        expr = parse_sexpr(item)
        fields = expect_tuple(expr, "cost step")
        if (len(fields) != 3 or
                expect_atom(fields[0], "step head") != "lts:rho:cost:step"):
            raise ValueError(f"unsupported cost step: {sexpr_to_text(expr)}")
        steps.append(
            node("cost-step", [parse_signature(fields[1]), parse_term(fields[2])])
        )
    return steps


@dataclass(frozen=True)
class Case:
    name: str
    fuel: int
    term: Wire
    search_fuel: int = 10_000

    def request(self) -> dict[str, Any]:
        return {
            "schema": SCHEMA,
            "fuel": self.fuel,
            "searchFuel": self.search_fuel,
            "term": self.term,
        }


@dataclass(frozen=True)
class BranchCase:
    name: str
    term: Wire


PAY = channel("pay")
WRONG = channel("wrong")
BASE = channel("base")
A = signature("alice")
B = signature("bob")
AB = signature("alice", "bob")
AA = signature("alice", "alice")
SEAL = signature("seal")
DONE = signed(proc_nil(), signature("done"))
PAYLOAD = signed(proc_nil(), signature("payload"))
COLLISION_LEFT = signature("ab", "c")
COLLISION_RIGHT = signature("a", "bc")
UTF8_SINGLE = signature("λ")
UTF8_PAIR = signature("é", "λ")
DELIMITER_PAIR = signature("a,b", "a:bc")


def whole_redex(surface: Wire, demand: Wire, order: str = "recv-send",
                 body: Wire = DONE) -> Wire:
    endpoints = [recv(surface, body), send(surface, PAYLOAD)]
    if order == "send-recv":
        endpoints.reverse()
    return signed(proc_par(endpoints), demand)


def split_redex(surface: Wire, recv_sig: Wire, send_sig: Wire) -> list[Wire]:
    return [signed(recv(surface, DONE), recv_sig), signed(send(surface, PAYLOAD), send_sig)]


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
        Case("quoted-alpha-surface", 1,
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
        Case("two-independent-surfaces", 2,
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
        [bin_path, "--profile", "he-extended", "--lang", "he", "/dev/stdin"],
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
        [bin_path, "--profile", "he-extended", "--lang", "he", "/dev/stdin"],
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
        [bin_path, "--profile", "he-extended", "--lang", "he", "/dev/stdin"],
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


def run_lean(mettapedia_root: Path, runner: Path,
             cases: list[Case]) -> list[dict[str, Any]]:
    request_json = json.dumps(
        [case.request() for case in cases], separators=(",", ":"), sort_keys=True
    )
    env = os.environ.copy()
    env["LAKE_JOBS"] = "1"
    env["LEAN_NUM_THREADS"] = "1"
    proc = subprocess.run(
        ["nice", "-n", "19", "lake", "env", "lean", "--run", str(runner), request_json],
        cwd=mettapedia_root,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stdout.strip() + "\n" + proc.stderr.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("Lean differential runner produced no JSON")
    batch = json.loads(lines[-1])
    if not isinstance(batch, dict):
        raise RuntimeError("Lean differential runner produced a non-object batch")
    preconditions = batch.get("preconditions")
    outcomes = batch.get("outcomes")
    if not isinstance(preconditions, list) or len(preconditions) != len(cases):
        raise RuntimeError(
            f"expected {len(cases)} Lean precondition records, saw "
            f"{len(preconditions) if isinstance(preconditions, list) else type(preconditions).__name__}"
        )
    required = {"wellFormed": True, "canonical": True,
                "encodingCanonical": True}
    for case, checks in zip(cases, preconditions, strict=True):
        if checks != required:
            raise RuntimeError(
                f"theorem preconditions failed closed for {case.name}: {checks!r}"
            )
    if not isinstance(outcomes, list) or len(outcomes) != len(cases):
        raise RuntimeError(
            f"expected {len(cases)} Lean outcomes, saw "
            f"{len(outcomes) if isinstance(outcomes, list) else type(outcomes).__name__}"
        )
    return outcomes


def run_receipt_replay(
    mettapedia_root: Path,
    runner: Path,
    cases: list[Case],
    sequential: list[dict[str, Wire]],
    threaded: list[dict[str, Wire]],
    branch_samples: list[tuple[str, Wire, Wire]],
) -> int:
    labeled_results: list[tuple[str, Wire, Wire]] = []
    for mode, outcomes in (("sequential", sequential), ("threaded", threaded)):
        if len(outcomes) != len(cases):
            raise RuntimeError(
                f"receipt replay expected {len(cases)} {mode} outcomes, "
                f"saw {len(outcomes)}"
            )
        for case, outcome in zip(cases, outcomes, strict=True):
            result = outcome.get("result")
            if not isinstance(result, dict):
                raise RuntimeError(
                    f"receipt replay missing result wire for {mode}:{case.name}"
                )
            labeled_results.append((f"{mode}:{case.name}", case.term, result))
    labeled_results.extend(branch_samples)

    requests: list[dict[str, Any]] = []
    labels: list[str] = []
    expected: list[str] = []
    for label, term, result in labeled_results:
        requests.append(
            {
                "schema": RECEIPT_REPLAY_SCHEMA,
                "term": term,
                "result": result,
            }
        )
        labels.append(label)
        expected.append("accepted")

    # Fail-closed probes exercise the actual C wire result rather than a
    # separately constructed Lean fixture.
    first_result = sequential[0].get("result")
    if not isinstance(first_result, dict):
        raise RuntimeError("receipt replay tamper probe lacks a seed result")

    bad_id = copy.deepcopy(first_result)
    try:
        _, prefix_fields = wire_node(bad_id, "causal-prefix")
        _, events = wire_node(prefix_fields[1], "receipt")
        _, event_fields = wire_node(events[0], "event")
        event_fields[0] = natural(999_999)
    except (IndexError, TypeError, ValueError) as exc:
        raise RuntimeError(f"could not construct receipt-ID tamper probe: {exc}") from exc
    requests.append(
        {
            "schema": RECEIPT_REPLAY_SCHEMA,
            "term": cases[0].term,
            "result": bad_id,
        }
    )
    labels.append("tamper:event-id")
    expected.append("rejected")

    bad_residual = copy.deepcopy(first_result)
    try:
        _, prefix_fields = wire_node(bad_residual, "causal-prefix")
        prefix_fields[2] = term_nil()
    except (IndexError, TypeError, ValueError) as exc:
        raise RuntimeError(
            f"could not construct receipt-residual tamper probe: {exc}"
        ) from exc
    requests.append(
        {
            "schema": RECEIPT_REPLAY_SCHEMA,
            "term": cases[0].term,
            "result": bad_residual,
        }
    )
    labels.append("tamper:residual")
    expected.append("rejected")

    env = os.environ.copy()
    env["LAKE_JOBS"] = "1"
    env["LEAN_NUM_THREADS"] = "1"
    target = (
        "Mettapedia.Languages.ProcessCalculi.RhoCalculus.Costed."
        "ReceiptReplayDifferential"
    )
    build = subprocess.run(
        ["lake", "build", target],
        cwd=mettapedia_root,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )
    if build.returncode != 0:
        raise RuntimeError(build.stdout.strip() + "\n" + build.stderr.strip())

    request_json = json.dumps(requests, separators=(",", ":"), sort_keys=True)
    proc = subprocess.run(
        ["nice", "-n", "19", "lake", "env", "lean", "--run", str(runner),
         "--stdin"],
        input=request_json,
        cwd=mettapedia_root,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stdout.strip() + "\n" + proc.stderr.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("Lean receipt replay produced no JSON")
    batch = json.loads(lines[-1])
    outcomes = batch.get("outcomes") if isinstance(batch, dict) else None
    if not isinstance(outcomes, list) or len(outcomes) != len(expected):
        raise RuntimeError(
            f"receipt replay expected {len(expected)} verdicts, saw "
            f"{len(outcomes) if isinstance(outcomes, list) else type(outcomes).__name__}"
        )
    mismatches = [
        (label, want, saw)
        for label, want, saw in zip(labels, expected, outcomes, strict=True)
        if saw != want
    ]
    if mismatches:
        rendered = ", ".join(
            f"{label}: expected {want}, saw {saw}"
            for label, want, saw in mismatches[:8]
        )
        raise RuntimeError(f"Lean receipt replay verdict mismatch: {rendered}")
    return len(labeled_results)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: rhocalc_cost_differential.py <cetta-bin>", file=sys.stderr)
        return 2

    bin_path = sys.argv[1]
    try:
        mettapedia_root = discover_mettapedia_root()
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    runner = Path(__file__).resolve().parents[1] / "tests" / "rhocalc_cost_differential.lean"
    replay_runner = (
        Path(__file__).resolve().parents[1]
        / "tests"
        / "rhocalc_cost_receipt_replay.lean"
    )
    cases = make_cases()
    branch_cases = make_branch_cases()

    boundary_exclusions = 0
    try:
        render_signature(signature(""))
    except ValueError:
        boundary_exclusions += 1
    else:
        print("cost-rho differential harness accepted an empty symbol atom", file=sys.stderr)
        return 1

    try:
        cetta, raw_lines = run_cetta(bin_path, cases)
        threaded, threaded_raw_lines = run_cetta(bin_path, cases, 4)
        branch_count, branch_samples = run_branch_outcome_checks(
            bin_path, branch_cases
        )
        lean = run_lean(mettapedia_root, runner, cases)
        replay_count = run_receipt_replay(
            mettapedia_root, replay_runner, cases, cetta, threaded,
            branch_samples
        )
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"cost-rho differential harness failed: {exc}", file=sys.stderr)
        return 1

    mismatches = 0
    for case, cetta_outcome, lean_outcome, raw_line in zip(
        cases, cetta, lean, raw_lines, strict=True
    ):
        if cetta_outcome == lean_outcome:
            continue
        mismatches += 1
        print(f"MISMATCH: {case.name}", file=sys.stderr)
        print(
            "  CeTTa: " + json.dumps(cetta_outcome, sort_keys=True, separators=(",", ":")),
            file=sys.stderr,
        )
        print(
            "  Lean:  " + json.dumps(lean_outcome, sort_keys=True, separators=(",", ":")),
            file=sys.stderr,
        )
        print(f"  raw:   {raw_line}", file=sys.stderr)
        if mismatches == 8:
            break

    if mismatches:
        print(f"FAIL: {mismatches} bounded cost-rho differential mismatches", file=sys.stderr)
        return 1

    threaded_mismatches = 0
    for case, sequential_outcome, threaded_outcome, raw_line in zip(
        cases, cetta, threaded, threaded_raw_lines, strict=True
    ):
        if threaded_outcome == sequential_outcome:
            continue
        threaded_mismatches += 1
        print(f"THREADED MISMATCH: {case.name}", file=sys.stderr)
        print(
            "  sequential: " + json.dumps(
                sequential_outcome, sort_keys=True, separators=(",", ":")
            ),
            file=sys.stderr,
        )
        print(
            "  threaded:   " + json.dumps(
                threaded_outcome, sort_keys=True, separators=(",", ":")
            ),
            file=sys.stderr,
        )
        print(f"  raw:        {raw_line}", file=sys.stderr)
        if threaded_mismatches == 8:
            break

    if threaded_mismatches:
        print(
            f"FAIL: {threaded_mismatches} threaded/sequential cost-rho "
            "prefix mismatches",
            file=sys.stderr,
        )
        return 1

    print(
        f"PASS: {len(cases)} bounded cost-rho CeTTa/Lean cases; "
        f"{len(cases)} threaded/sequential prefix cases; "
        f"{replay_count} compiled-C receipts replayed by Lean; "
        "2 tampered receipts rejected; "
        f"{branch_count} exhaustive/threaded branch outcome sets; "
        f"{len(cases)}/{len(cases)} theorem precondition records accepted; "
        f"{boundary_exclusions} out-of-fragment boundary case rejected; "
        "testing boundary only, not a universal theorem about compiled C"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
