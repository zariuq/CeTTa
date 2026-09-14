#!/usr/bin/env python3
"""Compile compositional execution-contract GSLTs to C and focused fixtures.

The spec is the authority: value sets, interpretation maps, relational heads,
and fixture wrappers are read from the presentation and compiled into the
generated header, make fragment, and fixtures.  The loader checks internal
coherence (referenced values must be declared, heads must exist in the symbol
table) and refuses constructions it has no compilation scheme for — it never
pins the spec to a previously generated instance.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from gen_semiring_fixtures import SExpr, SpecError, is_form, parse_forms, sexpr


ROOT = Path(__file__).resolve().parents[1]
SPEC = ROOT / "lib" / "gslt_execution_contracts.metta"
SYMBOL_TABLE = ROOT / "src" / "symbol.h"
HEADER = ROOT / "src" / "generated" / "cetta_execution_contracts.generated.h"
MAKE_FRAGMENT = (
    ROOT / "src" / "generated" / "cetta_execution_contracts.generated.mk"
)
FIXTURE = ROOT / "tests" / "generated" / "execution_contracts.metta"
EXPECTED = ROOT / "tests" / "generated" / "execution_contracts.expected"
FOLD_FIXTURE = (
    ROOT / "tests" / "generated" / "determinate_fold_contracts.metta"
)
FOLD_EXPECTED_HE = (
    ROOT / "tests" / "generated" / "determinate_fold_contracts.he.expected"
)
FOLD_EXPECTED_PRIME = (
    ROOT / "tests" / "generated" / "determinate_fold_contracts.prime.expected"
)
FOLD_EXPECTED_PETTA = (
    ROOT / "tests" / "generated" / "determinate_fold_contracts.petta.expected"
)
PRIME_FOLD_FIXTURE = (
    ROOT / "tests" / "generated" / "determinate_fold_prime_demand.metta"
)
PRIME_FOLD_EXPECTED = (
    ROOT / "tests" / "generated" / "determinate_fold_prime_demand.expected"
)
PETTA_FOLD_FIXTURE = (
    ROOT / "tests" / "generated" / "determinate_fold_petta_fallback.metta"
)
PETTA_FOLD_EXPECTED = (
    ROOT / "tests" / "generated" / "determinate_fold_petta_fallback.expected"
)

# The C-side probe vocabulary for streamed rows.  The generator can compile a
# row class only if it knows which probe observes it; anything else is a loud
# refusal, not a silent guess.
ROW_CLASS_PROBES = {"malformed": "!decoded", "cyclic": "cyclic", "valid": None}

# Compiler backend for the register-expression GSLT.  The presentation names
# semantic instructions; this table is their GMP interpretation.  Generated C
# executes these arms directly, so the spec produces the hot-path program.
REGISTER_INSTRUCTION_C: dict[str, tuple[str, str]] = {
    "integer-add": ("exact-integer", "mpz_add(integer_out, left, right);"),
    "integer-subtract": (
        "exact-integer", "mpz_sub(integer_out, left, right);"),
    "integer-multiply": (
        "exact-integer", "mpz_mul(integer_out, left, right);"),
    "integer-equal": ("boolean", "*boolean_out = mpz_cmp(left, right) == 0;"),
    "integer-less": ("boolean", "*boolean_out = mpz_cmp(left, right) < 0;"),
    "integer-greater": ("boolean", "*boolean_out = mpz_cmp(left, right) > 0;"),
    "integer-less-equal": (
        "boolean", "*boolean_out = mpz_cmp(left, right) <= 0;"),
    "integer-greater-equal": (
        "boolean", "*boolean_out = mpz_cmp(left, right) >= 0;"),
    "integer-remainder": (
        "exact-integer",
        "if (mpz_sgn(right) == 0) { return false; } "
        "mpz_tdiv_r(integer_out, left, right);"),
    "integer-floor-modulo": (
        "exact-integer",
        "if (mpz_sgn(right) == 0) { return false; } "
        "mpz_fdiv_r(integer_out, left, right);"),
}

# Tagged-small-integer interpretation of the same semantic instructions.
# Arithmetic promotes to the GMP arm exactly on signed overflow; comparisons
# never promote.  Both programs are emitted from the one instruction list.
REGISTER_INSTRUCTION_SMALL_C: dict[str, str] = {
    "integer-add": (
        "*promote_out = __builtin_add_overflow(left, right, integer_out);"),
    "integer-subtract": (
        "*promote_out = __builtin_sub_overflow(left, right, integer_out);"),
    "integer-multiply": (
        "*promote_out = __builtin_mul_overflow(left, right, integer_out);"),
    "integer-equal": "*boolean_out = left == right;",
    "integer-less": "*boolean_out = left < right;",
    "integer-greater": "*boolean_out = left > right;",
    "integer-less-equal": "*boolean_out = left <= right;",
    "integer-greater-equal": "*boolean_out = left >= right;",
    "integer-remainder": (
        "if (right == 0) { return false; } "
        "*integer_out = (left == INT64_MIN && right == -1) "
        "? 0 : left % right;"),
    "integer-floor-modulo": (
        "if (right == 0) { return false; } "
        "if (left == INT64_MIN && right == -1) { "
        "*integer_out = 0; "
        "} else { "
        "*integer_out = left % right; "
        "if (*integer_out != 0 && "
        "((*integer_out < 0) != (right < 0))) *integer_out += right; "
        "}"),
}

REGISTER_INSTRUCTION_ATOM_C: dict[str, tuple[str, str]] = {
    "atom-equal": ("boolean", "*boolean_out = atom_eq(left, right);"),
}

# Native instruction schemes for the prepared-pure intrinsic leaf.  The
# authored presentation selects concrete heads and operand disciplines; this
# set states only which abstract instructions the C backend can execute.
PREPARED_PURE_INTRINSIC_INSTRUCTIONS = {
    "grounded-dispatch",
    "deconstruct-nonempty-expression",
    "construct-expression-cons",
    "concatenate-expressions",
}
PREPARED_PURE_INTRINSIC_OPERAND_DISCIPLINES = {"strict-all"}

# Representation interpretation for value-allocation.  The presentation uses
# semantic leaf classes; this backend maps them to CeTTa's GroundedKind tags.
GROUNDED_LEAF_KIND_C: dict[str, str] = {
    "integer": "GV_INT",
    "float": "GV_FLOAT",
    "big-integer": "GV_BIGINT",
    "rational": "GV_RATIONAL",
}

# Identity-bearing resource classes are semantic declarations in the
# resource-lifetime component.  This backend mapping is the representation
# interpretation which turns those declarations into GroundedKind arms.
IDENTITY_GROUNDED_KIND_C: dict[str, str] = {
    "space": "GV_SPACE",
    "state": "GV_STATE",
    "capture": "GV_CAPTURE",
    "foreign": "GV_FOREIGN",
}


def register_instruction_result_kind(instruction: str) -> str:
    if instruction in REGISTER_INSTRUCTION_C:
        return REGISTER_INSTRUCTION_C[instruction][0]
    if instruction in REGISTER_INSTRUCTION_ATOM_C:
        return REGISTER_INSTRUCTION_ATOM_C[instruction][0]
    raise KeyError(instruction)

# Inline fixture templates exercising individual relational heads beyond the
# $relation instance.  Heads without an entry are covered by the classifier
# unit test alone.
HEAD_FIXTURE_TEMPLATES: dict[str, SExpr] = {
    "get-atoms": ["get-atoms", "&effect-space"],
}


def find_form(forms: list[SExpr], head: str, name: str) -> list[SExpr]:
    for form in forms:
        if isinstance(form, list) and form[:2] == [head, name]:
            return form
    raise SpecError(f"missing ({head} {name} ...)")


def entries(form: list[SExpr], head: str) -> list[list[SExpr]]:
    return [entry for entry in form[2:] if is_form(entry, head)]  # type: ignore[misc]


def atom_text(value: SExpr, context: str) -> str:
    if not isinstance(value, str):
        raise SpecError(f"{context} must be an atom: {sexpr(value)}")
    return value


def validate_no_question_predicates(value: SExpr) -> None:
    if isinstance(value, str):
        if value.endswith("?"):
            raise SpecError(f"predicate names may not end in '?': {value}")
        return
    for item in value:
        validate_no_question_predicates(item)


def declared_values(form: list[SExpr], what: str) -> list[str]:
    values = [atom_text(item[1], what)
              for item in entries(form, "value") if len(item) == 2]
    if len(set(values)) != len(values) or not values:
        raise SpecError(f"{what} list must be nonempty and duplicate-free")
    return values


def c_enum(name: str) -> str:
    return name.upper().replace("-", "_").replace(":", "_")


def load_symbol_fields() -> dict[str, str]:
    """Map concrete symbol spellings to their g_builtin_syms field names."""
    fields: dict[str, str] = {}
    for match in re.finditer(r'X\((\w+),\s*"([^"]*)"\)', SYMBOL_TABLE.read_text()):
        fields.setdefault(match.group(2), match.group(1))
    if not fields:
        raise SpecError(f"no symbol entries found in {SYMBOL_TABLE}")
    return fields


class Spec:
    def __init__(self, text: str) -> None:
        forms = parse_forms(text)
        for form in forms:
            validate_no_question_predicates(form)
        self.digest = hashlib.sha256(text.encode()).hexdigest()

        effect = find_form(forms, "gslt", "relational-may-effect")
        rows = find_form(forms, "gslt", "streamed-row-disposition")
        observation = find_form(forms, "gslt", "observation-profile")
        lifetime = find_form(forms, "gslt", "resource-lifetime")
        prepared = find_form(forms, "gslt", "determinate-equation-continuation")
        total = find_form(forms, "gslt", "total-expression-normalization")
        allocation = find_form(forms, "gslt", "value-allocation")
        register = find_form(forms, "gslt", "register-expression-execution")
        intrinsic = find_form(
            forms, "gslt", "prepared-pure-intrinsic-execution")
        fold = find_form(forms, "gslt", "determinate-fold-continuation")
        pure_call = find_form(
            forms, "gslt", "determinate-pure-call-continuation")
        frames = find_form(forms, "gslt", "evaluator-root-frames")
        composition = find_form(forms, "composition", "cetta-execution-contracts")
        native = find_form(forms, "interpretation", "cetta-native-execution")
        fixtures = find_form(forms, "fixtures", "relational-admission")
        fold_fixtures = find_form(
            forms, "fixtures", "determinate-fold-semantics")

        field_kinds = {atom_text(item[1], "root field kind")
                       for item in entries(frames, "root-field-kind")
                       if len(item) == 2}
        if not field_kinds:
            raise SpecError("evaluator-root-frames declares no field kinds")
        self.root_frame_kinds: list[tuple[str, list[tuple[str, str]]]] = []
        for entry in entries(frames, "frame-kind"):
            if len(entry) < 3:
                raise SpecError(f"malformed frame-kind: {sexpr(entry)}")
            kind_name = atom_text(entry[1], "frame kind")
            if not re.fullmatch(r"[a-z][a-z0-9_]*", kind_name):
                raise SpecError(f"frame kind is not a C identifier: {kind_name}")
            fields: list[tuple[str, str]] = []
            for field in entry[2:]:
                if not (isinstance(field, list) and len(field) == 3 and
                        field[0] == "field"):
                    continue
                fname = atom_text(field[1], "frame field name")
                fkind = atom_text(field[2], "frame field kind")
                if not re.fullmatch(r"[a-z][a-z0-9_]*", fname):
                    raise SpecError(f"frame field is not a C identifier: {fname}")
                if fkind not in field_kinds:
                    raise SpecError(
                        f"frame {kind_name} field {fname} has undeclared "
                        f"field kind {fkind}")
                fields.append((fname, fkind))
            if not fields:
                raise SpecError(f"frame kind {kind_name} declares no fields")
            self.root_frame_kinds.append((kind_name, fields))
        if not self.root_frame_kinds:
            raise SpecError("at least one root frame kind is required")

        component_entry = next(iter(entries(composition, "components")), None)
        component_names = {form[1] for form in forms
                           if isinstance(form, list) and form[:1] == ["gslt"]}
        if component_entry is None or set(component_entry[1:]) != component_names:
            raise SpecError(
                "composition components must list exactly the declared gslts")

        # Effect algebra: pure is the fold unit (rule atom-effect); every
        # other declared value becomes a joinable flag bit.
        self.effect_values = declared_values(effect, "effect value")
        if "pure" not in self.effect_values:
            raise SpecError("effect algebra must declare the unit value pure")
        self.effect_flags = [v for v in self.effect_values if v != "pure"]

        self.row_values = declared_values(rows, "row disposition")
        self.visibility_values = declared_values(observation, "visibility")
        self.transitions = [atom_text(item[1], "lifetime transition")
                            for item in entries(lifetime, "transition")
                            if len(item) >= 2]
        if len(set(self.transitions)) != len(self.transitions) or not self.transitions:
            raise SpecError("lifetime transitions must be nonempty and unique")
        self.identity_grounded_kinds = [
            atom_text(item[1], "identity-bearing grounded kind")
            for item in entries(lifetime, "identity-bearing-grounded-kind")
            if len(item) == 2
        ]
        if (not self.identity_grounded_kinds or
                len(set(self.identity_grounded_kinds)) !=
                len(self.identity_grounded_kinds)):
            raise SpecError(
                "identity-bearing grounded kinds must be nonempty and unique")
        unknown_identity_kinds = (
            set(self.identity_grounded_kinds) -
            set(IDENTITY_GROUNDED_KIND_C))
        if unknown_identity_kinds:
            raise SpecError(
                "no native GroundedKind for identity-bearing classes: "
                f"{sorted(unknown_identity_kinds)}")
        self.identity_grounded_kind_tags = [
            IDENTITY_GROUNDED_KIND_C[kind]
            for kind in self.identity_grounded_kinds
        ]
        self.thread_local_resource_symbols = [
            atom_text(item[1], "thread-local resource symbol")
            for item in entries(lifetime, "thread-local-resource-symbol")
            if len(item) == 2
        ]
        if (not self.thread_local_resource_symbols or
                len(set(self.thread_local_resource_symbols)) !=
                len(self.thread_local_resource_symbols)):
            raise SpecError(
                "thread-local resource symbols must be nonempty and unique")
        self.prepared_evidence = declared_values(
            prepared, "prepared-equation evidence")
        self.total_evidence = declared_values(
            total, "total-expression evidence")
        if set(self.total_evidence) != {
                "exact-integer-leaf", "total-integer-head"}:
            raise SpecError(
                "total-expression evidence must name leaf and head proofs")
        self.allocation_classes = declared_values(
            allocation, "value-allocation class")
        if set(self.allocation_classes) != {
                "arena-owned-leaf", "implicit-shared"}:
            raise SpecError(
                "value-allocation classes lack a native compilation scheme")
        self.numeric_leaf_kinds = [
            atom_text(item[1], "numeric leaf kind")
            for item in entries(allocation, "numeric-leaf-kind")
            if len(item) == 2
        ]
        if (not self.numeric_leaf_kinds or
                len(set(self.numeric_leaf_kinds)) !=
                len(self.numeric_leaf_kinds)):
            raise SpecError(
                "numeric leaf kinds must be nonempty and duplicate-free")
        unknown_leaf_kinds = (
            set(self.numeric_leaf_kinds) - set(GROUNDED_LEAF_KIND_C))
        if unknown_leaf_kinds:
            raise SpecError(
                "no native GroundedKind for numeric leaf classes: "
                f"{sorted(unknown_leaf_kinds)}")
        self.register_result_kinds = declared_values(
            register, "register-expression result kind")
        self.register_operand_disciplines = [
            atom_text(item[1], "register-expression operand discipline")
            for item in entries(register, "operand-discipline")
            if len(item) == 2
        ]
        if (not self.register_operand_disciplines or
                len(set(self.register_operand_disciplines)) !=
                len(self.register_operand_disciplines)):
            raise SpecError(
                "register operand disciplines must be nonempty and unique")
        self.fold_control_kinds = declared_values(
            fold, "determinate-fold control kind")
        self.fold_operand_roles = [
            atom_text(item[1], "determinate-fold operand role")
            for item in entries(fold, "operand-role")
            if len(item) == 2
        ]
        if (not self.fold_operand_roles or
                len(set(self.fold_operand_roles)) !=
                len(self.fold_operand_roles)):
            raise SpecError(
                "determinate-fold operand roles must be nonempty and unique")
        self.fold_control_operands: dict[str, list[str]] = {}
        for entry in entries(fold, "control-operands"):
            if len(entry) < 3:
                raise SpecError(
                    f"malformed control-operands: {sexpr(entry)}")
            control = atom_text(entry[1], "control-operands control")
            roles = [
                atom_text(role, "control-operands role")
                for role in entry[2:]
            ]
            if control not in self.fold_control_kinds:
                raise SpecError(
                    f"control-operands names undeclared control {control}")
            unknown_roles = set(roles) - set(self.fold_operand_roles)
            if unknown_roles:
                raise SpecError(
                    "control-operands names undeclared roles: "
                    f"{sorted(unknown_roles)}")
            if control in self.fold_control_operands:
                raise SpecError(
                    f"duplicate control-operands for {control}")
            self.fold_control_operands[control] = roles
        if set(self.fold_control_operands) != set(self.fold_control_kinds):
            missing = (
                set(self.fold_control_kinds) -
                set(self.fold_control_operands))
            raise SpecError(
                "control-operands must cover every determinate-fold "
                f"control; missing {sorted(missing)}")
        self.prepared_intrinsic_instructions = [
            atom_text(item[1], "prepared-pure intrinsic instruction")
            for item in entries(intrinsic, "instruction")
            if len(item) == 2
        ]
        if (not self.prepared_intrinsic_instructions or
                len(set(self.prepared_intrinsic_instructions)) !=
                len(self.prepared_intrinsic_instructions)):
            raise SpecError(
                "prepared-pure intrinsic instructions must be nonempty "
                "and unique")
        unknown_intrinsic_instructions = (
            set(self.prepared_intrinsic_instructions) -
            PREPARED_PURE_INTRINSIC_INSTRUCTIONS)
        if unknown_intrinsic_instructions:
            raise SpecError(
                "no native compilation scheme for prepared-pure intrinsic "
                f"instructions: {sorted(unknown_intrinsic_instructions)}")
        self.prepared_intrinsic_operand_disciplines = [
            atom_text(item[1], "prepared-pure intrinsic operand discipline")
            for item in entries(intrinsic, "operand-discipline")
            if len(item) == 2
        ]
        if (not self.prepared_intrinsic_operand_disciplines or
                len(set(self.prepared_intrinsic_operand_disciplines)) !=
                len(self.prepared_intrinsic_operand_disciplines)):
            raise SpecError(
                "prepared-pure intrinsic operand disciplines must be "
                "nonempty and unique")
        unknown_intrinsic_disciplines = (
            set(self.prepared_intrinsic_operand_disciplines) -
            PREPARED_PURE_INTRINSIC_OPERAND_DISCIPLINES)
        if unknown_intrinsic_disciplines:
            raise SpecError(
                "no native compilation scheme for prepared-pure intrinsic "
                "operand disciplines: "
                f"{sorted(unknown_intrinsic_disciplines)}")
        self.pure_call_modes = declared_values(
            pure_call, "determinate-pure-call mode")
        if self.pure_call_modes != ["eager", "call-by-need"]:
            raise SpecError(
                "determinate-pure-call modes must be eager, call-by-need")
        self.pure_call_pattern_kinds = [
            atom_text(item[1], "determinate-pure-call pattern kind")
            for item in entries(pure_call, "pattern-kind")
            if len(item) == 2
        ]
        if self.pure_call_pattern_kinds != [
                "variable", "atom", "expression"]:
            raise SpecError(
                "determinate-pure-call pattern kinds must be variable, "
                "atom, expression")
        if entries(pure_call, "selection-judgment") != [
                ["selection-judgment", "whnf-disjoint"]]:
            raise SpecError(
                "determinate-pure-call selection must use whnf-disjoint")
        self.register_instructions = [
            atom_text(item[1], "register-expression instruction")
            for item in entries(register, "instruction") if len(item) == 2
        ]
        if (len(set(self.register_instructions)) !=
                len(self.register_instructions) or
                not self.register_instructions):
            raise SpecError(
                "register-expression instructions must be nonempty and unique")
        unknown_instructions = (
            set(self.register_instructions) -
            (set(REGISTER_INSTRUCTION_C) |
             set(REGISTER_INSTRUCTION_ATOM_C)))
        if unknown_instructions:
            raise SpecError(
                "no native compilation scheme for register instructions: "
                f"{sorted(unknown_instructions)}")
        integer_instructions = (
            set(self.register_instructions) & set(REGISTER_INSTRUCTION_C))
        unknown_small_instructions = (
            integer_instructions - set(REGISTER_INSTRUCTION_SMALL_C))
        if unknown_small_instructions:
            raise SpecError(
                "no tagged-integer compilation scheme for register "
                f"instructions: {sorted(unknown_small_instructions)}")
        self.register_instruction_disciplines: dict[str, str] = {}
        for entry in entries(register, "instruction-discipline"):
            if len(entry) != 3:
                raise SpecError(
                    f"malformed instruction-discipline: {sexpr(entry)}")
            instruction = atom_text(
                entry[1], "instruction-discipline instruction")
            discipline = atom_text(
                entry[2], "instruction-discipline operand discipline")
            if instruction not in self.register_instructions:
                raise SpecError(
                    f"instruction-discipline names undeclared instruction "
                    f"{instruction}")
            if discipline not in self.register_operand_disciplines:
                raise SpecError(
                    f"instruction-discipline names undeclared discipline "
                    f"{discipline}")
            if instruction in self.register_instruction_disciplines:
                raise SpecError(
                    f"duplicate instruction-discipline for {instruction}")
            self.register_instruction_disciplines[instruction] = discipline
        if set(self.register_instruction_disciplines) != \
                set(self.register_instructions):
            missing = (set(self.register_instructions) -
                       set(self.register_instruction_disciplines))
            raise SpecError(
                "instruction-discipline must cover every register "
                f"instruction; missing {sorted(missing)}")

        # Native interpretation: derive maps, checking only that references
        # resolve to declared values.
        if entries(native, "query-default") != [["query-default", "pure"]]:
            raise SpecError("native query default must be the fold unit pure")
        if entries(native, "pull-admission") != [["pull-admission", "pure"]]:
            raise SpecError("pull admission must require a pure template")

        symbol_fields = load_symbol_fields()
        unknown_resource_symbols = (
            set(self.thread_local_resource_symbols) - set(symbol_fields))
        if unknown_resource_symbols:
            raise SpecError(
                "thread-local resource symbols have no symbol-table entry: "
                f"{sorted(unknown_resource_symbols)}")
        self.thread_local_resource_symbol_fields = [
            symbol_fields[name] for name in self.thread_local_resource_symbols
        ]
        self.heads: list[tuple[str, str, str]] = []
        for entry in entries(native, "query-head"):
            if len(entry) != 3:
                raise SpecError(f"malformed query-head: {sexpr(entry)}")
            name = atom_text(entry[1], "query head")
            effect_value = atom_text(entry[2], "query-head effect")
            if effect_value not in self.effect_flags:
                raise SpecError(
                    f"query-head {name} names undeclared effect {effect_value}")
            if name not in symbol_fields:
                raise SpecError(
                    f"query-head {name} has no entry in {SYMBOL_TABLE.name}")
            self.heads.append((name, symbol_fields[name], effect_value))
        if not self.heads:
            raise SpecError("at least one relational query head is required")

        self.total_integer_heads: list[tuple[str, str, int]] = []
        for entry in entries(native, "total-integer-head"):
            if len(entry) != 3:
                raise SpecError(f"malformed total-integer-head: {sexpr(entry)}")
            name = atom_text(entry[1], "total integer head")
            arity_text = atom_text(entry[2], "total integer arity")
            if name not in symbol_fields:
                raise SpecError(
                    f"total integer head {name} has no symbol-table entry")
            if not arity_text.isdigit() or not 1 <= int(arity_text) <= 16:
                raise SpecError("total integer arity must be in [1, 16]")
            self.total_integer_heads.append(
                (name, symbol_fields[name], int(arity_text)))
        if not self.total_integer_heads:
            raise SpecError("at least one total integer head is required")

        self.grounded_allocations: dict[str, str] = {}
        for entry in entries(native, "grounded-allocation"):
            if len(entry) != 3:
                raise SpecError(
                    f"malformed grounded-allocation: {sexpr(entry)}")
            leaf_kind = atom_text(entry[1], "grounded allocation leaf kind")
            allocation_class = atom_text(
                entry[2], "grounded allocation class")
            if leaf_kind not in self.numeric_leaf_kinds:
                raise SpecError(
                    f"grounded-allocation names undeclared leaf {leaf_kind}")
            if allocation_class not in self.allocation_classes:
                raise SpecError(
                    f"grounded-allocation names undeclared class "
                    f"{allocation_class}")
            if leaf_kind in self.grounded_allocations:
                raise SpecError(
                    f"duplicate grounded-allocation for {leaf_kind}")
            self.grounded_allocations[leaf_kind] = allocation_class
        if set(self.grounded_allocations) != set(self.numeric_leaf_kinds):
            raise SpecError(
                "native grounded-allocation must cover every numeric leaf")
        self.arena_owned_grounded_kinds = [
            GROUNDED_LEAF_KIND_C[kind]
            for kind in self.numeric_leaf_kinds
            if self.grounded_allocations[kind] == "arena-owned-leaf"
        ]
        if not self.arena_owned_grounded_kinds:
            raise SpecError(
                "native value allocation needs an arena-owned leaf class")

        self.register_heads: list[tuple[str, str, int, str, str]] = []
        for entry in entries(native, "register-head"):
            if len(entry) != 5:
                raise SpecError(f"malformed register-head: {sexpr(entry)}")
            name = atom_text(entry[1], "register head")
            arity_text = atom_text(entry[2], "register head arity")
            result_kind = atom_text(entry[3], "register result kind")
            instruction = atom_text(entry[4], "register instruction")
            if name not in symbol_fields:
                raise SpecError(
                    f"register head {name} has no symbol-table entry")
            if not arity_text.isdigit() or not 1 <= int(arity_text) <= 16:
                raise SpecError("register head arity must be in [1, 16]")
            if result_kind not in self.register_result_kinds:
                raise SpecError(
                    f"register head {name} names undeclared result kind "
                    f"{result_kind}")
            if instruction not in self.register_instructions:
                raise SpecError(
                    f"register head {name} names undeclared instruction "
                    f"{instruction}")
            expected_kind = register_instruction_result_kind(instruction)
            if result_kind != expected_kind:
                raise SpecError(
                    f"register head {name} gives {instruction} result "
                    f"{result_kind}, expected {expected_kind}")
            self.register_heads.append(
                (name, symbol_fields[name], int(arity_text), result_kind,
                 instruction))
        if not self.register_heads:
            raise SpecError("at least one register head is required")
        if len({(field, arity) for _, field, arity, _, _ in self.register_heads}) != \
                len(self.register_heads):
            raise SpecError("register heads must be unique by symbol and arity")

        self.prepared_intrinsic_heads: list[
            tuple[str, str, int, str, str]
        ] = []
        for entry in entries(native, "prepared-pure-intrinsic-head"):
            if len(entry) != 5:
                raise SpecError(
                    f"malformed prepared-pure-intrinsic-head: "
                    f"{sexpr(entry)}")
            name = atom_text(entry[1], "prepared-pure intrinsic head")
            arity_text = atom_text(
                entry[2], "prepared-pure intrinsic arity")
            discipline = atom_text(
                entry[3], "prepared-pure intrinsic operand discipline")
            instruction = atom_text(
                entry[4], "prepared-pure intrinsic instruction")
            if name not in symbol_fields:
                raise SpecError(
                    f"prepared-pure intrinsic head {name} has no "
                    "symbol-table entry")
            if not arity_text.isdigit() or not 1 <= int(arity_text) <= 16:
                raise SpecError(
                    "prepared-pure intrinsic arity must be in [1, 16]")
            if discipline not in self.prepared_intrinsic_operand_disciplines:
                raise SpecError(
                    f"prepared-pure intrinsic head {name} names undeclared "
                    f"operand discipline {discipline}")
            if instruction not in self.prepared_intrinsic_instructions:
                raise SpecError(
                    f"prepared-pure intrinsic head {name} names undeclared "
                    f"instruction {instruction}")
            if (instruction == "deconstruct-nonempty-expression" and
                    int(arity_text) != 1):
                raise SpecError(
                    "deconstruct-nonempty-expression requires arity one")
            if (instruction in {
                    "construct-expression-cons",
                    "concatenate-expressions",
                    } and int(arity_text) != 2):
                raise SpecError(
                    f"{instruction} requires arity two")
            self.prepared_intrinsic_heads.append((
                name, symbol_fields[name], int(arity_text), discipline,
                instruction))
        if not self.prepared_intrinsic_heads:
            raise SpecError(
                "at least one prepared-pure intrinsic head is required")
        if len({(field, arity) for _, field, arity, _, _
                in self.prepared_intrinsic_heads}) != \
                len(self.prepared_intrinsic_heads):
            raise SpecError(
                "prepared-pure intrinsic heads must be unique by symbol "
                "and arity")
        if {instruction for _, _, _, _, instruction
                in self.prepared_intrinsic_heads} != \
                set(self.prepared_intrinsic_instructions):
            raise SpecError(
                "native prepared-pure intrinsic heads must cover every "
                "declared intrinsic instruction")

        self.fold_control_heads: list[tuple[str, str, int, str]] = []
        for entry in entries(native, "fold-control-head"):
            if len(entry) != 4:
                raise SpecError(
                    f"malformed fold-control-head: {sexpr(entry)}")
            name = atom_text(entry[1], "fold control head")
            arity_text = atom_text(entry[2], "fold control arity")
            control = atom_text(entry[3], "fold control kind")
            if name not in symbol_fields:
                raise SpecError(
                    f"fold control head {name} has no symbol-table entry")
            if not arity_text.isdigit() or not 1 <= int(arity_text) <= 16:
                raise SpecError("fold control arity must be in [1, 16]")
            if control not in self.fold_control_kinds:
                raise SpecError(
                    f"fold control head {name} names undeclared control "
                    f"{control}")
            if int(arity_text) != len(self.fold_control_operands[control]):
                raise SpecError(
                    f"fold control head {name} arity does not match the "
                    f"declared operands of {control}")
            self.fold_control_heads.append(
                (name, symbol_fields[name], int(arity_text), control))
        if not self.fold_control_heads:
            raise SpecError("at least one fold control head is required")
        if len({(field, arity) for _, field, arity, _
                in self.fold_control_heads}) != len(self.fold_control_heads):
            raise SpecError(
                "fold control heads must be unique by symbol and arity")

        self.sequence_representations: list[
            tuple[str, str, str, str]
        ] = []
        for entry in entries(native, "sequence-representation"):
            if len(entry) != 5:
                raise SpecError(
                    f"malformed sequence-representation: {sexpr(entry)}")
            key = atom_text(entry[1], "sequence representation key")
            materializer = atom_text(
                entry[2], "sequence representation materializer")
            empty = atom_text(
                entry[3], "sequence representation empty constructor")
            cons = atom_text(
                entry[4], "sequence representation cons constructor")
            if not all((key, materializer, empty, cons)):
                raise SpecError(
                    "sequence representation names must be nonempty")
            if empty == cons:
                raise SpecError(
                    "sequence empty and cons constructors must differ")
            self.sequence_representations.append(
                (key, materializer, empty, cons))
        if not self.sequence_representations:
            raise SpecError(
                "at least one sequence representation is required")
        if len({key for key, _, _, _ in self.sequence_representations}) != \
                len(self.sequence_representations):
            raise SpecError("sequence representation keys must be unique")

        representation_keys = {
            key for key, _, _, _ in self.sequence_representations
        }
        self.sequence_fold_consumers: list[tuple[str, str, int, int]] = []
        for entry in entries(native, "sequence-fold-consumer"):
            if len(entry) != 5:
                raise SpecError(
                    f"malformed sequence-fold-consumer: {sexpr(entry)}")
            key = atom_text(entry[1], "sequence fold consumer key")
            consumer = atom_text(entry[2], "sequence fold consumer")
            arity_text = atom_text(entry[3], "sequence fold consumer arity")
            operand_text = atom_text(
                entry[4], "sequence fold consumer represented operand")
            if key not in representation_keys:
                raise SpecError(
                    f"sequence fold consumer names unknown representation "
                    f"{key}")
            if not consumer:
                raise SpecError("sequence fold consumer name must be nonempty")
            if not arity_text.isdigit() or not operand_text.isdigit():
                raise SpecError(
                    "sequence fold consumer arity and operand must be "
                    "nonnegative integers")
            arity = int(arity_text)
            operand = int(operand_text)
            if arity != 5 or operand != 0:
                raise SpecError(
                    "sequence fold consumer must have foldl arity 5 with "
                    "represented operand 0")
            self.sequence_fold_consumers.append(
                (key, consumer, arity, operand))
        if {key for key, _, _, _ in self.sequence_fold_consumers} != \
                representation_keys:
            raise SpecError(
                "sequence fold consumers must cover every representation")
        if len({(consumer, arity) for _, consumer, arity, _
                in self.sequence_fold_consumers}) != \
                len(self.sequence_fold_consumers):
            raise SpecError(
                "sequence fold consumers must be unique by head and arity")

        self.sequence_fold_targets: list[tuple[str, str]] = []
        for entry in entries(native, "sequence-fold-target"):
            if len(entry) != 3:
                raise SpecError(
                    f"malformed sequence-fold-target: {sexpr(entry)}")
            key = atom_text(entry[1], "sequence fold target key")
            target = atom_text(entry[2], "sequence fold target")
            if not key or not target:
                raise SpecError(
                    "sequence fold target names must be nonempty")
            self.sequence_fold_targets.append((key, target))
        if {key for key, _ in self.sequence_fold_targets} != \
                representation_keys or \
                len(self.sequence_fold_targets) != len(representation_keys):
            raise SpecError(
                "sequence fold targets must cover representation keys "
                "exactly once")

        erasure_modes = {"unwrap", "recursive-all", "scalar-all"}
        self.sequence_erasures: list[
            tuple[str, str, str, str, int, int]
        ] = []
        for entry in entries(native, "sequence-erasure"):
            if len(entry) != 7:
                raise SpecError(
                    f"malformed sequence-erasure: {sexpr(entry)}")
            key = atom_text(entry[1], "sequence erasure key")
            source = atom_text(entry[2], "sequence erasure source")
            target = atom_text(entry[3], "sequence erasure target")
            mode = atom_text(entry[4], "sequence erasure mode")
            minimum_text = atom_text(
                entry[5], "sequence erasure minimum arity")
            maximum_text = atom_text(
                entry[6], "sequence erasure maximum arity")
            if key not in representation_keys:
                raise SpecError(
                    f"sequence erasure names unknown representation {key}")
            if not source or not target:
                raise SpecError(
                    "sequence erasure names must be nonempty")
            if mode not in erasure_modes:
                raise SpecError(
                    f"sequence erasure has unknown mode {mode}")
            if not minimum_text.isdigit() or not maximum_text.isdigit():
                raise SpecError(
                    "sequence erasure arities must be nonnegative integers")
            minimum = int(minimum_text)
            maximum = int(maximum_text)
            if minimum > maximum or maximum > 16:
                raise SpecError(
                    "sequence erasure arity interval must lie in [0, 16]")
            if mode == "unwrap" and (
                    target != "identity" or minimum != 1 or maximum != 1):
                raise SpecError(
                    "unwrap sequence erasure must target identity at arity 1")
            if mode != "unwrap" and target == "identity":
                raise SpecError(
                    "non-unwrap sequence erasure needs an operation target")
            self.sequence_erasures.append(
                (key, source, target, mode, minimum, maximum))
        if not self.sequence_erasures:
            raise SpecError("at least one sequence erasure is required")
        if len({(key, source) for key, source, _, _, _, _
                in self.sequence_erasures}) != len(self.sequence_erasures):
            raise SpecError(
                "sequence erasures must be unique by representation and source")

        user_head_analysis = entries(native, "user-head-analysis")
        if user_head_analysis != [["user-head-analysis", "revision-keyed",
                                   "least-fixed-point"]]:
            raise SpecError(
                "user-head analysis must be revision-keyed least-fixed-point")
        user_head_uncertain = entries(native, "user-head-uncertain")
        if len(user_head_uncertain) != 1 or len(user_head_uncertain[0]) != 2:
            raise SpecError("native interpretation needs one user-head-uncertain")
        self.user_head_uncertain = atom_text(
            user_head_uncertain[0][1], "uncertain user-head effect")
        if self.user_head_uncertain not in self.effect_flags:
            raise SpecError(
                "uncertain user-head effect must be a non-unit effect")
        user_head_inert = entries(native, "user-head-inert")
        if user_head_inert != [["user-head-inert", "pure"]]:
            raise SpecError("inert user-head symbols must map to pure")
        inert_children = entries(native, "inert-expression-children")
        if inert_children != [["inert-expression-children", "opaque"]]:
            raise SpecError(
                "native execution must keep inert-expression children opaque")
        self.opaque_expression_heads: list[tuple[str, str]] = []
        for entry in entries(native, "opaque-expression-head"):
            if len(entry) != 2:
                raise SpecError(
                    f"malformed opaque-expression-head: {sexpr(entry)}")
            name = atom_text(entry[1], "opaque expression head")
            if name not in symbol_fields:
                raise SpecError(
                    f"opaque expression head {name} has no symbol-table entry")
            self.opaque_expression_heads.append(
                (name, symbol_fields[name]))
        if not self.opaque_expression_heads:
            raise SpecError("at least one opaque expression head is required")
        if len({field for _, field in self.opaque_expression_heads}) != \
                len(self.opaque_expression_heads):
            raise SpecError("opaque expression heads must be unique")
        relational_fields = {field for _, field, _ in self.heads}
        overlap = relational_fields & {
            field for _, field in self.opaque_expression_heads}
        if overlap:
            raise SpecError(
                "an opaque expression head cannot also be relational: "
                f"{sorted(overlap)}")

        self.row_map: dict[str, str] = {}
        for entry in entries(native, "row"):
            if len(entry) != 3:
                raise SpecError(f"malformed row entry: {sexpr(entry)}")
            row_class = atom_text(entry[1], "row class")
            disposition = atom_text(entry[2], "row disposition")
            if row_class not in ROW_CLASS_PROBES:
                raise SpecError(
                    f"no compilation scheme for row class {row_class}")
            if disposition not in self.row_values:
                raise SpecError(f"undeclared row disposition {disposition}")
            self.row_map[row_class] = disposition
        if set(self.row_map) != set(ROW_CLASS_PROBES):
            raise SpecError(
                f"native row interpretation incomplete: {sorted(self.row_map)}")

        self.observations: dict[tuple[str, str], str] = {}
        self.events: list[str] = []
        for entry in entries(native, "observation"):
            if len(entry) != 4:
                raise SpecError(f"malformed observation: {sexpr(entry)}")
            event = atom_text(entry[1], "observation event")
            profile = atom_text(entry[2], "observation profile")
            action = atom_text(entry[3], "observation action")
            if action not in self.visibility_values:
                raise SpecError(f"undeclared visibility {action}")
            if event not in self.events:
                self.events.append(event)
            self.observations[(event, profile)] = action
        for event in self.events:
            for profile in ("default", "diagnostic"):
                if (event, profile) not in self.observations:
                    raise SpecError(
                        f"observation {event} missing {profile} profile")

        self.environments = {
            atom_text(entry[1], "environment role"):
                atom_text(entry[2], "environment name")
            for entry in entries(native, "environment") if len(entry) == 3
        }
        for role in ("diagnostic", "diagnostic-interval"):
            if role not in self.environments:
                raise SpecError(f"missing environment role {role}")
        for name in self.environments.values():
            if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
                raise SpecError(f"invalid environment name: {name}")

        self.lifetime_map: dict[str, str] = {}
        for entry in entries(native, "lifetime"):
            if len(entry) != 3:
                raise SpecError(f"malformed lifetime entry: {sexpr(entry)}")
            transition = atom_text(entry[1], "lifetime event")
            if transition not in self.transitions:
                raise SpecError(f"undeclared lifetime transition {transition}")
            self.lifetime_map[transition] = atom_text(entry[2], "lifetime result")
        if not self.lifetime_map:
            raise SpecError("native lifetime interpretation is empty")

        plan_requirements = entries(native, "prepared-equation-requires")
        call_requirements = entries(native, "prepared-call-requires")
        register_requirements = entries(
            native, "prepared-register-step-requires")
        recursive_requirements = entries(
            native, "prepared-register-recursion-requires")
        register_limits = entries(native, "prepared-register-limit")
        if len(plan_requirements) != 1 or len(plan_requirements[0]) < 2:
            raise SpecError(
                "native interpretation needs one nonempty "
                "prepared-equation-requires entry")
        if len(call_requirements) != 1 or len(call_requirements[0]) < 2:
            raise SpecError(
                "native interpretation needs one nonempty "
                "prepared-call-requires entry")
        if len(register_requirements) != 1 or len(register_requirements[0]) < 2:
            raise SpecError(
                "native interpretation needs one nonempty "
                "prepared-register-step-requires entry")
        if len(recursive_requirements) != 1 or len(recursive_requirements[0]) < 2:
            raise SpecError(
                "native interpretation needs one nonempty "
                "prepared-register-recursion-requires entry")
        self.prepared_plan_requirements = [
            atom_text(value, "prepared-equation requirement")
            for value in plan_requirements[0][1:]
        ]
        self.prepared_call_requirements = [
            atom_text(value, "prepared-call requirement")
            for value in call_requirements[0][1:]
        ]
        self.prepared_register_requirements = [
            atom_text(value, "prepared-register-step requirement")
            for value in register_requirements[0][1:]
        ]
        self.prepared_recursive_requirements = [
            atom_text(value, "prepared-register-recursion requirement")
            for value in recursive_requirements[0][1:]
        ]
        all_requirements = (self.prepared_plan_requirements +
                            self.prepared_call_requirements +
                            self.prepared_register_requirements +
                            self.prepared_recursive_requirements)
        if (len(set(all_requirements)) != len(all_requirements) or
                set(all_requirements) != set(self.prepared_evidence)):
            raise SpecError(
                "prepared requirements must partition exactly the declared "
                "evidence values")
        if len(register_limits) != 1 or len(register_limits[0]) != 2:
            raise SpecError("native interpretation needs one register limit")
        limit_text = atom_text(
            register_limits[0][1], "prepared register limit")
        if not limit_text.isdigit() or int(limit_text) < 1 or int(limit_text) > 64:
            raise SpecError("prepared register limit must be in [1, 64]")
        self.prepared_register_limit = int(limit_text)

        self.owned_locals: list[tuple[str, str, str]] = []
        for entry in entries(native, "owned-local"):
            if len(entry) != 4:
                raise SpecError(f"malformed owned-local: {sexpr(entry)}")
            self.owned_locals.append(tuple(
                atom_text(item, "owned-local field") for item in entry[1:]))
        if not self.owned_locals:
            raise SpecError("at least one owned-local interpretation is required")

        self.wrappers: dict[str, SExpr] = {}
        defined_wrappers = entries(fixtures, "defined-wrapper")
        if len(defined_wrappers) != 1 or len(defined_wrappers[0]) != 2:
            raise SpecError(
                "relational fixtures need one defined-wrapper head")
        self.defined_wrapper = atom_text(
            defined_wrappers[0][1], "defined-wrapper head")
        for entry in entries(fixtures, "wrapper"):
            if len(entry) != 3:
                raise SpecError(f"malformed wrapper: {sexpr(entry)}")
            self.wrappers[atom_text(entry[1], "wrapper name")] = entry[2]
        if not self.wrappers:
            raise SpecError("at least one fixture wrapper is required")

        self.fold_definitions: list[SExpr] = []
        for entry in entries(fold_fixtures, "definition"):
            if len(entry) != 2:
                raise SpecError(
                    f"malformed fold fixture definition: {sexpr(entry)}")
            self.fold_definitions.append(entry[1])

        self.fold_cases: list[tuple[str, str, int, SExpr, SExpr]] = []
        self.pure_fold_case_names: set[str] = set()
        for case_form in ("fold-case", "pure-fold-case"):
            for entry in entries(fold_fixtures, case_form):
                if len(entry) != 6:
                    raise SpecError(
                        f"malformed {case_form}: {sexpr(entry)}")
                name = atom_text(entry[1], f"{case_form} name")
                disposition = atom_text(
                    entry[2], f"{case_form} disposition")
                steps_text = atom_text(
                    entry[3], f"{case_form} step count")
                if disposition not in {"admitted", "fallback"}:
                    raise SpecError(
                        f"{case_form} {name} has unknown disposition "
                        f"{disposition}")
                if not steps_text.isdigit():
                    raise SpecError(
                        f"{case_form} {name} step count must be nonnegative")
                steps = int(steps_text)
                if disposition == "fallback" and steps != 0:
                    raise SpecError(
                        f"fallback {case_form} {name} cannot claim "
                        "prepared steps")
                if case_form == "pure-fold-case":
                    if disposition != "admitted":
                        raise SpecError(
                            "pure-fold-case must reach machine admission")
                    self.pure_fold_case_names.add(name)
                self.fold_cases.append(
                    (name, disposition, steps, entry[4], entry[5]))
        if not self.fold_cases or not any(
                disposition == "admitted"
                for _, disposition, _, _, _ in self.fold_cases):
            raise SpecError(
                "fold fixtures need at least one admitted fold-case")
        if not any(disposition == "fallback"
                   for _, disposition, _, _, _ in self.fold_cases):
            raise SpecError(
                "fold fixtures need at least one fallback fold-case")

        self.prime_fold_cases: list[
            tuple[str, str, int, SExpr, SExpr]
        ] = []
        for entry in entries(fold_fixtures, "prime-fold-case"):
            if len(entry) != 6:
                raise SpecError(
                    f"malformed prime-fold-case: {sexpr(entry)}")
            name = atom_text(entry[1], "prime-fold-case name")
            disposition = atom_text(
                entry[2], "prime-fold-case disposition")
            steps_text = atom_text(
                entry[3], "prime-fold-case step count")
            if disposition != "admitted":
                raise SpecError(
                    "Prime demand fixtures must reach prepared admission")
            if not steps_text.isdigit() or int(steps_text) < 1:
                raise SpecError(
                    "Prime demand fixtures must complete a prepared step")
            self.prime_fold_cases.append(
                (name, disposition, int(steps_text), entry[4], entry[5]))
        if not self.prime_fold_cases:
            raise SpecError(
                "fold fixtures need a non-vacuous Prime demand case")

        self.petta_fold_cases: list[
            tuple[str, str, int, SExpr, SExpr]
        ] = []
        for entry in entries(fold_fixtures, "petta-fold-case"):
            if len(entry) != 6:
                raise SpecError(
                    f"malformed petta-fold-case: {sexpr(entry)}")
            name = atom_text(entry[1], "petta-fold-case name")
            disposition = atom_text(
                entry[2], "petta-fold-case disposition")
            steps_text = atom_text(
                entry[3], "petta-fold-case step count")
            if disposition != "fallback" or steps_text != "0":
                raise SpecError(
                    "PeTTa machine-owned fixture must be a zero-step fallback")
            self.petta_fold_cases.append(
                (name, disposition, 0, entry[4], entry[5]))
        if not self.petta_fold_cases:
            raise SpecError(
                "fold fixtures need a PeTTa machine-owned fallback case")

        self.fold_observations: list[tuple[str, SExpr, SExpr]] = []
        for entry in entries(fold_fixtures, "observation-case"):
            if len(entry) != 4:
                raise SpecError(
                    f"malformed fold observation-case: {sexpr(entry)}")
            self.fold_observations.append((
                atom_text(entry[1], "fold observation name"),
                entry[2], entry[3]))
        if not self.fold_observations:
            raise SpecError(
                "fold fixtures need an observation-case for effects")

        fold_case_names = [name for name, _, _, _, _ in self.fold_cases]
        prime_case_names = [
            name for name, _, _, _, _ in self.prime_fold_cases]
        petta_case_names = [
            name for name, _, _, _, _ in self.petta_fold_cases]
        observation_names = [name for name, _, _ in self.fold_observations]
        all_case_names = (fold_case_names + prime_case_names +
                          petta_case_names + observation_names)
        if len(set(all_case_names)) != len(all_case_names):
            raise SpecError("fold fixture case names must be unique")

        self.fold_expected_admissions = sum(
            1 for _, disposition, _, _, _ in self.fold_cases
            if disposition == "admitted")
        self.fold_expected_steps = sum(
            steps for _, disposition, steps, _, _ in self.fold_cases
            if disposition == "admitted")
        self.fold_expected_commits = self.fold_expected_admissions
        self.fold_expected_pure_admissions = len(
            self.pure_fold_case_names)
        self.fold_expected_pure_steps = sum(
            steps for name, _, steps, _, _ in self.fold_cases
            if name in self.pure_fold_case_names)
        self.fold_expected_pure_declines = 0
        self.prime_fold_expected_admissions = len(self.prime_fold_cases)
        self.prime_fold_expected_steps = sum(
            steps for _, _, steps, _, _ in self.prime_fold_cases)
        self.prime_fold_expected_commits = len(self.prime_fold_cases)
        self.petta_fold_expected_admissions = 0
        self.petta_fold_expected_steps = 0
        self.petta_fold_expected_commits = 0


def contains_relation(value: SExpr) -> bool:
    if isinstance(value, str):
        return value == "$relation"
    return any(contains_relation(item) for item in value)


def render_header(spec: Spec) -> str:
    effect_constants = ["    CETTA_GSLT_QUERY_EFFECT_PURE = 0u,"]
    for index, value in enumerate(spec.effect_flags):
        effect_constants.append(
            f"    CETTA_GSLT_QUERY_EFFECT_{c_enum(value)} = 1u << {index},")
    head_rows = " \\\n".join(
        f"    X({field}, CETTA_GSLT_QUERY_EFFECT_{c_enum(effect)})"
        for _, field, effect in spec.heads
    )
    opaque_expression_head_rows = " \\\n".join(
        f"    X({field})"
        for _, field in spec.opaque_expression_heads
    )
    opaque_expression_head_disjuncts = " ||\n           ".join(
        f"head == builtins->{field}"
        for _, field in spec.opaque_expression_heads
    )
    total_integer_rows = " \\\n".join(
        f"    X({field}, {arity}u)"
        for _, field, arity in spec.total_integer_heads
    )
    arena_owned_grounded_rows = " \\\n".join(
        f"    X({kind})" for kind in spec.arena_owned_grounded_kinds
    )
    identity_grounded_rows = " \\\n".join(
        f"    X({kind})" for kind in spec.identity_grounded_kind_tags
    )
    identity_grounded_cases = "\n".join(
        f"    case {kind}:\n        return true;"
        for kind in spec.identity_grounded_kind_tags
    )
    thread_local_symbol_rows = " \\\n".join(
        f"    X({field})"
        for field in spec.thread_local_resource_symbol_fields
    )
    thread_local_symbol_disjuncts = " ||\n           ".join(
        f"symbol == builtins->{field}"
        for field in spec.thread_local_resource_symbol_fields
    )
    register_result_constants = "\n".join(
        f"    CETTA_GSLT_REGISTER_RESULT_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.register_result_kinds)
    )
    register_instruction_constants = "\n".join(
        f"    CETTA_GSLT_REGISTER_INSTRUCTION_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.register_instructions)
    )
    register_operand_discipline_constants = "\n".join(
        f"    CETTA_GSLT_REGISTER_OPERANDS_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.register_operand_disciplines)
    )
    register_operand_discipline_cases = "\n".join(
        f"    case CETTA_GSLT_REGISTER_INSTRUCTION_{c_enum(instruction)}:\n"
        f"        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_"
        f"{c_enum(spec.register_instruction_disciplines[instruction])};\n"
        f"        return true;"
        for instruction in spec.register_instructions
    )
    register_head_rows = " \\\n".join(
        f"    X({field}, {arity}u, "
        f"CETTA_GSLT_REGISTER_RESULT_{c_enum(result_kind)}, "
        f"CETTA_GSLT_REGISTER_INSTRUCTION_{c_enum(instruction)})"
        for _, field, arity, result_kind, instruction in spec.register_heads
    )
    prepared_intrinsic_instruction_constants = "\n".join(
        f"    CETTA_GSLT_PREPARED_PURE_INTRINSIC_{c_enum(value)} = "
        f"{index},"
        for index, value in enumerate(spec.prepared_intrinsic_instructions)
    )
    prepared_intrinsic_operand_constants = "\n".join(
        f"    CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_"
        f"{c_enum(value)} = {index},"
        for index, value in enumerate(
            spec.prepared_intrinsic_operand_disciplines)
    )
    prepared_intrinsic_head_rows = " \\\n".join(
        f"    X({field}, {arity}u, "
        f"CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_"
        f"{c_enum(discipline)}, "
        f"CETTA_GSLT_PREPARED_PURE_INTRINSIC_{c_enum(instruction)})"
        for _, field, arity, discipline, instruction
        in spec.prepared_intrinsic_heads
    )
    fold_control_constants = "\n".join(
        f"    CETTA_GSLT_FOLD_CONTROL_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.fold_control_kinds)
    )
    fold_operand_role_constants = "\n".join(
        f"    CETTA_GSLT_CONTROL_OPERAND_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.fold_operand_roles)
    )
    fold_operand_role_cases = []
    for control in spec.fold_control_kinds:
        index_cases = "\n".join(
            f"        case {index}u:\n"
            f"            *role_out = CETTA_GSLT_CONTROL_OPERAND_"
            f"{c_enum(role)};\n"
            f"            return true;"
            for index, role in enumerate(
                spec.fold_control_operands[control])
        )
        fold_operand_role_cases.append(
            f"    case CETTA_GSLT_FOLD_CONTROL_{c_enum(control)}:\n"
            f"        switch (operand_index) {{\n"
            f"{index_cases}\n"
            f"        default:\n"
            f"            return false;\n"
            f"        }}")
    fold_operand_role_case_body = "\n".join(fold_operand_role_cases)
    pure_call_mode_constants = "\n".join(
        f"    CETTA_GSLT_PURE_CALL_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.pure_call_modes)
    )
    pure_call_pattern_kind_constants = "\n".join(
        f"    CETTA_GSLT_PATTERN_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.pure_call_pattern_kinds)
    )
    fold_control_rows = " \\\n".join(
        f"    X({field}, {arity}u, "
        f"CETTA_GSLT_FOLD_CONTROL_{c_enum(control)})"
        for _, field, arity, control in spec.fold_control_heads
    )
    sequence_representation_rows = " \\\n".join(
        f'    X("{key}", "{materializer}", "{empty}", "{cons}")'
        for key, materializer, empty, cons
        in spec.sequence_representations
    )
    sequence_fold_consumer_rows = " \\\n".join(
        f'    X("{key}", "{consumer}", {arity}u, {operand}u)'
        for key, consumer, arity, operand
        in spec.sequence_fold_consumers
    )
    sequence_fold_target_rows = " \\\n".join(
        f'    X("{key}", "{target}")'
        for key, target in spec.sequence_fold_targets
    )
    sequence_erasure_rows = " \\\n".join(
        f'    X("{key}", "{source}", "{target}", '
        f'CETTA_GSLT_SEQUENCE_ERASURE_{c_enum(mode)}, '
        f'{minimum}u, {maximum}u)'
        for key, source, target, mode, minimum, maximum
        in spec.sequence_erasures
    )
    register_instruction_cases = "\n".join(
        f"    case CETTA_GSLT_REGISTER_INSTRUCTION_{c_enum(instruction)}:\n"
        f"        {REGISTER_INSTRUCTION_C[instruction][1]}\n"
        f"        *kind_out = CETTA_GSLT_REGISTER_RESULT_"
        f"{c_enum(REGISTER_INSTRUCTION_C[instruction][0])};\n"
        f"        return true;"
        for instruction in spec.register_instructions
        if instruction in REGISTER_INSTRUCTION_C
    )
    register_small_instruction_cases = "\n".join(
        f"    case CETTA_GSLT_REGISTER_INSTRUCTION_{c_enum(instruction)}:\n"
        f"        {REGISTER_INSTRUCTION_SMALL_C[instruction]}\n"
        f"        *kind_out = CETTA_GSLT_REGISTER_RESULT_"
        f"{c_enum(register_instruction_result_kind(instruction))};\n"
        f"        return true;"
        for instruction in spec.register_instructions
        if instruction in REGISTER_INSTRUCTION_SMALL_C
    )
    register_atom_instruction_cases = "\n".join(
        f"    case CETTA_GSLT_REGISTER_INSTRUCTION_{c_enum(instruction)}:\n"
        f"        {REGISTER_INSTRUCTION_ATOM_C[instruction][1]}\n"
        f"        *kind_out = CETTA_GSLT_REGISTER_RESULT_"
        f"{c_enum(REGISTER_INSTRUCTION_ATOM_C[instruction][0])};\n"
        f"        return true;"
        for instruction in spec.register_instructions
        if instruction in REGISTER_INSTRUCTION_ATOM_C
    )
    row_constants = "\n".join(
        f"    CETTA_GSLT_ROW_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.row_values)
    )
    guarded: list[str] = []
    fallback: list[str] = []
    for row_class, probe in ROW_CLASS_PROBES.items():
        disposition = f"CETTA_GSLT_ROW_{c_enum(spec.row_map[row_class])}"
        if probe is None:
            fallback.append(f"    return {disposition};")
        else:
            guarded.append(f"    if ({probe})\n        return {disposition};")
    if len(fallback) != 1:
        raise SpecError("exactly one row class must be the fallback probe")
    row_body = "\n".join(guarded + fallback)
    event_constants = "\n".join(
        f"    CETTA_GSLT_OBSERVATION_{c_enum(event)} = {index},"
        for index, event in enumerate(spec.events)
    )
    visible_events = [event for event in spec.events
                      if spec.observations[(event, "diagnostic")] == "emit"
                      and spec.observations[(event, "default")] == "erase"]
    if len(visible_events) != len(spec.events):
        raise SpecError(
            "only diagnostic-emit/default-erase observations are compilable")
    visible_disjuncts = " ||\n           ".join(
        f"event == CETTA_GSLT_OBSERVATION_{c_enum(event)}"
        for event in spec.events
    )
    transition_constants = "\n".join(
        f"    CETTA_GSLT_LIFETIME_{c_enum(name)} = {index},"
        for index, name in enumerate(spec.transitions)
    )
    invalidating = [name for name, result in spec.lifetime_map.items()
                    if result == "invalidate"]
    if not invalidating:
        raise SpecError("no lifetime transition invalidates allocations")
    invalidate_disjuncts = " ||\n           ".join(
        f"transition == CETTA_GSLT_LIFETIME_{c_enum(name)}"
        for name in spec.transitions if name in invalidating
    )
    owned_macros = "\n\n".join(
        f"""/* This interpretation makes the uninitialized -> live transition explicit
 * at every cleanup-owned local.  The owning header must be visible at
 * expansion. */
