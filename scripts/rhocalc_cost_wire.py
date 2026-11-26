#!/usr/bin/env python3
"""The cost-rho wire: its constructors, its rendering as cost-rho syntax, and
the reading of the causal prefixes and step frontiers CeTTa prints."""

from __future__ import annotations

from typing import Any

from rhocalc_tiny_semantics import SExpr, parse_sexpr, sexpr_to_text


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


def send(syntax: Wire, payload: Wire) -> Wire:
    return node("send", [syntax, payload])


def recv(syntax: Wire, body: Wire) -> Wire:
    return node("recv", [syntax, body])


def signed(proc: Wire, spend: Wire) -> Wire:
    return node("signed", [proc, spend])


def purse(syntax: Wire, stack: list[Wire]) -> Wire:
    return node("purse", [syntax, node("stack", stack)])


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


def whole_redex(syntax: Wire, demand: Wire, order: str = "recv-send",
                 body: Wire = DONE) -> Wire:
    endpoints = [recv(syntax, body), send(syntax, PAYLOAD)]
    if order == "send-recv":
        endpoints.reverse()
    return signed(proc_par(endpoints), demand)


def split_redex(syntax: Wire, recv_sig: Wire, send_sig: Wire) -> list[Wire]:
    return [signed(recv(syntax, DONE), recv_sig), signed(send(syntax, PAYLOAD), send_sig)]
