#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Name:
    pass


@dataclass(frozen=True)
class NameVar(Name):
    spelling: str


@dataclass(frozen=True)
class NameQuote(Name):
    proc: "Proc"


@dataclass(frozen=True)
class Proc:
    pass


@dataclass(frozen=True)
class ProcNil(Proc):
    pass


@dataclass(frozen=True)
class ProcPar(Proc):
    parts: tuple[Proc, ...]


@dataclass(frozen=True)
class ProcSend(Proc):
    channel: Name
    payload: Proc


@dataclass(frozen=True)
class ProcRecv(Proc):
    channel: Name
    binder: str
    body: Proc


@dataclass(frozen=True)
class ProcDrop(Proc):
    name: Name


SExpr = str | tuple["SExpr", ...]


def strip_comments(text: str) -> str:
    lines: list[str] = []
    for line in text.splitlines():
        comment = line.find(";")
        if comment >= 0:
            line = line[:comment]
        lines.append(line)
    return "\n".join(lines)


def tokenize(text: str) -> list[str]:
    tokens: list[str] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch.isspace():
            i += 1
            continue
        if ch in "()":
            tokens.append(ch)
            i += 1
            continue
        j = i
        while j < len(text) and (not text[j].isspace()) and text[j] not in "()":
            j += 1
        tokens.append(text[i:j])
        i = j
    return tokens


def parse_sexpr(text: str) -> SExpr:
    tokens = tokenize(strip_comments(text))
    pos = 0

    def parse_one() -> SExpr:
        nonlocal pos
        if pos >= len(tokens):
            raise ValueError("unexpected end of input")
        token = tokens[pos]
        pos += 1
        if token == "(":
            items: list[SExpr] = []
            while True:
                if pos >= len(tokens):
                    raise ValueError("missing closing ')'")
                if tokens[pos] == ")":
                    pos += 1
                    return tuple(items)
                items.append(parse_one())
        if token == ")":
            raise ValueError("unexpected ')'")
        return token

    result = parse_one()
    if pos != len(tokens):
        raise ValueError("unexpected trailing tokens")
    return result


def _expect_symbol(expr: SExpr) -> str:
    if not isinstance(expr, str):
        raise ValueError("expected symbol")
    return expr


def parse_name(expr: SExpr) -> Name:
    if isinstance(expr, str):
        if not expr.startswith("$"):
            raise ValueError(f"unsupported name atom: {expr}")
        return NameVar(expr)
    if not expr:
        raise ValueError("empty name form")
    head = _expect_symbol(expr[0])
    if head != "rho:quote" or len(expr) != 2:
        raise ValueError(f"unsupported name form: {sexpr_to_text(expr)}")
    return NameQuote(parse_proc(expr[1]))


def parse_proc(expr: SExpr) -> Proc:
    if isinstance(expr, str):
        if expr == "rho:nil":
            return ProcNil()
        raise ValueError(f"unsupported process atom: {expr}")
    if not expr:
        raise ValueError("empty process form")
    head = _expect_symbol(expr[0])
    if head == "rho:par":
        return ProcPar(tuple(parse_proc(arg) for arg in expr[1:]))
    if head == "rho:send" and len(expr) == 3:
        return ProcSend(parse_name(expr[1]), parse_proc(expr[2]))
    if head == "rho:recv" and len(expr) == 4:
        binder = _expect_symbol(expr[2])
        if not binder.startswith("$"):
            raise ValueError(f"unsupported binder spelling: {binder}")
        return ProcRecv(parse_name(expr[1]), binder, parse_proc(expr[3]))
    if head == "rho:drop" and len(expr) == 2:
        return ProcDrop(parse_name(expr[1]))
    raise ValueError(f"unsupported process form: {sexpr_to_text(expr)}")


def parse_mrho_proc(text: str) -> Proc:
    return parse_proc(parse_sexpr(text))


def sexpr_to_text(expr: SExpr) -> str:
    if isinstance(expr, str):
        return expr
    return "(" + " ".join(sexpr_to_text(item) for item in expr) + ")"


def name_to_mrho(name: Name) -> str:
    if isinstance(name, NameVar):
        return name.spelling
    if isinstance(name, NameQuote):
        return f"(rho:quote {proc_to_mrho(name.proc)})"
    raise TypeError(f"unsupported name: {name!r}")


def proc_to_mrho(proc: Proc) -> str:
    if isinstance(proc, ProcNil):
        return "rho:nil"
    if isinstance(proc, ProcPar):
        return "(rho:par " + " ".join(proc_to_mrho(part) for part in proc.parts) + ")"
    if isinstance(proc, ProcSend):
        return f"(rho:send {name_to_mrho(proc.channel)} {proc_to_mrho(proc.payload)})"
    if isinstance(proc, ProcRecv):
        return (
            f"(rho:recv {name_to_mrho(proc.channel)} {proc.binder} "
            f"{proc_to_mrho(proc.body)})"
        )
    if isinstance(proc, ProcDrop):
        return f"(rho:drop {name_to_mrho(proc.name)})"
    raise TypeError(f"unsupported process: {proc!r}")


def name_key(name: Name) -> str:
    if isinstance(name, NameVar):
        return name.spelling
    if isinstance(name, NameQuote):
        if isinstance(name.proc, ProcDrop):
            return name_key(name.proc.name)
        return "@(" + proc_key(name.proc) + ")"
    raise TypeError(f"unsupported name: {name!r}")


