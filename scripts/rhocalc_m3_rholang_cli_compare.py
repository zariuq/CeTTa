#!/usr/bin/env python3

from __future__ import annotations

import sys

from rhocalc_bounded_reachability import normalize_expr, run_cmd, split_successor_set
from rhocalc_tiny_semantics import (
    Name,
    NameQuote,
    NameVar,
    Proc,
    ProcDrop,
    ProcNil,
    ProcPar,
    ProcRecv,
    ProcSend,
    normalize_proc,
    parse_mrho_proc,
    top_level_components,
)


ObservationSet = tuple[str, ...]


class FreshNameCanon:
    def __init__(self) -> None:
        self._names: dict[str, str] = {}

    def canonical(self, raw: str) -> str:
        if not _is_fresh_name_key(raw):
            return raw
        if raw not in self._names:
            self._names[raw] = f"fresh{len(self._names)}"
        return self._names[raw]


def _is_fresh_name_key(raw: str) -> bool:
    return raw.startswith("rho:unforgeable:") or (
        raw.startswith("private") and raw.removeprefix("private").isdigit()
    )


def parse_observation_set(spec: str) -> ObservationSet:
    spec = spec.strip()
    if spec == "empty":
        return ()
    observations: list[str] = []
    for item in spec.split(","):
        observation = item.strip()
        if not observation:
            continue
        if "=" not in observation:
            raise ValueError(
                f"expected payload-aware observation 'channel=payload', got '{observation}'"
            )
        observations.append(observation)
    return tuple(sorted(observations))


def expected_observation_sets(spec: str) -> set[ObservationSet]:
    return {parse_observation_set(item) for item in spec.split(";") if item.strip()}


def format_observation_set(observations: ObservationSet) -> str:
    if not observations:
        return "empty"
    return ",".join(observations)


def format_expected_observation_sets(expected: set[ObservationSet]) -> str:
    expected_items = sorted(expected)
    return ";".join(format_observation_set(item) for item in expected_items)


def contract_name_key(name: Name, fresh: FreshNameCanon | None = None) -> str:
    if isinstance(name, NameVar):
        raw = name.spelling.removeprefix("$")
        return fresh.canonical(raw) if fresh is not None else raw
    if isinstance(name, NameQuote):
        return f"quote({contract_proc_key(name.proc, fresh)})"
    raise TypeError(f"unsupported name: {name!r}")


def contract_proc_key(proc: Proc, fresh: FreshNameCanon | None = None) -> str:
    proc = normalize_proc(proc)
    if isinstance(proc, ProcNil):
        return "nil"
    if isinstance(proc, ProcPar):
        return "par(" + "|".join(contract_proc_key(part, fresh) for part in proc.parts) + ")"
    if isinstance(proc, ProcSend):
        return (
            f"send({contract_name_key(proc.channel, fresh)} "
            f"{contract_proc_key(proc.payload, fresh)})"
        )
    if isinstance(proc, ProcRecv):
        return (
            f"recv({contract_name_key(proc.channel, fresh)} "
            f"{contract_proc_key(proc.body, fresh)})"
        )
    if isinstance(proc, ProcDrop):
        return f"drop({contract_name_key(proc.name, fresh)})"
    raise TypeError(f"unsupported process: {proc!r}")


def output_observations(proc: Proc) -> ObservationSet:
    fresh = FreshNameCanon()
    observations: list[str] = []
    for component in top_level_components(proc):
        if isinstance(component, ProcSend):
            observations.append(
                f"{contract_name_key(component.channel, fresh)}="
                f"{contract_proc_key(component.payload, fresh)}"
            )
    return tuple(sorted(observations))


def extract_single_mrho_result(output: str) -> str:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    for line in reversed(lines):
        items = split_successor_set(line)
        if len(items) == 1:
            item = items[0]
            if item == "rho:nil" or item.startswith("(rho:"):
                return item
        if line == "rho:nil" or line.startswith("(rho:"):
            return line
    raise RuntimeError("cetta metta run did not produce a single rho term result")


def mrho_residual_observations(bin_path: str, expr: str) -> ObservationSet:
    proc = run_cmd([
        bin_path,
        "--lang",
        "rhocalc",
        "--syntax",
        "mrho",
        "-e",
        expr,
    ])
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("cetta rhocalc run produced no residual output")
    return output_observations(parse_mrho_proc(lines[0]))


def cetta_observed_outputs(
    bin_path: str,
    syntax: str,
    fixture: str,
) -> ObservationSet:
    if syntax == "metta":
        proc = run_cmd([bin_path, fixture])
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
        return mrho_residual_observations(bin_path, extract_single_mrho_result(proc.stdout))

    proc = run_cmd([
        bin_path,
        "--lang",
        "rhocalc",
        "--syntax",
        syntax,
        fixture,
    ])
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("cetta produced no residual output")
    residual = normalize_expr(bin_path, syntax, lines[0])
    return output_observations(parse_mrho_proc(residual))


RholangToken = tuple[str, str]