#define CETTA_GSLT_OWNED_{c_enum(type_name)}(name) \\
    __attribute__((cleanup({free_fn}))) {type_name} name; \\
    {init_fn}(&(name))"""
        for type_name, init_fn, free_fn in spec.owned_locals
    )
    diagnostic_env = spec.environments["diagnostic"]
    interval_env = spec.environments["diagnostic-interval"]
    effect_enum = "\n".join(effect_constants)
    prepared_constants = "\n".join(
        f"    CETTA_GSLT_EVIDENCE_{c_enum(value)} = 1u << {index},"
        for index, value in enumerate(spec.prepared_evidence)
    )
    prepared_plan_mask = " |\n        ".join(
        f"CETTA_GSLT_EVIDENCE_{c_enum(value)}"
        for value in spec.prepared_plan_requirements
    )
    prepared_call_mask = " |\n        ".join(
        f"CETTA_GSLT_EVIDENCE_{c_enum(value)}"
        for value in spec.prepared_call_requirements
    )
    prepared_register_mask = " |\n        ".join(
        f"CETTA_GSLT_EVIDENCE_{c_enum(value)}"
        for value in spec.prepared_register_requirements
    )
    frame_kind_constants = "\n".join(
        f"    CETTA_EVAL_GC_FRAME_{c_enum(kind)} = {i},"
        for i, (kind, _) in enumerate(spec.root_frame_kinds)
    )
    frame_kind_rows = " \\\n".join(
        f"    X({kind}, {c_enum(kind)})"
        for kind, _ in spec.root_frame_kinds
    )
    visitor_of = {
        "strong-atom-slot": "CETTA_GC_VISIT_STRONG_ATOM_SLOT",
        "strong-atom-span": "CETTA_GC_VISIT_STRONG_ATOM_SPAN",
        "logical-bindings": "CETTA_GC_VISIT_LOGICAL_BINDINGS",
        "outcome-set": "CETTA_GC_VISIT_OUTCOME_SET",
        "variant-instance": "CETTA_GC_VISIT_VARIANT_INSTANCE",
        "ephemeron-atom-map": "CETTA_GC_VISIT_EPHEMERON_ATOM_MAP",
    }
    field_emitter_of = {
        "strong-atom-slot": "STRONG_ATOM_SLOT",
        "strong-atom-span": "STRONG_ATOM_SPAN",
        "logical-bindings": "LOGICAL_BINDINGS",
        "outcome-set": "OUTCOME_SET",
        "variant-instance": "VARIANT_INSTANCE",
        "ephemeron-atom-map": "EPHEMERON_ATOM_MAP",
    }
    frame_field_macros = []
    arm_blocks = []
    for kind, fields in spec.root_frame_kinds:
        for _, fkind in fields:
            if fkind not in visitor_of:
                raise SpecError(
                    f"no compilation scheme for root field kind {fkind}")
        field_body = " \\\n".join(
            f"    {field_emitter_of[fkind]}({fname})"
            for fname, fkind in fields
        )
        frame_field_macros.append(
            f"#define CETTA_EVAL_GC_FRAME_FIELDS_{kind}(\\\n"
            f"    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \\\n"
            f"    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \\\n"
            f"    EPHEMERON_ATOM_MAP) \\\n"
            f"{field_body}"
        )
        body = " \\\n".join(
            f"    {visitor_of[fkind]}((SESSION), (PAYLOAD)->{fname}, FAIL);"
            for fname, fkind in fields
        )
        arm_blocks.append(
            f"#define CETTA_EVAL_GC_ARM_{kind}(SESSION, PAYLOAD, FAIL) do {{ \\\n"
            f"{body} \\\n"
            f"}} while (0)"
        )
    frame_field_macro_blocks = "\n\n".join(frame_field_macros)
    frame_arm_macros = "\n\n".join(arm_blocks)
    prepared_recursive_mask = " |\n        ".join(
        f"CETTA_GSLT_EVIDENCE_{c_enum(value)}"
        for value in spec.prepared_recursive_requirements
    )
    return f"""#ifndef CETTA_EXECUTION_CONTRACTS_GENERATED_H