def proc_key(proc: Proc) -> str:
    if isinstance(proc, ProcNil):
        return "0"
    if isinstance(proc, ProcPar):
        return "par(" + "|".join(proc_key(part) for part in proc.parts) + ")"
    if isinstance(proc, ProcSend):
        return f"send({name_key(proc.channel)},{proc_key(proc.payload)})"
    if isinstance(proc, ProcRecv):
        return f"recv({name_key(proc.channel)},{proc_key(proc.body)})"
    if isinstance(proc, ProcDrop):
        return f"drop({name_key(proc.name)})"
    raise TypeError(f"unsupported process: {proc!r}")


def normalize_name(name: Name) -> Name:
    if isinstance(name, NameVar):
        return name
    if isinstance(name, NameQuote):
        return NameQuote(normalize_proc(name.proc))
    raise TypeError(f"unsupported name: {name!r}")


def _par_parts(proc: Proc) -> list[Proc]:
    if isinstance(proc, ProcPar):
        parts: list[Proc] = []
        for part in proc.parts:
            parts.extend(_par_parts(part))
        return parts
    return [proc]


def normalize_proc(proc: Proc) -> Proc:
    if isinstance(proc, ProcNil):
        return proc
    if isinstance(proc, ProcSend):
        return ProcSend(normalize_name(proc.channel), normalize_proc(proc.payload))
    if isinstance(proc, ProcRecv):
        return ProcRecv(normalize_name(proc.channel), proc.binder, normalize_proc(proc.body))
    if isinstance(proc, ProcDrop):
        return ProcDrop(normalize_name(proc.name))
    if isinstance(proc, ProcPar):
        flat: list[Proc] = []
        for part in proc.parts:
            norm = normalize_proc(part)
            if isinstance(norm, ProcNil):
                continue
            if isinstance(norm, ProcPar):
                flat.extend(norm.parts)
            else:
                flat.append(norm)
        if not flat:
            return ProcNil()
        flat.sort(key=proc_key)
        if len(flat) == 1:
            return flat[0]
        return ProcPar(tuple(flat))
    raise TypeError(f"unsupported process: {proc!r}")


def top_level_components(proc: Proc) -> list[Proc]:
    norm = normalize_proc(proc)
    if isinstance(norm, ProcPar):
        return list(norm.parts)
    if isinstance(norm, ProcNil):
        return []
    return [norm]


def immediate_output_barbs(proc: Proc) -> set[str]:
    outputs: set[str] = set()
    for component in top_level_components(proc):
        if isinstance(component, ProcSend):
            outputs.add(name_key(component.channel))
    return outputs


def immediate_output_barbs_from_mrho(text: str) -> set[str]:
    return immediate_output_barbs(parse_mrho_proc(text))


def substitute_name_with_match(
    name: Name, binder: str, replacement: Name
) -> tuple[Name, bool]:
    if isinstance(name, NameVar):
        if name.spelling == binder:
            return replacement, True
        return name, False
    if isinstance(name, NameQuote):
        return name, False
    raise TypeError(f"unsupported name: {name!r}")


def substitute_proc(proc: Proc, binder: str, replacement: Name) -> Proc:
    if isinstance(proc, ProcNil):
        return proc
    if isinstance(proc, ProcPar):
        return ProcPar(tuple(substitute_proc(part, binder, replacement) for part in proc.parts))
    if isinstance(proc, ProcSend):
        next_channel, _ = substitute_name_with_match(proc.channel, binder, replacement)
        return ProcSend(
            next_channel,
            substitute_proc(proc.payload, binder, replacement),
        )
    if isinstance(proc, ProcRecv):
        next_channel, _ = substitute_name_with_match(proc.channel, binder, replacement)
        if proc.binder == binder:
            return ProcRecv(next_channel, proc.binder, proc.body)
        return ProcRecv(
            next_channel,
            proc.binder,
            substitute_proc(proc.body, binder, replacement),
        )
    if isinstance(proc, ProcDrop):
        next_name, matched = substitute_name_with_match(proc.name, binder, replacement)
        if matched and isinstance(next_name, NameQuote):
            return normalize_proc(next_name.proc)
        return ProcDrop(next_name)
    raise TypeError(f"unsupported process: {proc!r}")


def _comm_successors(proc: Proc) -> set[Proc]:
    components = top_level_components(proc)
    sends = [
        (index, component)
        for index, component in enumerate(components)
        if isinstance(component, ProcSend)
    ]
    recvs = [
        (index, component)
        for index, component in enumerate(components)
        if isinstance(component, ProcRecv)
    ]
    results: set[str] = set()
    for send_index, send in sends:
        for recv_index, recv in recvs:
            if name_key(send.channel) != name_key(recv.channel):
                continue
            replacement = NameQuote(normalize_proc(send.payload))
            body = normalize_proc(substitute_proc(recv.body, recv.binder, replacement))
            rebuilt = [
                component
                for index, component in enumerate(components)
                if index not in (send_index, recv_index)
            ]
            rebuilt.append(body)
            results.add(normalize_proc(ProcPar(tuple(rebuilt))))
    return results


def immediate_successors(proc: Proc) -> list[str]:
    results = {proc_to_mrho(item) for item in _comm_successors(proc)}
    return sorted(results)


def immediate_successors_from_mrho(text: str) -> list[str]:
    return immediate_successors(parse_mrho_proc(text))