def tokenize_rholang_storage(text: str) -> list[RholangToken]:
    tokens: list[RholangToken] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch.isspace():
            i += 1
            continue
        if ch in "()!|":
            tokens.append((ch, ch))
            i += 1
            continue
        if ch == '"':
            j = i + 1
            value: list[str] = []
            while j < len(text) and text[j] != '"':
                value.append(text[j])
                j += 1
            if j >= len(text):
                raise ValueError("unterminated rholang string name")
            tokens.append(("STRING", "".join(value)))
            i = j + 1
            continue
        unforgeable = "Unforgeable("
        if text.startswith(unforgeable, i):
            j = text.find(")", i + len(unforgeable))
            if j < 0:
                raise ValueError("unterminated rholang Unforgeable name")
            tokens.append(("UNFORGEABLE", text[i + len(unforgeable):j]))
            i = j + 1
            continue
        if text.startswith("Nil", i):
            tokens.append(("Nil", "Nil"))
            i += 3
            continue
        raise ValueError(f"unsupported rholang storage character at byte {i}: {ch!r}")
    return tokens


class RholangStorageParser:
    def __init__(self, tokens: list[RholangToken]) -> None:
        self.tokens = tokens
        self.pos = 0

    def peek(self) -> RholangToken | None:
        if self.pos >= len(self.tokens):
            return None
        return self.tokens[self.pos]

    def accept(self, kind: str) -> str | None:
        token = self.peek()
        if token is None or token[0] != kind:
            return None
        self.pos += 1
        return token[1]

    def expect(self, kind: str) -> str:
        value = self.accept(kind)
        if value is None:
            found = self.peek()
            raise ValueError(f"expected rholang token {kind}, found {found}")
        return value

    def parse_proc(self) -> Proc:
        parts = [self.parse_atom()]
        while self.accept("|") is not None:
            parts.append(self.parse_atom())
        if len(parts) == 1:
            return parts[0]
        return ProcPar(tuple(parts))

    def parse_atom(self) -> Proc:
        token = self.peek()
        if token is not None and token[0] in ("STRING", "UNFORGEABLE"):
            self.pos += 1
            kind, value = token
            if kind == "UNFORGEABLE":
                name = NameVar(f"$rho:unforgeable:{value}")
            else:
                name = NameVar(f"${value}")
            if self.accept("!") is not None:
                self.expect("(")
                payload = self.parse_proc()
                self.expect(")")
                return ProcSend(name, payload)
            if kind == "UNFORGEABLE":
                return ProcDrop(name)
            raise ValueError("expected '!' after rholang string name")
        if self.accept("Nil") is not None:
            return ProcNil()
        if self.accept("(") is not None:
            proc = self.parse_proc()
            self.expect(")")
            return proc
        raise ValueError(f"expected rholang storage process, found {self.peek()}")

    def finish(self) -> None:
        if self.pos != len(self.tokens):
            raise ValueError(f"unexpected trailing rholang storage token {self.peek()}")


def parse_rholang_storage_proc(text: str) -> Proc:
    parser = RholangStorageParser(tokenize_rholang_storage(text))
    proc = parser.parse_proc()
    parser.finish()
    return proc


def parse_rholang_storage_outputs(output: str) -> ObservationSet:
    marker = "Storage Contents:"
    if marker not in output:
        raise RuntimeError("rholang-cli output missing 'Storage Contents:' section")
    storage = output.split(marker, 1)[1].strip()
    if storage.startswith("The space is empty."):
        return ()
    observations = output_observations(parse_rholang_storage_proc(storage))
    if not observations:
        raise RuntimeError("could not parse unmatched sends from rholang-cli storage output")
    return observations


def rholang_observed_outputs(rholang_cli: str, fixture: str) -> ObservationSet:
    proc = run_cmd([
        rholang_cli,
        "--unmatched-sends-only",
        fixture,
    ])
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return parse_rholang_storage_outputs(proc.stdout)


def main() -> int:
    if len(sys.argv) != 7:
        print(
            "usage: rhocalc_m3_rholang_cli_compare.py "
            "<cetta-bin> <rholang-cli> <cetta-syntax> <cetta-fixture> <rholang-fixture> "
            "<expected-output-set[;expected-output-set...]>",
            file=sys.stderr,
        )
        return 2

    cetta_bin = sys.argv[1]
    rholang_cli = sys.argv[2]
    cetta_syntax = sys.argv[3]
    cetta_fixture = sys.argv[4]
    rholang_fixture = sys.argv[5]
    try:
        expected = expected_observation_sets(sys.argv[6])
        if not expected:
            print("expected output-observation contract is empty", file=sys.stderr)
            return 2
        cetta_outputs = cetta_observed_outputs(cetta_bin, cetta_syntax, cetta_fixture)
        rholang_outputs = rholang_observed_outputs(rholang_cli, rholang_fixture)
    except (RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if cetta_outputs not in expected:
        print("cetta observed outputs are not an allowed overlap outcome",
              file=sys.stderr)
        print(f"expected alternatives: {format_expected_observation_sets(expected)}", file=sys.stderr)
        print(f"observed: {format_observation_set(cetta_outputs)}", file=sys.stderr)
        return 1

    if rholang_outputs not in expected:
        print("rholang-cli observed outputs are not an allowed overlap outcome",
              file=sys.stderr)
        print(f"expected alternatives: {format_expected_observation_sets(expected)}", file=sys.stderr)
        print(f"observed: {format_observation_set(rholang_outputs)}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