#define CETTA_EXECUTION_CONTRACTS_GENERATED_H

/* Generated from lib/gslt_execution_contracts.metta.
 * Source SHA-256: {spec.digest}
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {{
{effect_enum}
}} CettaGsltQueryEffect;

#define CETTA_GSLT_QUERY_EFFECT_HEAD_ROWS(X) \\
{head_rows}

static inline CettaGsltQueryEffect cetta_gslt_query_effect_join(
    CettaGsltQueryEffect left, CettaGsltQueryEffect right) {{
    return (CettaGsltQueryEffect)((unsigned)left | (unsigned)right);
}}

/* The analysis policy is generated with the effect algebra.  Native code
 * supplies the revision-pinned equation graph; uncertainty is never allowed
 * to become accelerator authority. */
#define CETTA_GSLT_USER_HEAD_EFFECT_REVISION_KEYED 1
#define CETTA_GSLT_USER_HEAD_EFFECT_LEAST_FIXED_POINT 1
#define CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD \
    CETTA_GSLT_QUERY_EFFECT_{c_enum(spec.user_head_uncertain)}
#define CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL \
    CETTA_GSLT_QUERY_EFFECT_PURE
#define CETTA_GSLT_INERT_EXPRESSION_CHILDREN_OPAQUE 1

#define CETTA_GSLT_OPAQUE_EXPRESSION_HEAD_ROWS(X) \
{opaque_expression_head_rows}

