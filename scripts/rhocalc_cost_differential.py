#!/usr/bin/env python3
"""Bounded CeTTa/Lean differential checks for the pure cost-rho wire boundary.

The comparison exercises two independently implemented reducers over the same
versioned wire inputs.  It is conformance testing, not a proof about compiled C.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from mettapedia_paths import discover_mettapedia_root
from rhocalc_tiny_semantics import SExpr, parse_sexpr, sexpr_to_text


SCHEMA = "cetta.cost-rho.causal-prefix.v1"
SAFE_ATOM = re.compile(r"^[A-Za-z][A-Za-z0-9-]*$")

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
    if not SAFE_ATOM.fullmatch(value):
        raise ValueError(f"test atom is not a safe CeTTa symbol: {value!r}")
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


@dataclass(frozen=True)
class Case:
    name: str
    fuel: int
    term: Wire

    def request(self) -> dict[str, Any]:
        return {"schema": SCHEMA, "fuel": self.fuel, "term": self.term}


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


def whole_redex(surface: Wire, demand: Wire, order: str = "recv-send",
                 body: Wire = DONE) -> Wire:
    endpoints = [recv(surface, body), send(surface, PAYLOAD)]
    if order == "send-recv":
        endpoints.reverse()
    return signed(proc_par(endpoints), demand)


def split_redex(surface: Wire, recv_sig: Wire, send_sig: Wire) -> list[Wire]:
    return [signed(recv(surface, DONE), recv_sig), signed(send(surface, PAYLOAD), send_sig)]


def make_cases() -> list[Case]:
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


def run_cetta(bin_path: str, cases: list[Case]) -> tuple[list[dict[str, Wire]], list[str]]:
    script = ["!(import! &self lts:rho:cost)"]
    script.extend(
        f"!(lts:rho:cost:causal-prefix {case.fuel} {render_term(case.term)})"
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
    if len(lines) != len(cases) + 1 or lines[0] != "[()]":
        raise RuntimeError(
            f"expected import result plus {len(cases)} prefixes, saw {len(lines)} lines"
        )
    raw_lines = lines[1:]
    return ([{"result": parse_cetta_prefix(line)} for line in raw_lines], raw_lines)


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
    outcomes = json.loads(lines[-1])
    if not isinstance(outcomes, list) or len(outcomes) != len(cases):
        raise RuntimeError(
            f"expected {len(cases)} Lean outcomes, saw "
            f"{len(outcomes) if isinstance(outcomes, list) else type(outcomes).__name__}"
        )
    return outcomes


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
    cases = make_cases()

    try:
        cetta, raw_lines = run_cetta(bin_path, cases)
        lean = run_lean(mettapedia_root, runner, cases)
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

    print(
        f"PASS: {len(cases)} bounded cost-rho CeTTa/Lean cases; "
        "testing boundary only, not a universal theorem about compiled C"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
