#!/usr/bin/env python3
"""Generate the CeTTa RuleMachineProgram compiler templates from their admitted GSLT.

The generated table is deliberately small: it is the executable image of the
two ``compile-rule-program-block`` rewrites.  The runtime supplies only a generic
template instantiator and checker.  Unsupported rule or operand shapes fail
generation instead of falling back to a handwritten semantic copy.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Sequence
import argparse

import gslt2parse_schema_v1 as sx


class GenerationError(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class Operand:
    kind: str
    symbol: str | None = None
    integer: int = 0


@dataclass(frozen=True, slots=True)
class Operation:
    opcode: str
    operands: tuple[Operand, ...]


@dataclass(frozen=True, slots=True)
class CompiledRule:
    c_name: str
    rule_name: str
    operations: tuple[Operation, ...]


def symbol(term: sx.SExpr, context: str) -> str:
    if not isinstance(term, sx.Symbol):
        raise GenerationError(f"{context}: expected symbol, got {sx.render(term)}")
    return term.text


def application(term: sx.SExpr, head: str, arity: int, context: str) -> tuple[sx.SExpr, ...]:
    if (
        not isinstance(term, tuple)
        or len(term) != arity + 1
        or not isinstance(term[0], sx.Symbol)
        or term[0].text != head
    ):
        raise GenerationError(
            f"{context}: expected {head}/{arity}, got {sx.render(term)}"
        )
    return term


def flatten_code(term: sx.SExpr, context: str) -> tuple[sx.SExpr, ...]:
    code = application(term, "rule-program-code", 1, context)
    cursor = code[1]
    operations: list[sx.SExpr] = []
    while not (isinstance(cursor, sx.Symbol) and cursor.text == "rule-program-nil"):
        cell = application(cursor, "rule-program-cons", 2, context)
        operations.append(cell[1])
        cursor = cell[2]
    if not operations:
        raise GenerationError(f"{context}: instruction program may not be empty")
    return tuple(operations)


def operand(term: sx.SExpr, context: str) -> Operand:
    if isinstance(term, sx.Symbol):
        return Operand("RM_RULE_PROGRAM_OPERAND_SYMBOL", symbol=term.text)
    if isinstance(term, int):
        return Operand("RM_RULE_PROGRAM_OPERAND_INT", integer=term)
    if isinstance(term, sx.Variable) and term.name == "proof":
        return Operand("RM_RULE_PROGRAM_OPERAND_PROOF")
    raise GenerationError(
        f"{context}: only literal symbols, integers, and ?proof are supported; "
        f"got {sx.render(term)}"
    )


def operation(term: sx.SExpr, context: str) -> Operation:
    if isinstance(term, sx.Symbol):
        return Operation(term.text, ())
    if not isinstance(term, tuple) or not term:
        raise GenerationError(f"{context}: malformed operation {sx.render(term)}")
    opcode = symbol(term[0], context)
    operands = tuple(
        operand(item, f"{context}.{opcode}[{index}]")
        for index, item in enumerate(term[1:])
    )
    if len(operands) > 3:
        raise GenerationError(f"{context}: {opcode} exceeds the three-operand ABI")
    return Operation(opcode, operands)


def compile_rule(rule: sx.RuleDecl, c_name: str) -> CompiledRule:
    if rule.body:
        raise GenerationError(
            f"{rule.name}: runtime template generation does not hide body goals"
        )
    head = application(rule.head, "compile-rule-program-block", 2, rule.name)
    application(head[1], "bc-block", 6, f"{rule.name}.source")
    output = application(head[2], "rule-program-block", 4, f"{rule.name}.output")
    operations = tuple(
        operation(item, f"{rule.name}.code")
        for item in flatten_code(output[4], f"{rule.name}.code")
    )
    proof_operands = sum(
        item.kind == "RM_RULE_PROGRAM_OPERAND_PROOF"
        for op in operations
        for item in op.operands
    )
    if proof_operands != 1:
        raise GenerationError(
            f"{rule.name}: expected exactly one proof-symbol occurrence, "
            f"got {proof_operands}"
        )
    return CompiledRule(c_name, rule.name, operations)


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def render_operand(item: Operand) -> str:
    symbol_value = c_string(item.symbol) if item.symbol is not None else "NULL"
    return f"{{{item.kind}, {symbol_value}, INT64_C({item.integer})}}"


def render_header(digest: str, rules: Sequence[CompiledRule]) -> str:
    lines = [
        "/* Generated from the admitted RuleMachine Program GSLT package. */",
        "/* Regenerate with tools/generate_rule_machine_program_v1.py. */",
        "#ifndef CETTA_RULE_MACHINE_PROGRAM_V1_GENERATED_H",
        "#define CETTA_RULE_MACHINE_PROGRAM_V1_GENERATED_H",
        "",
        f'#define RM_RULE_PROGRAM_GSLT_DIGEST {c_string(digest)}',
        f'#define RM_RULE_PROGRAM_GSLT_IDENTITY {c_string("HilbertBFCProgramSpecializationV1-" + digest)}',
        "",
    ]
    for rule in rules:
        lines.append(
            f"static const RMRuleProgramGeneratedOp rm_generated_{rule.c_name}_ops[] = {{"
        )
        for op in rule.operations:
            operands = list(op.operands) + [Operand("RM_RULE_PROGRAM_OPERAND_NONE")] * (
                3 - len(op.operands)
            )
            rendered = ", ".join(render_operand(item) for item in operands)
            lines.append(
                f"    {{{c_string(op.opcode)}, {len(op.operands)}, {{{rendered}}}}},"
            )
        lines.extend(
            [
                "};",
                f"static const uint32_t rm_generated_{rule.c_name}_op_count =",
                f"    (uint32_t)(sizeof(rm_generated_{rule.c_name}_ops) /",
                f"               sizeof(rm_generated_{rule.c_name}_ops[0]));",
                "",
            ]
        )
    lines.extend(["#endif", ""])
    return "\n".join(lines)


def generate(core: Path, rule_program: Path) -> str:
    presentations = sx.admit([core, rule_program])
    rules = {
        rule.name: rule
        for presentation in presentations
        for rule in presentation.rules
    }
    wanted = (
        ("rule_program_axiom", "compile-rule-program-axiom"),
        ("rule_program_inverse_mp", "compile-rule-program-inverse-mp"),
    )
    missing = [name for _, name in wanted if name not in rules]
    if missing:
        raise GenerationError(f"missing semantic compiler rules: {', '.join(missing)}")
    compiled = tuple(compile_rule(rules[name], c_name) for c_name, name in wanted)
    return render_header(sx.package_digest(presentations), compiled)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--program-gslt", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        rendered = generate(args.core, args.program_gslt)
    except (OSError, sx.SchemaError, GenerationError) as error:
        parser.exit(1, f"RuleMachineProgramGenerationError: {error}\n")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