static inline bool cetta_gslt_query_effect_children_opaque(
    SymbolId head, const BuiltinSyms *builtins) {{
    return builtins &&
           ({opaque_expression_head_disjuncts});
}}

#define CETTA_GSLT_TOTAL_INTEGER_HEAD_ROWS(X) \
{total_integer_rows}

/* Default construction keeps these generated leaf classes arena-owned.
 * Explicit publication may still hash-cons them through hashcons_get. */
#define CETTA_GSLT_ARENA_OWNED_GROUNDED_KIND_ROWS(X) \
{arena_owned_grounded_rows}

/* Executable resource-leaf classifiers generated from resource-lifetime.
 * Atom constructors fold these judgments into compositional summary bits;
 * evaluators consume the summaries without recursively rescanning terms. */
#define CETTA_GSLT_IDENTITY_BEARING_GROUNDED_KIND_ROWS(X) \
{identity_grounded_rows}

static inline bool cetta_gslt_identity_bearing_grounded_kind(
    GroundedKind kind) {{
    switch (kind) {{
{identity_grounded_cases}
    default:
        return false;
    }}
}}

#define CETTA_GSLT_THREAD_LOCAL_RESOURCE_SYMBOL_ROWS(X) \
{thread_local_symbol_rows}

static inline bool cetta_gslt_thread_local_resource_symbol(
    SymbolId symbol, const BuiltinSyms *builtins) {{
    return builtins &&
           ({thread_local_symbol_disjuncts});
}}

typedef enum {{
{register_result_constants}
}} CettaGsltRegisterResultKind;

typedef enum {{
{register_instruction_constants}
}} CettaGsltRegisterInstruction;

typedef enum {{
{register_operand_discipline_constants}
}} CettaGsltRegisterOperandDiscipline;

static inline bool cetta_gslt_register_operand_discipline(
    CettaGsltRegisterInstruction instruction,
    CettaGsltRegisterOperandDiscipline *discipline_out) {{
    if (!discipline_out)
        return false;
    switch (instruction) {{
{register_operand_discipline_cases}
    default:
        return false;
    }}
}}

#define CETTA_GSLT_REGISTER_HEAD_ROWS(X) \
{register_head_rows}

typedef enum {{
{prepared_intrinsic_instruction_constants}
}} CettaGsltPreparedPureIntrinsicInstruction;

typedef enum {{
{prepared_intrinsic_operand_constants}
}} CettaGsltPreparedPureIntrinsicOperandDiscipline;

#define CETTA_GSLT_PREPARED_PURE_INTRINSIC_HEAD_ROWS(X) \
{prepared_intrinsic_head_rows}

typedef enum {{
{fold_control_constants}
}} CettaGsltFoldControl;

typedef enum {{
{fold_operand_role_constants}
}} CettaGsltControlOperandRole;

static inline bool cetta_gslt_fold_control_operand_role(
    CettaGsltFoldControl control, uint32_t operand_index,
    CettaGsltControlOperandRole *role_out) {{
    if (!role_out)
        return false;
    switch (control) {{
{fold_operand_role_case_body}
    default:
        return false;
    }}
}}

typedef enum {{
{pure_call_mode_constants}
}} CettaGsltPureCallMode;

typedef enum {{
{pure_call_pattern_kind_constants}
}} CettaGsltPatternKind;

/* Executable unique-match evidence generated from the pure-call component.
 * The runtime projects representation facts into this judgment; uncertainty
 * (including differences below a shared constructor) is deliberately not
 * evidence of determinacy. */
static inline bool cetta_gslt_pure_call_whnf_disjoint(
    CettaGsltPatternKind left_kind,
    CettaGsltPatternKind right_kind,
    bool atoms_equal,
    uint64_t left_arity,
    uint64_t right_arity,
    bool left_head_rigid,
    bool right_head_rigid,
    bool heads_equal) {{
    if (left_kind == CETTA_GSLT_PATTERN_VARIABLE ||
        right_kind == CETTA_GSLT_PATTERN_VARIABLE)
        return false;
    if (left_kind != right_kind)
        return true;
    if (left_kind == CETTA_GSLT_PATTERN_ATOM)
        return !atoms_equal;
    if (left_kind != CETTA_GSLT_PATTERN_EXPRESSION)
        return false;
    if (left_arity != right_arity)
        return true;
    return left_arity > 0u && left_head_rigid &&
           right_head_rigid && !heads_equal;
}}

/* Executable control arms for the generated determinate-fold program. */
#define CETTA_GSLT_FOLD_CONTROL_HEAD_ROWS(X) \
{fold_control_rows}

/* Representation rows for sequence consumers.  The runtime resolves these
 * names in the active symbol table; library constructors therefore remain
 * authored data rather than evaluator vocabulary. */
#define CETTA_GSLT_SEQUENCE_REPRESENTATION_ROWS(X) \
{sequence_representation_rows}

/* A represented operand remains source syntax until its sequence consumer
 * exposes the generated fold handler.  This prevents eager pure-call
 * compilation from materializing a representation that the handler can erase
 * by an observer-commuting homomorphism. */
#define CETTA_GSLT_SEQUENCE_FOLD_CONSUMER_ROWS(X) \
{sequence_fold_consumer_rows}

typedef enum {{
    CETTA_GSLT_SEQUENCE_ERASURE_UNWRAP = 0,
    CETTA_GSLT_SEQUENCE_ERASURE_RECURSIVE_ALL = 1,
    CETTA_GSLT_SEQUENCE_ERASURE_SCALAR_ALL = 2,
}} CettaGsltSequenceErasureMode;

/* Canonical fold target used only after a prepared sequence path declines. */
#define CETTA_GSLT_SEQUENCE_FOLD_TARGET_ROWS(X) \
{sequence_fold_target_rows}

/* Observer-commuting erasures from authored sequence producers to the flat
 * free-monoid representation consumed by the prepared fold. */
#define CETTA_GSLT_SEQUENCE_ERASURE_ROWS(X) \
{sequence_erasure_rows}

/* ── Evaluator root frames: generated evacuation arms ─────────────────────
 * Frame kinds and their field traversal sequences come from the
 * presentation.  The collector core defines ONE visitor macro per root
 * field kind and stays language-ignorant:
 *   CETTA_GC_VISIT_STRONG_ATOM_SLOT(SESSION, SLOT_PTR, FAIL)
 *   CETTA_GC_VISIT_STRONG_ATOM_SPAN(SESSION, SPAN_PTR, FAIL)
 *   CETTA_GC_VISIT_LOGICAL_BINDINGS(SESSION, ENV_PTR, FAIL)
 *   CETTA_GC_VISIT_OUTCOME_SET(SESSION, OS_PTR, FAIL)
 *   CETTA_GC_VISIT_EPHEMERON_ATOM_MAP(SESSION, MAP, FAIL)
 * A spec field kind without a core visitor macro fails to compile; an
 * omitted arm is exercised by the skip-arm negative gate. */
typedef enum {{
{frame_kind_constants}
    CETTA_EVAL_GC_FRAME_KIND_COUNT
}} CettaEvalGcFrameKind;

#define CETTA_EVAL_GC_FRAME_KIND_ROWS(X) \
{frame_kind_rows}

/* Expanding these rows with C type emitters produces the root payload
 * structures themselves.  The presentation therefore owns both layout and
 * traversal order; the collector supplies only the representation
 * types and their language-ignorant visitors. */
{frame_field_macro_blocks}

{frame_arm_macros}

/* Representation-polymorphic register arms generated from the presentation. */
static inline bool cetta_gslt_register_execute_atom_binary(
    CettaGsltRegisterInstruction instruction,
    Atom *left, Atom *right, bool *boolean_out,
    CettaGsltRegisterResultKind *kind_out) {{
    if (!left || !right || !boolean_out || !kind_out)
        return false;
    switch (instruction) {{
{register_atom_instruction_cases}
    default:
        return false;
    }}
}}

/* Tagged-small-integer interpretation generated from the same register
 * program.  Exact arithmetic requests GMP promotion only on overflow. */
static inline bool cetta_gslt_register_execute_small_binary(
    CettaGsltRegisterInstruction instruction,
    int64_t *integer_out, bool *boolean_out,
    int64_t left, int64_t right,
    CettaGsltRegisterResultKind *kind_out, bool *promote_out) {{
    if (!integer_out || !boolean_out || !kind_out || !promote_out)
        return false;
    *promote_out = false;
    switch (instruction) {{
{register_small_instruction_cases}
    default:
        return false;
    }}
}}

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>

/* Executable interpretation generated from register-expression-execution. */
static inline bool cetta_gslt_register_execute_binary(
    CettaGsltRegisterInstruction instruction,
    mpz_ptr integer_out, bool *boolean_out,
    mpz_srcptr left, mpz_srcptr right,
    CettaGsltRegisterResultKind *kind_out) {{
    if (!integer_out || !boolean_out || !left || !right || !kind_out)
        return false;
    switch (instruction) {{
{register_instruction_cases}
    default:
        return false;
    }}
}}
#endif

typedef enum {{
{row_constants}
}} CettaGsltRowDisposition;

static inline CettaGsltRowDisposition cetta_gslt_classify_streamed_row(
    bool decoded, bool cyclic) {{
{row_body}
}}

typedef enum {{
{event_constants}
}} CettaGsltObservationEvent;

static inline bool cetta_gslt_observation_visible(
    CettaGsltObservationEvent event, bool diagnostic_profile) {{
    return diagnostic_profile &&
           ({visible_disjuncts});
}}

#define CETTA_GSLT_MATCH_CHAIN_TRACE_ENV \"{diagnostic_env}\"
#define CETTA_GSLT_MATCH_CHAIN_TRACE_INTERVAL_ENV \"{interval_env}\"

typedef enum {{
{transition_constants}
}} CettaGsltLifetimeTransition;

static inline bool cetta_gslt_lifetime_invalidates_allocations(
    CettaGsltLifetimeTransition transition) {{
    return {invalidate_disjuncts};
}}

typedef enum {{
{prepared_constants}
}} CettaGsltPreparedEquationEvidence;

#define CETTA_GSLT_PREPARED_EQUATION_MAX_REGISTERS \
    {spec.prepared_register_limit}u

static inline bool cetta_gslt_prepared_equation_plan_admitted(
    uint32_t evidence) {{
    const uint32_t required =
        {prepared_plan_mask};
    return (evidence & required) == required;
}}

/* Shared accelerator admission for call-policy preservation.  The current
 * implementation is conservative: declared arrow signatures fall back to
 * canonical dispatch until an accelerator implements their policy. */
#define CETTA_GSLT_ACCELERATOR_CALL_POLICY_SUPPORTED(space, head, arity) \
    (!space_head_has_arrow_signature((space), (head), (arity)))

static inline bool cetta_gslt_prepared_equation_call_admitted(
    uint32_t evidence) {{
    const uint32_t required =
        {prepared_plan_mask} |
        {prepared_call_mask};
    return (evidence & required) == required;
}}

static inline bool cetta_gslt_prepared_register_step_admitted(
    uint32_t evidence) {{
    const uint32_t required =
        {prepared_plan_mask} |
        {prepared_call_mask} |
        {prepared_register_mask};
    return (evidence & required) == required;
}}

static inline bool cetta_gslt_prepared_register_recursion_admitted(
    uint32_t evidence) {{
    const uint32_t required =
        {prepared_plan_mask} |
        {prepared_call_mask} |
        {prepared_recursive_mask};
    return (evidence & required) == required;
}}

{owned_macros}

#endif /* CETTA_EXECUTION_CONTRACTS_GENERATED_H */
"""


def render_make_fragment(spec: Spec) -> str:
    return f"""# Generated from lib/gslt_execution_contracts.metta.
# Source SHA-256: {spec.digest}
CETTA_GSLT_MATCH_CHAIN_TRACE_ENV := {spec.environments["diagnostic"]}
CETTA_GSLT_MATCH_CHAIN_TRACE_INTERVAL_ENV := {spec.environments["diagnostic-interval"]}
CETTA_GSLT_FOLD_FIXTURE_EXPECTED_ADMISSIONS := {spec.fold_expected_admissions}
CETTA_GSLT_FOLD_FIXTURE_EXPECTED_STEPS := {spec.fold_expected_steps}
CETTA_GSLT_FOLD_FIXTURE_EXPECTED_COMMITS := {spec.fold_expected_commits}
CETTA_GSLT_PRIME_FOLD_EXPECTED_ADMISSIONS := {spec.prime_fold_expected_admissions}
CETTA_GSLT_PRIME_FOLD_EXPECTED_STEPS := {spec.prime_fold_expected_steps}
CETTA_GSLT_PRIME_FOLD_EXPECTED_COMMITS := {spec.prime_fold_expected_commits}
CETTA_GSLT_PETTA_FOLD_EXPECTED_ADMISSIONS := {spec.petta_fold_expected_admissions}
CETTA_GSLT_PETTA_FOLD_EXPECTED_STEPS := {spec.petta_fold_expected_steps}
CETTA_GSLT_PETTA_FOLD_EXPECTED_COMMITS := {spec.petta_fold_expected_commits}
"""


def substitute(value: SExpr, replacement: SExpr) -> SExpr:
    if isinstance(value, str):
        return replacement if value == "$relation" else value
    return [substitute(item, replacement) for item in value]


def render_fixture(spec: Spec) -> str:
    relation: SExpr = ["match", "&effect-space", ["right", "$key", "$value"],
                       ["joined", "$name", "$value"]]
    lines = [
        "; Generated by scripts/gen_execution_contracts.py.",
        "; Planner admission follows the relational may-effect fold.",
        "!(bind! &effect-space (new-space pathmap))",
        "!(add-atom &effect-space (left n0 k0))",
        "!(add-atom &effect-space (right k0 v0))",
        "!(add-atom &effect-space (left n1 k1))",
        "!(add-atom &effect-space (right k1 v1))",
        f"(= ({spec.defined_wrapper} $name $key) {sexpr(relation)})",
        "",
    ]
    for name, wrapper in spec.wrappers.items():
        wrapped = substitute(wrapper, relation)
        expected = ("(((joined n0 v0) (joined n1 v1)))"
                    if contains_relation(wrapper)
                    else "(((joined n0 k0) (joined n1 k1)))")
        lines.extend([
            f"; {name}",
            "!(assertEqualToResult",
            "  (collapse",
            "    (match &effect-space (left $name $key)",
            f"      {sexpr(wrapped)}))",
            f"  {expected})",
        ])
    lines.extend([
        "",
        "; A user-defined head inherits the relational effect of its body.",
        "!(assertEqualToResult",
        "  (collapse",
        "    (match &effect-space (left $name $key)",
        f"      ({spec.defined_wrapper} $name $key)))",
        "  (((joined n0 v0) (joined n1 v1))))",
    ])
    for name, _, _ in spec.heads:
        template = HEAD_FIXTURE_TEMPLATES.get(name)
        if template is None:
            continue
        lines.extend([
            "",
            f"; A template running {name} is relational: the pull declines",
            "; and the oracle answers per row.",
            "!(collapse",
            "  (match &effect-space (left $name $key)",
            f"    {sexpr(template)}))",
        ])
    lines.extend([
        "",
        "; A cyclic substitution is additive zero, not a transport fault.",
        "!(bind! &row-space (new-space pathmap))",
        "!(add-atom &row-space (edge $stored $stored))",
        "!(add-atom &row-space (edge a (f a)))",
        "!(assertEqualToResult",
        "  (collapse (match &row-space (edge $query (f $query)) (ok $query)))",
        "  (((ok a))))",
        "",
    ])
    return "\n".join(lines)


def render_fold_fixture(spec: Spec) -> str:
    lines = [
        "; Generated by scripts/gen_execution_contracts.py.",
        "; These queries instantiate the determinate-fold laws in every lane.",
    ]
    for definition in spec.fold_definitions:
        lines.append(sexpr(definition))
    for name, disposition, _, query, _ in spec.fold_cases:
        lines.extend([
            "",
            f"; {name}: {disposition}",
            f"!{sexpr(query)}",
        ])
    for name, query, _ in spec.fold_observations:
        lines.extend([
            "",
            f"; {name}",
            f"!{sexpr(query)}",
        ])
    lines.append("")
    return "\n".join(lines)


def render_prime_fold_fixture(spec: Spec) -> str:
    lines = [
        "; Generated by scripts/gen_execution_contracts.py.",
        "; Prime demand is preserved by the prepared fold instruction.",
    ]
    for definition in spec.fold_definitions:
        lines.append(sexpr(definition))
    for name, disposition, _, query, _ in spec.prime_fold_cases:
        lines.extend([
            "",
            f"; {name}: {disposition}",
            f"!{sexpr(query)}",
        ])
    lines.append("")
    return "\n".join(lines)


def render_petta_fold_fixture(spec: Spec) -> str:
    lines = [
        "; Generated by scripts/gen_execution_contracts.py.",
        "; PeTTa machine-owned control remains on its relational continuation.",
    ]
    for name, disposition, _, query, _ in spec.petta_fold_cases:
        lines.extend([
            "",
            f"; {name}: {disposition}",
            f"!{sexpr(query)}",
        ])
    lines.append("")
    return "\n".join(lines)


def render_expected_values(values: list[SExpr], language: str) -> str:
    def dialect_value(value: SExpr) -> SExpr:
        if language == "petta" and isinstance(value, str):
            if value == "True":
                return "true"
            if value == "False":
                return "false"
        if isinstance(value, list):
            return [dialect_value(item) for item in value]
        return value

    if language in {"he", "prime"}:
        return "\n".join(f"[{sexpr(value)}]" for value in values) + "\n"
    if language == "petta":
        return "\n".join(
            sexpr(dialect_value(value)) for value in values) + "\n"
    raise SpecError(f"no fold fixture rendering for language {language}")


def render_fold_expected(spec: Spec, language: str) -> str:
    values = [expected for _, _, _, _, expected in spec.fold_cases]
    values.extend(expected for _, _, expected in spec.fold_observations)
    return render_expected_values(values, language)


def render_prime_fold_expected(spec: Spec) -> str:
    return render_expected_values(
        [expected for _, _, _, _, expected in spec.prime_fold_cases],
        "prime")


def render_petta_fold_expected(spec: Spec) -> str:
    return render_expected_values(
        [expected for _, _, _, _, expected in spec.petta_fold_cases],
        "petta")


def unique_runtime_fixture(prefix: str, fixture_text: str) -> Path:
    runtime_dir = ROOT / "runtime"
    runtime_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", prefix=prefix, suffix=".metta",
            dir=runtime_dir, delete=False) as handle:
        handle.write(fixture_text)
        return Path(handle.name)


def oracle_expected(cetta: Path, fixture_text: str) -> str:
    scratch = unique_runtime_fixture(
        "execution-contracts-oracle.", fixture_text)
    env = os.environ.copy()
    env["CETTA_PATHMAP_QUERY_INDEX"] = "1"
    env["CETTA_PATHMAP_PULL_CONSUMERS"] = "0"
    try:
        proc = subprocess.run(
            [str(cetta), "--lang", "he", "--profile", "extended", str(scratch)],
            cwd=ROOT, env=env, text=True, capture_output=True, timeout=30,
        )
    finally:
        scratch.unlink(missing_ok=True)
    if proc.returncode != 0 or proc.stderr or "(Error " in proc.stdout:
        raise RuntimeError(
            "materializing oracle rejected generated execution fixture\n"
            f"return={proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    return proc.stdout


def fold_oracle_expected(
        cetta: Path, fixture_text: str, language: str) -> str:
    scratch = unique_runtime_fixture(
        f"determinate-fold-{language}-oracle.", fixture_text)
    env = os.environ.copy()
    env["CETTA_MATCH_CHAIN_TRACE"] = "1"
    command = [str(cetta), "--quiet", "--lang", language]
    if language == "he":
        command.extend(["--profile", "extended"])
    command.append(str(scratch))
    try:
        proc = subprocess.run(
            command, cwd=ROOT, env=env, text=True,
            capture_output=True, timeout=30,
        )
    finally:
        scratch.unlink(missing_ok=True)
    if proc.returncode != 0 or "(Error " in proc.stdout:
        raise RuntimeError(
            f"{language} canonical fold rejected generated fixture\n"
            f"return={proc.returncode}\nstdout:\n{proc.stdout}"
            f"stderr:\n{proc.stderr}"
        )
    return proc.stdout


def verify_prepared_fold_runtime(
        cetta: Path, fixture_text: str, language: str,
        expected_output: str, expected_admissions: int,
        expected_steps: int, expected_commits: int,
        expected_pure_admissions: int | None = None,
        expected_pure_steps: int | None = None,
        expected_pure_declines: int | None = None) -> None:
    scratch = unique_runtime_fixture(
        f"determinate-fold-{language}-prepared.", fixture_text)
    env = os.environ.copy()
    env.pop("CETTA_MATCH_CHAIN_TRACE", None)
    command = [
        str(cetta), "--emit-runtime-stats", "--quiet", "--lang", language,
    ]
    if language == "he":
        command.extend(["--profile", "extended"])
    command.append(str(scratch))
    try:
        proc = subprocess.run(
            command, cwd=ROOT, env=env, text=True,
            capture_output=True, timeout=30,
        )
    finally:
        scratch.unlink(missing_ok=True)
    if proc.returncode != 0 or "(Error " in proc.stdout:
        raise RuntimeError(
            f"{language} prepared fold rejected generated fixture\n"
            f"return={proc.returncode}\nstdout:\n{proc.stdout}"
            f"stderr:\n{proc.stderr}"
        )
    if proc.stdout != expected_output:
        raise RuntimeError(
            f"{language} prepared fold disagrees with the spec\n"
            f"expected:\n{expected_output}actual:\n{proc.stdout}")
    counters: dict[str, int] = {}
    for line in proc.stderr.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0] == "runtime-counter":
            counters[fields[1]] = int(fields[2])
    observed_admissions = counters.get("prepared-fold-admission")
    observed_steps = counters.get("prepared-fold-step")
    observed_commits = counters.get("prepared-fold-commit")
    if (observed_admissions != expected_admissions or
            observed_steps != expected_steps or
            observed_commits != expected_commits):
        raise RuntimeError(
            f"{language} prepared fold mechanism mismatch: "
            f"admissions={observed_admissions}, steps={observed_steps}, "
            f"commits={observed_commits}; expected "
            f"admissions={expected_admissions}, steps={expected_steps}, "
            f"commits={expected_commits}")
    if expected_pure_admissions is not None:
        observed_pure_admissions = counters.get(
            "prepared-pure-machine-admission")
        observed_pure_steps = counters.get("prepared-pure-machine-step")
        observed_pure_declines = counters.get(
            "prepared-pure-machine-decline")
        if (observed_pure_admissions != expected_pure_admissions or
                observed_pure_steps != expected_pure_steps or
                observed_pure_declines != expected_pure_declines):
            raise RuntimeError(
                f"{language} prepared pure-machine mismatch: "
                f"admissions={observed_pure_admissions}, "
                f"steps={observed_pure_steps}, "
                f"declines={observed_pure_declines}; expected "
                f"admissions={expected_pure_admissions}, "
                f"steps={expected_pure_steps}, "
                f"declines={expected_pure_declines}")


def write_or_check(path: Path, content: str, check: bool) -> None:
    if check:
        if not path.is_file() or path.read_text() != content:
            raise RuntimeError(f"generated artifact is stale: {path.relative_to(ROOT)}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--check-fold-runtime", action="store_true")
    args = parser.parse_args()
    try:
        if args.check_fold_runtime and not args.check:
            raise RuntimeError("--check-fold-runtime requires --check")
        spec = Spec(SPEC.read_text())
        header = render_header(spec)
        make_fragment = render_make_fragment(spec)
        fixture = render_fixture(spec)
        expected = oracle_expected(args.cetta.resolve(), fixture)
        fold_fixture = render_fold_fixture(spec)
        fold_expected = {
            language: render_fold_expected(spec, language)
            for language in ("he", "prime", "petta")
        }
        for language, expected_output in fold_expected.items():
            actual_output = fold_oracle_expected(
                args.cetta.resolve(), fold_fixture, language)
            if actual_output != expected_output:
                raise RuntimeError(
                    f"{language} canonical fold disagrees with the spec\n"
                    f"expected:\n{expected_output}actual:\n{actual_output}")
        prime_fold_fixture = render_prime_fold_fixture(spec)
        prime_fold_expected = render_prime_fold_expected(spec)
        prime_fold_oracle = fold_oracle_expected(
            args.cetta.resolve(), prime_fold_fixture, "prime")
        if prime_fold_oracle != prime_fold_expected:
            raise RuntimeError(
                "Prime canonical demand fold disagrees with the spec\n"
                f"expected:\n{prime_fold_expected}"
                f"actual:\n{prime_fold_oracle}")
        petta_fold_fixture = render_petta_fold_fixture(spec)
        petta_fold_expected = render_petta_fold_expected(spec)
        petta_fold_oracle = fold_oracle_expected(
            args.cetta.resolve(), petta_fold_fixture, "petta")
        if petta_fold_oracle != petta_fold_expected:
            raise RuntimeError(
                "PeTTa canonical failure fold disagrees with the spec\n"
                f"expected:\n{petta_fold_expected}"
                f"actual:\n{petta_fold_oracle}")
        write_or_check(HEADER, header, args.check)
        write_or_check(MAKE_FRAGMENT, make_fragment, args.check)
        write_or_check(FIXTURE, fixture, args.check)
        write_or_check(EXPECTED, expected, args.check)
        write_or_check(FOLD_FIXTURE, fold_fixture, args.check)
        write_or_check(FOLD_EXPECTED_HE, fold_expected["he"], args.check)
        write_or_check(
            FOLD_EXPECTED_PRIME, fold_expected["prime"], args.check)
        write_or_check(
            FOLD_EXPECTED_PETTA, fold_expected["petta"], args.check)
        write_or_check(PRIME_FOLD_FIXTURE, prime_fold_fixture, args.check)
        write_or_check(PRIME_FOLD_EXPECTED, prime_fold_expected, args.check)
        write_or_check(PETTA_FOLD_FIXTURE, petta_fold_fixture, args.check)
        write_or_check(PETTA_FOLD_EXPECTED, petta_fold_expected, args.check)
        if args.check_fold_runtime:
            for language, expected_output in fold_expected.items():
                verify_prepared_fold_runtime(
                    args.cetta.resolve(), fold_fixture, language,
                    expected_output, spec.fold_expected_admissions,
                    spec.fold_expected_steps, spec.fold_expected_commits,
                    spec.fold_expected_pure_admissions,
                    spec.fold_expected_pure_steps,
                    spec.fold_expected_pure_declines)
            verify_prepared_fold_runtime(
                args.cetta.resolve(), prime_fold_fixture, "prime",
                prime_fold_expected, spec.prime_fold_expected_admissions,
                spec.prime_fold_expected_steps,
                spec.prime_fold_expected_commits)
            verify_prepared_fold_runtime(
                args.cetta.resolve(), petta_fold_fixture, "petta",
                petta_fold_expected, spec.petta_fold_expected_admissions,
                spec.petta_fold_expected_steps,
                spec.petta_fold_expected_commits)
    except (SpecError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    action = "verified" if args.check else "generated"
    print(f"PASS: {action} compositional execution-contract artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
