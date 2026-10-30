#!/usr/bin/env python3
"""Compile compositional execution-contract GSLTs to C and focused fixtures.

The spec is the authority: value sets, interpretation maps, relational heads,
and fixture wrappers are read from the presentation and compiled into the
generated header, make fragment, and fixtures.  The loader checks internal
coherence (referenced values must be declared, heads must exist in the symbol
table) and refuses constructions it has no compilation scheme for — it never
pins the spec to a previously generated instance.

Runtime mechanism checks also respect the frontend's return carrier. A
source-fold law does not admit an Atom-only return that loses invocation state.
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
# semantic instructions; arithmetic ones are operations of the exact-integer
# theory, computed by its one implementation (src/exact_integer.h) on GMP
# and on tagged small integers alike.  Comparisons are interpreted here.
REGISTER_INSTRUCTION_OPERATION: dict[str, str] = {
    "integer-add": "CETTA_EXACT_INTEGER_V1_ADD",
    "integer-subtract": "CETTA_EXACT_INTEGER_V1_SUB",
    "integer-multiply": "CETTA_EXACT_INTEGER_V1_MUL",
    "integer-remainder": "CETTA_EXACT_INTEGER_V1_TREM",
    "integer-floor-modulo": "CETTA_EXACT_INTEGER_V1_FREM",
}

REGISTER_INSTRUCTION_C: dict[str, tuple[str, str]] = {
    **{
        instruction: (
            "exact-integer",
            f"if (!cetta_exact_integer_big({operation}, left, right, "
            "integer_out)) { return false; }")
        for instruction, operation in REGISTER_INSTRUCTION_OPERATION.items()
    },
    "integer-equal": ("boolean", "*boolean_out = mpz_cmp(left, right) == 0;"),
    "integer-less": ("boolean", "*boolean_out = mpz_cmp(left, right) < 0;"),
    "integer-greater": ("boolean", "*boolean_out = mpz_cmp(left, right) > 0;"),
    "integer-less-equal": (
        "boolean", "*boolean_out = mpz_cmp(left, right) <= 0;"),
    "integer-greater-equal": (
        "boolean", "*boolean_out = mpz_cmp(left, right) >= 0;"),
}

# Tagged-small-integer interpretation of the same semantic instructions.
# Arithmetic promotes to the GMP arm exactly when the exact result does not
# fit; a zero divisor declines on both arms; comparisons never promote.
REGISTER_INSTRUCTION_SMALL_C: dict[str, str] = {
    **{
        instruction: (
            f"switch (cetta_exact_integer_small({operation}, left, right, "
            "integer_out)) { "
            "case CETTA_EXACT_INTEGER_UNDEFINED: return false; "
            "case CETTA_EXACT_INTEGER_PROMOTE: *promote_out = true; break; "
            "case CETTA_EXACT_INTEGER_VALUE: break; }")
        for instruction, operation in REGISTER_INSTRUCTION_OPERATION.items()
    },
    "integer-equal": "*boolean_out = left == right;",
    "integer-less": "*boolean_out = left < right;",
    "integer-greater": "*boolean_out = left > right;",
    "integer-less-equal": "*boolean_out = left <= right;",
    "integer-greater-equal": "*boolean_out = left >= right;",
}

REGISTER_INSTRUCTION_ATOM_C: dict[str, tuple[str, str]] = {
    "atom-equal": ("boolean", "*boolean_out = atom_value_eq(left, right);"),
}

# Native instruction schemes for the prepared-pure intrinsic leaf.  The
# authored presentation selects concrete heads and operand disciplines; this
# set states only which abstract instructions the C backend can execute.
PREPARED_PURE_INTRINSIC_INSTRUCTIONS = {
    "grounded-dispatch",
    "boolean-dispatch",
    "deconstruct-nonempty-expression",
    "construct-expression-cons",
    "concatenate-expressions",
    "library-value-dispatch",
}
PREPARED_PURE_INTRINSIC_OPERAND_DISCIPLINES = {"strict-all", "short-circuit"}
# The dialects an intrinsic row can be scoped to, in mask-bit order.
INTRINSIC_DIALECTS = ("he", "petta", "prime")

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
    "bindings": "GV_BINDINGS",
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
        service_admission = find_form(forms, "gslt", "native-service-admission")
        lifetime = find_form(forms, "gslt", "resource-lifetime")
        prepared = find_form(forms, "gslt", "determinate-equation-continuation")
        total = find_form(forms, "gslt", "total-expression-normalization")
        allocation = find_form(forms, "gslt", "value-allocation")
        register = find_form(forms, "gslt", "register-expression-execution")
        intrinsic = find_form(
            forms, "gslt", "prepared-pure-intrinsic-execution")
        fold = find_form(forms, "gslt", "determinate-fold-continuation")
        control_plans = find_form(forms, "gslt", "execution-control-plans")
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
        placement_values = [
            atom_text(item[1], "native service placement")
            for item in entries(service_admission, "placement")
            if len(item) == 2]
        progress_values = [
            atom_text(item[1], "native service progress")
            for item in entries(service_admission, "progress")
            if len(item) == 2]
        if (len(set(placement_values)) != len(placement_values) or
                len(set(progress_values)) != len(progress_values)):
            raise SpecError("native service advice values must be duplicate-free")
        if set(placement_values) != {"local", "coordinator", "unresolved"}:
            raise SpecError("native service placement must declare local, coordinator and unresolved")
        if set(progress_values) != {
                "bounded", "yielding", "isolated", "unresolved-progress"}:
            raise SpecError("native service progress declaration is incomplete")
        self.service_placement_values = placement_values
        self.service_progress_values = progress_values
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

        defaults = entries(native, "native-call-default")
        if defaults != [["native-call-default", "unresolved", "unresolved-progress"]]:
            raise SpecError("native call default must be unresolved/unresolved-progress")
        constructors = entries(native, "native-call-constructor")
        if len(constructors) != 1 or len(constructors[0]) != 4:
            raise SpecError("native call constructor needs placement, progress and bound")
        user_equations = entries(native, "native-call-user-equation")
        if len(user_equations) != 1 or len(user_equations[0]) != 3:
            raise SpecError("native call user-equation needs placement and progress")
        self.native_call_constructor = (
            atom_text(constructors[0][1], "constructor placement"),
            atom_text(constructors[0][2], "constructor progress"),
            atom_text(constructors[0][3], "constructor bound"))
        self.native_call_user_equation = (
            atom_text(user_equations[0][1], "user-equation placement"),
            atom_text(user_equations[0][2], "user-equation progress"))
        if self.native_call_constructor[:2] != ("local", "bounded") or \
                not self.native_call_constructor[2].isdigit() or \
                int(self.native_call_constructor[2]) < 1:
            raise SpecError("constructor calls must be local with a positive bounded contract")
        if self.native_call_user_equation != ("local", "yielding"):
            raise SpecError("user equations must be local yielding calls")
        self.native_call_heads: list[tuple[str, str, str, str, int]] = []
        seen_native_call_fields: set[str] = set()
        for entry in entries(native, "native-call-head"):
            if len(entry) not in (4, 5):
                raise SpecError(f"malformed native-call-head: {sexpr(entry)}")
            name = atom_text(entry[1], "native call head")
            placement = atom_text(entry[2], "native call placement")
            progress = atom_text(entry[3], "native call progress")
            if name not in symbol_fields:
                raise SpecError(f"native call head {name} has no entry in {SYMBOL_TABLE.name}")
            if placement not in placement_values or progress not in progress_values:
                raise SpecError(f"native call head {name} has unknown advice")
            bound = 0
            if progress == "bounded":
                if len(entry) != 5 or not atom_text(entry[4], "native call bound").isdigit():
                    raise SpecError(f"bounded native call head {name} needs a positive bound")
                bound = int(atom_text(entry[4], "native call bound"))
                if bound < 1:
                    raise SpecError(f"bounded native call head {name} needs a positive bound")
            elif len(entry) != 4:
                raise SpecError(f"unbounded native call head {name} cannot carry a bound")
            field = symbol_fields[name]
            if field in seen_native_call_fields:
                raise SpecError(f"native call head {name} is declared twice")
            if placement == "local" and progress not in ("bounded", "yielding"):
                raise SpecError(f"local native call head {name} lacks inline progress")
            if progress == "isolated" and placement != "coordinator":
                raise SpecError(f"isolated native call head {name} must be coordinator-owned")
            seen_native_call_fields.add(field)
            self.native_call_heads.append((name, field, placement, progress, bound))
        if not self.native_call_heads:
            raise SpecError("at least one native call head is required")
        relational_fields = {field for _, field, _ in self.heads}
        conflicting_local = relational_fields & {
            field for _, field, placement, _, _ in self.native_call_heads
            if placement == "local"}
        if conflicting_local:
            raise SpecError(
                "a store-effect head cannot be worker-local: " +
                repr(sorted(conflicting_local)))

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

        # A row without a dialect scope, like one naming every dialect, holds
        # in every language.  Rows for one head and arity may differ in
        # operand discipline between dialects, so each row carries its own
        # scope and no dialect sees two rows.
        self.prepared_intrinsic_heads: list[
            tuple[str, str, int, str, str, tuple[str, ...]]
        ] = []
        for entry in entries(native, "prepared-pure-intrinsic-head"):
            if len(entry) not in (5, 6):
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
            dialects = INTRINSIC_DIALECTS
            if len(entry) == 6:
                scope = entry[5]
                if not isinstance(scope, list) or len(scope) < 2 or scope[0] != "dialects":
                    raise SpecError("intrinsic scope must name nonempty dialects")
                dialects = tuple(atom_text(value, "intrinsic dialect") for value in scope[1:])
                if (not set(dialects) <= set(INTRINSIC_DIALECTS) or
                        len(dialects) != len(set(dialects))):
                    raise SpecError("intrinsic dialects must be unique known language names")
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
            # A connective computes a truth value: the open case runs the
            # canonical operation on both values and answers in the
            # caller's Boolean constructor.
            if discipline == "short-circuit" and (
                    int(arity_text) != 2 or
                    instruction != "boolean-dispatch"):
                raise SpecError(
                    f"short-circuit head {name} must be a binary "
                    "boolean dispatch")
            self.prepared_intrinsic_heads.append((
                name, symbol_fields[name], int(arity_text), discipline,
                instruction, dialects))
        if not self.prepared_intrinsic_heads:
            raise SpecError(
                "at least one prepared-pure intrinsic head is required")
        if len({(field, arity, dialect)
                for _, field, arity, _, _, dialects
                in self.prepared_intrinsic_heads
                for dialect in dialects}) != \
                sum(len(dialects) for *_, dialects
                    in self.prepared_intrinsic_heads):
            raise SpecError(
                "prepared-pure intrinsic heads must be unique by symbol, "
                "arity and dialect")
        if {instruction for _, _, _, _, instruction, _
                in self.prepared_intrinsic_heads} != \
                set(self.prepared_intrinsic_instructions):
            raise SpecError(
                "native prepared-pure intrinsic heads must cover every "
                "declared intrinsic instruction")
        self.short_circuit_absorbing: list[tuple[str, str, int, bool]] = []
        for entry in entries(native, "short-circuit-absorbing"):
            if len(entry) != 4:
                raise SpecError(
                    f"malformed short-circuit-absorbing: {sexpr(entry)}")
            name = atom_text(entry[1], "short-circuit head")
            arity_text = atom_text(entry[2], "short-circuit arity")
            value = atom_text(entry[3], "short-circuit absorbing value")
            if value not in ("True", "False"):
                raise SpecError(
                    f"short-circuit head {name} must absorb a truth value")
            if not any(row_name == name and str(arity) == arity_text and
                       discipline == "short-circuit"
                       for row_name, _, arity, discipline, _, _
                       in self.prepared_intrinsic_heads):
                raise SpecError(
                    f"short-circuit-absorbing names {name}/{arity_text}, "
                    "which no short-circuit head declares")
            self.short_circuit_absorbing.append(
                (name, symbol_fields[name], int(arity_text),
                 value == "True"))
        if len({(field, arity) for _, field, arity, _
                in self.short_circuit_absorbing}) != \
                len(self.short_circuit_absorbing):
            raise SpecError("short-circuit absorbing values must be unique")
        for name, field, arity, discipline, _, _ in \
                self.prepared_intrinsic_heads:
            if discipline == "short-circuit" and not any(
                    absorbing_field == field and absorbing_arity == arity
                    for _, absorbing_field, absorbing_arity, _
                    in self.short_circuit_absorbing):
                raise SpecError(
                    f"short-circuit head {name} declares no absorbing value")

        self.sequence_value_constructor_heads: list[tuple[str, str, int, int]] = []
        for entry in entries(native, "sequence-value-constructor-head"):
            if len(entry) != 4:
                raise SpecError(
                    f"malformed sequence-value-constructor-head: {sexpr(entry)}")
            name = atom_text(entry[1], "sequence value constructor head")
            arities_form = entry[2]
            if not is_form(arities_form, "arities") or len(arities_form) < 2:
                raise SpecError("sequence value constructor must declare arities")
            arities = [atom_text(value, "sequence constructor arity")
                       for value in arities_form[1:]]
            if any(not arity.isdigit() or not 0 <= int(arity) <= 16
                   for arity in arities):
                raise SpecError("sequence constructor arities must be unique in [0, 16]")
            numeric_arities = [int(arity) for arity in arities]
            if len(set(numeric_arities)) != len(numeric_arities):
                raise SpecError("sequence constructor arities must be unique in [0, 16]")
            arity_mask = sum(1 << arity for arity in numeric_arities)
            mask_text = atom_text(entry[3], "sequence held-operand mask")
            if name not in symbol_fields:
                raise SpecError(
                    f"sequence value constructor head {name} has no symbol-table entry")
            if not mask_text.isdigit() or not 0 <= int(mask_text) <= 0xffff:
                raise SpecError("sequence held-operand mask must be in [0, 65535]")
            if int(mask_text) >> max(numeric_arities):
                raise SpecError("sequence held operand is outside the declared arities")
            self.sequence_value_constructor_heads.append(
                (name, symbol_fields[name], arity_mask, int(mask_text)))
        if len({field for _, field, _, _ in self.sequence_value_constructor_heads}) != \
                len(self.sequence_value_constructor_heads):
            raise SpecError("sequence value constructor heads must be unique")
        for _, field, _, mask in self.sequence_value_constructor_heads:
            if mask and any(intrinsic_field == field
                            for _, intrinsic_field, _, _, _, _
                            in self.prepared_intrinsic_heads):
                raise SpecError(
                    "a held-operand constructor cannot have strict-all prepared admission")

        self.control_operations = declared_values(control_plans, "control operation")
        slot_shapes = {
            "sequence": (True, True, True),
            "branch": (True, True, None),
            "sequence-many": (True, False, True),
            "force": (True, False, False),
            "return-scope": (True, False, False),
            "ordered-choice": (True, False, False),
            "failure": (False, False, False),
            "scoped-commit": (False, False, False),
            "handle": (True, False, False),
            "return-value": (False, False, False),
            "construct": (False, False, False),
            "invoke": (False, False, False),
            "sequence-first": (False, False, False),
            "sequence-last": (False, False, False),
            "collect": (True, False, False),
            "match-branches": (True, True, False),
        }
        if set(self.control_operations) != set(slot_shapes):
            raise SpecError("control operations must have a native operand-layout scheme")
        fold_schemes = {
            ("sequence", 3, (1, 0, 2)): "bind",
            ("branch", 3, (0, 1, 2)): "branch",
            ("sequence-many", 2, (0, None, 1)): "bind-sequence",
            ("force", 1, (0, None, None)): "evaluate",
            ("return-scope", 1, (0, None, None)): "evaluate",
        }
        self.control_plans = []
        self.control_forms = []
        self.implicit_operations = []
        self.sequence_orders = []
        self.preparation_steps = []
        for entry in entries(control_plans, "preparation-step"):
            if (len(entry) != 3 or not isinstance(entry[2], str)
                    or not entry[2].isdigit()):
                raise SpecError("a preparation step needs a name and integer position")
            self.preparation_steps.append((atom_text(entry[1], "preparation step"), int(entry[2])))
        if (len(self.preparation_steps) != 5 or
                {name for name, _ in self.preparation_steps} !=
                {"install-type", "compute", "check-return", "check-type", "bind"} or
                {position for _, position in self.preparation_steps} != set(range(5))):
            raise SpecError("preparation stages need distinct names and positions covering 0 through 4")
        for entry in entries(control_plans, "sequence-order"):
            if (len(entry) != 5 or not all(isinstance(x, str) for x in entry[2:])
                    or set(entry[2:]) != {"source", "first", "last"} or entry[4] != "last"):
                raise SpecError("a sequence order covers both computations before its body")
            self.sequence_orders.append((atom_text(entry[1], "sequence order"), entry[2:]))
        if ({name for name, _ in self.sequence_orders} != {"source-first", "binder-first"}
                or len(self.sequence_orders) != 2):
            raise SpecError("sequence execution needs its two ordered lowering schemes")
        for entry in entries(control_plans, "implicit-operation"):
            if len(entry) != 2:
                raise SpecError("an implicit operation names exactly one scheme")
            self.implicit_operations.append(atom_text(entry[1], "implicit operation"))
        if (set(self.implicit_operations) != {"return-value", "construct", "invoke"} or
                len(self.implicit_operations) != len(set(self.implicit_operations))):
            raise SpecError("implicit operations must name the native value, constructor and call schemes")
        self.fold_control_heads: list[tuple[str, str, int, str]] = []
        control_entries = [(None, entry) for entry in entries(control_plans, "source-head")]
        for entry in entries(control_plans, "dialect-form"):
            if len(entry) != 7 or entry[1] != "petta":
                raise SpecError("a dialect control form needs a registered dialect and operand layout")
            control_entries.append(("petta", [entry[0], *entry[2:]]))
        native_forms = set(re.findall(r"\bPETTA_FORM_[A-Z][A-Z0-9_]*\b",
            (ROOT / "src/petta_semantics.h").read_text().split("} PeTTaForm;", 1)[0]))
        self.variadic_forms = []
        for entry in entries(control_plans, "variadic-form"):
            if len(entry) != 4 or entry[1] != "petta":
                raise SpecError("a variadic form needs its dialect, name and result policy")
            name = atom_text(entry[2], "variadic form")
            operation = atom_text(entry[3], "variadic operation")
            field = "PETTA_FORM_" + c_enum(name)
            if field not in native_forms or operation not in {"sequence-first", "sequence-last"}:
                raise SpecError("variadic form or operation has no native interpretation")
            self.variadic_forms.append((name, field, operation))
        if (len(self.variadic_forms) != 2 or
                len({field for _, field, _ in self.variadic_forms}) != 2 or
                {operation for _, _, operation in self.variadic_forms} !=
                    {"sequence-first", "sequence-last"}):
            raise SpecError("variadic forms need distinct first/last sequence policies")
        self.invocation_operations = []
        for entry in entries(control_plans, "invocation-operation"):
            if len(entry) != 2:
                raise SpecError("an invocation operation needs one name")
            self.invocation_operations.append(atom_text(entry[1], "invocation operation"))
        if (len(self.invocation_operations) != 8 or set(self.invocation_operations) !=
                {"host", "predicate-value", "relational-member", "list-unary",
                 "list-set", "read-term", "extension", "predicate-call"}):
            raise SpecError("invocation operations need distinct native body interpretations")
        self.invocation_heads, self.invocation_forms = [], []
        invocation_entries = [(None, row) for row in entries(control_plans, "invocation-head")]
        for row in entries(control_plans, "invocation-form"):
            if len(row) != 7 or row[1] != "petta":
                raise SpecError("an invocation form needs a registered dialect and layout")
            invocation_entries.append(("petta", [row[0], *row[2:]]))
        for dialect, row in invocation_entries:
            if len(row) != 6:
                raise SpecError("an invocation needs a head, arity, body and argument range")
            name, arity, operation, first, last = [atom_text(x, "invocation field") for x in row[1:]]
            field = symbol_fields.get(name) if dialect is None else "PETTA_FORM_" + c_enum(name)
            if (not field or operation not in self.invocation_operations or
                    (dialect is not None and field not in native_forms)):
                raise SpecError("invocation head or body has no native interpretation")
            if not all(x.isdigit() for x in [arity, first, last]):
                raise SpecError("invocation arity and coordinates must be nonnegative")
            arity, first, last = map(int, [arity, first, last])
            if not (0 <= first <= last == arity <= 255) or first != 0:
                raise SpecError("a strict invocation prepares exactly its admitted arguments")
            target = self.invocation_heads if dialect is None else self.invocation_forms
            target.append((name, field, arity, operation, first, last))
        for rows in [self.invocation_heads, self.invocation_forms]:
            if not rows or len({(field, arity) for _, field, arity, *_ in rows}) != len(rows):
                raise SpecError("invocation descriptors must be nonempty and unique")
        if {row[3] for row in self.invocation_heads + self.invocation_forms} != set(self.invocation_operations):
            raise SpecError("every invocation body needs an admitted layout")
        for dialect, entry in control_entries:
            if len(entry) != 6 or not is_form(entry[4], "operands") or not is_form(entry[5], "fold"):
                raise SpecError(f"malformed control source-head: {sexpr(entry)}")
            name = atom_text(entry[1], "control head")
            arity_text = atom_text(entry[2], "control arity")
            operation = atom_text(entry[3], "control operation")
            if operation in {"sequence-first", "sequence-last"}:
                raise SpecError("a variadic series needs its variadic-form descriptor")
            if operation in self.implicit_operations:
                raise SpecError("an implicit operation needs occurrence admission, not a source-head row")
            field = symbol_fields.get(name) if dialect is None else "PETTA_FORM_" + c_enum(name)
            if (not field or operation not in slot_shapes or
                    (dialect is not None and field not in native_forms)):
                raise SpecError("control source head or operation has no native interpretation")
            if not arity_text.isdigit() or not 0 <= int(arity_text) <= 16:
                raise SpecError("control arity must be in [0, 16]")
            arity = int(arity_text)
            coordinates = []
            for item in entry[4][1:]:
                coordinate = atom_text(item, "control coordinate")
                if coordinate == "absent":
                    coordinates.append(None)
                elif coordinate.isdigit() and int(coordinate) < arity:
                    coordinates.append(int(coordinate))
                else:
                    raise SpecError("control coordinate is outside the admitted arity")
            if len(coordinates) != 3:
                raise SpecError("a control plan needs source, first and last coordinates")
            if any(required is not None and (coordinate is not None) != required
                   for required, coordinate in zip(slot_shapes[operation], coordinates)):
                raise SpecError("control coordinate presence disagrees with its operation")
            present = [coordinate for coordinate in coordinates if coordinate is not None]
            if sorted(present) != list(range(arity)):
                raise SpecError("control coordinates must cover each operand exactly once")
            if len(entry[5]) != 2:
                raise SpecError("a control plan names exactly one optional fold lowering")
            lowering = atom_text(entry[5][1], "control fold lowering")
            coordinates = tuple(coordinates)
            if lowering != "none":
                if (dialect is not None or lowering not in self.fold_control_kinds or
                        fold_schemes.get((operation, arity, coordinates)) != lowering):
                    raise SpecError("control plan has no matching fold lowering scheme")
                self.fold_control_heads.append((name, symbol_fields[name], arity, lowering))
            target = self.control_plans if dialect is None else self.control_forms
            target.append((name, field, arity, operation, coordinates, lowering))
        if (not self.control_plans or
                len({(field, arity) for _, field, arity, _, _, _ in self.control_plans}) != len(self.control_plans)):
            raise SpecError("control plans must be nonempty and unique by source head and arity")
        if len({(field, arity) for _, field, arity, _, _, _ in self.control_forms}) != len(self.control_forms):
            raise SpecError("dialect control forms must be unique by form and arity")
        if ({operation for _, _, _, operation, _, _ in self.control_plans + self.control_forms}
                | {operation for _, _, operation in self.variadic_forms}
                | set(self.implicit_operations)) != set(self.control_operations):
            raise SpecError("control source plans must cover every declared operation")

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
        if len(user_head_uncertain) != 1 or len(user_head_uncertain[0]) < 2:
            raise SpecError("native interpretation needs one user-head-uncertain")
        # An uncertain head may do anything an effect names: the join of the
        # values listed, which must be every non-unit value.
        self.user_head_uncertain = [
            atom_text(value, "uncertain user-head effect")
            for value in user_head_uncertain[0][1:]]
        if sorted(self.user_head_uncertain) != sorted(self.effect_flags):
            raise SpecError(
                "an uncertain user head must join every non-unit effect")
        user_head_inert = entries(native, "user-head-inert")
        if user_head_inert != [["user-head-inert", "pure"]]:
            raise SpecError("inert user-head symbols must map to pure")
        inert_children = entries(native, "inert-expression-children")
        if inert_children != [["inert-expression-children", "evaluated"]]:
            raise SpecError(
                "an inert expression's children are evaluated, in HE and "
                "PeTTa; their effects count")
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
        self.matched_operands: list[tuple[str, int, list[int], str]] = []
        for entry in entries(native, "matched-operands"):
            if len(entry) < 5:
                raise SpecError(
                    f"malformed matched-operands: {sexpr(entry)}")
            language = atom_text(entry[1], "matched operand language")
            if language not in ("he", "all"):
                raise SpecError(
                    f"matched operands name an unknown language {language}")
            name = atom_text(entry[2], "matched operand head")
            arity_text = atom_text(entry[3], "matched operand arity")
            indices = [atom_text(item, "matched operand index")
                       for item in entry[4:]]
            if name not in symbol_fields:
                raise SpecError(
                    f"matched operand head {name} has no symbol-table entry")
            if not arity_text.isdigit() or not 1 <= int(arity_text) <= 16:
                raise SpecError("matched operand arity must be in [1, 16]")
            if (any(not index.isdigit() or int(index) >= int(arity_text)
                    for index in indices) or
                    len(set(indices)) != len(indices)):
                raise SpecError(
                    f"matched operands of {name} must be distinct operand "
                    "indices below its arity")
            if symbol_fields[name] in relational_fields:
                raise SpecError(
                    f"a relational head cannot match operands: {name}")
            self.matched_operands.append(
                (symbol_fields[name], int(arity_text),
                 [int(index) for index in indices], language))
        if self.matched_operands and not any(
                len(item) >= 2 and item[1] == "matched-operand-effect"
                for item in entries(effect, "rule")):
            raise SpecError(
                "matched operands need the matched-operand-effect rule")
        self.pattern_pair_operands: list[tuple[str, int, int, str]] = []
        for entry in entries(native, "pattern-pair-operands"):
            if len(entry) != 5:
                raise SpecError(
                    f"malformed pattern-pair-operands: {sexpr(entry)}")
            language = atom_text(entry[1], "pattern-pair operand language")
            if language not in ("he", "all"):
                raise SpecError(
                    f"pattern-pair operands name an unknown language {language}")
            name = atom_text(entry[2], "pattern-pair operand head")
            arity_text = atom_text(entry[3], "pattern-pair operand arity")
            index_text = atom_text(entry[4], "pattern-pair operand index")
            if name not in symbol_fields:
                raise SpecError(
                    f"pattern-pair operand head {name} has no symbol-table entry")
            if not arity_text.isdigit() or not 1 <= int(arity_text) <= 16:
                raise SpecError("pattern-pair operand arity must be in [1, 16]")
            if not index_text.isdigit() or int(index_text) >= int(arity_text):
                raise SpecError(
                    f"the pattern-pair operand of {name} must be an operand "
                    "index below its arity")
            if symbol_fields[name] in relational_fields:
                raise SpecError(
                    f"a relational head cannot take pattern pairs: {name}")
            key = (symbol_fields[name], int(arity_text), int(index_text))
            if any(item[:3] == key for item in self.pattern_pair_operands):
                raise SpecError(
                    f"pattern-pair operands of {name} are listed twice")
            self.pattern_pair_operands.append((*key, language))
        if self.pattern_pair_operands and not any(
                len(item) >= 2 and item[1] == "pattern-pair-operand-effect"
                for item in entries(effect, "rule")):
            raise SpecError(
                "pattern-pair operands need the pattern-pair-operand-effect rule")

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
    placement_constants = "\n".join(
        f"    CETTA_GSLT_PLACEMENT_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.service_placement_values)
    )
    progress_constants = "\n".join(
        f"    CETTA_GSLT_PROGRESS_{c_enum(value)} = {index},"
        for index, value in enumerate(spec.service_progress_values)
    )
    native_call_rows = " \\\n".join(
        f"    X({field}, CETTA_GSLT_PLACEMENT_{c_enum(placement)}, "
        f"CETTA_GSLT_PROGRESS_{c_enum(progress)}, {bound}u)"
        for _, field, placement, progress, bound in spec.native_call_heads
    )
    constructor_placement, constructor_progress, constructor_bound = \
        spec.native_call_constructor
    user_placement, user_progress = spec.native_call_user_equation
    opaque_expression_head_rows = " \\\n".join(
        f"    X({field})"
        for _, field in spec.opaque_expression_heads
    )
    opaque_expression_head_disjuncts = " ||\n           ".join(
        f"head == builtins->{field}"
        for _, field in spec.opaque_expression_heads
    )
    matched_operand_rows = " \\\n".join(
        f"    X({field}, {arity}u, {index}u, {language})"
        for field, arity, indices, language in spec.matched_operands
        for index in indices
    )
    matched_operand_disjuncts = " ||\n           ".join(
        "(" + ("" if language == "all" else "he_language && ") +
        f"head == builtins->{field} && arity == {arity}u && "
        f"operand_index == {index}u)"
        for field, arity, indices, language in spec.matched_operands
        for index in indices
    ) or "false"
    pattern_pair_operand_rows = " \\\n".join(
        f"    X({field}, {arity}u, {index}u, {language})"
        for field, arity, index, language in spec.pattern_pair_operands
    )
    pattern_pair_operand_disjuncts = " ||\n           ".join(
        "(" + ("" if language == "all" else "he_language && ") +
        f"head == builtins->{field} && arity == {arity}u && "
        f"operand_index == {index}u)"
        for field, arity, index, language in spec.pattern_pair_operands
    ) or "false"
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
        f"CETTA_GSLT_PREPARED_PURE_INTRINSIC_{c_enum(instruction)}, " +
        ("CETTA_GSLT_INTRINSIC_DIALECT_ALL"
         if set(dialects) == set(INTRINSIC_DIALECTS) else
         " | ".join(
             f"CETTA_GSLT_INTRINSIC_DIALECT_{c_enum(dialect)}"
             for dialect in dialects)) + ")"
        for _, field, arity, discipline, instruction, dialects
        in spec.prepared_intrinsic_heads
    )
    short_circuit_rows = " \\\n".join(
        f"    X({field}, {arity}u, {'true' if absorbing else 'false'})"
        for _, field, arity, absorbing in spec.short_circuit_absorbing
    )
    short_circuit_macro = \
        "#define CETTA_GSLT_PREPARED_PURE_SHORT_CIRCUIT_ROWS(X)"
    if short_circuit_rows:
        short_circuit_macro += " \\\n" + short_circuit_rows
    intrinsic_dialect_constants = "\n".join(
        f"    CETTA_GSLT_INTRINSIC_DIALECT_{c_enum(dialect)} = {1 << index}u,"
        for index, dialect in enumerate(INTRINSIC_DIALECTS)
    ) + f"\n    CETTA_GSLT_INTRINSIC_DIALECT_ALL = " \
        f"{(1 << len(INTRINSIC_DIALECTS)) - 1}u,"
    sequence_value_constructor_rows = " \\\n".join(
        f"    X({field}, {arity_mask}u, {mask}u)"
        for _, field, arity_mask, mask in spec.sequence_value_constructor_heads
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
    control_operation_constants = "\n".join(
        f"    CETTA_CONTROL_{c_enum(value)} = {index + 1},"
        for index, value in enumerate(spec.control_operations))
    def control_coordinate(value):
        return "CETTA_CONTROL_OPERAND_ABSENT" if value is None else f"{value}u"
    control_plan_rows = " \\\n".join(
        f"    X({field}, {arity}u, CETTA_CONTROL_{c_enum(operation)}, " +
        ", ".join(control_coordinate(value) for value in coordinates) + ")"
        for _, field, arity, operation, coordinates, _ in spec.control_plans)
    control_form_rows = " \\\n".join(
        f"    X({field}, {arity}u, CETTA_CONTROL_{c_enum(operation)}, " +
        ", ".join(control_coordinate(value) for value in coordinates) + ")"
        for _, field, arity, operation, coordinates, _ in spec.control_forms)
    variadic_form_rows = " ".join(
        f"X({field}, CETTA_CONTROL_{c_enum(operation)})"
        for _, field, operation in spec.variadic_forms)
    implicit_plan_definitions = "\n".join(
        f"#define CETTA_CONTROL_{c_enum(operation)}_PLAN "
        f"((CettaControlOperationPlan){{CETTA_CONTROL_{c_enum(operation)}, "
        "CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT})"
        for operation in spec.implicit_operations)
    control_form_cases = "\n".join(
        f"    case CETTA_CONTROL_{c_enum(operation)}:"
        for operation in spec.control_operations if operation not in spec.implicit_operations)
    sequence_order_constants = "\n".join(
        f"    CETTA_SEQUENCE_{c_enum(name)} = {index},"
        for index, (name, _) in enumerate(spec.sequence_orders))
    preparation_constants = "\n".join(
        f"    CETTA_CALL_PREP_{c_enum(name)} = 1u << {position}u,"
        for name, position in sorted(spec.preparation_steps, key=lambda row: row[1]))
    preparation_reverse_rows = " \\\n".join(
        f"    X(CETTA_CALL_PREP_{c_enum(name)})"
        for name, _ in sorted(spec.preparation_steps, key=lambda row: row[1], reverse=True))
    invocation_constants = "\n".join(
        f"    CETTA_INVOCATION_{c_enum(name)} = {index + 1},"
        for index, name in enumerate(spec.invocation_operations))
    def invocation_rows(rows):
        return " \\\n".join(
            f"    X({field}, {arity}u, CETTA_INVOCATION_{c_enum(operation)}, {first}u, {last}u)"
            for _, field, arity, operation, first, last in rows)
    invocation_head_rows = invocation_rows(spec.invocation_heads)
    invocation_form_rows = invocation_rows(spec.invocation_forms)
    sequence_order_cases = "\n".join(
        f"    case CETTA_SEQUENCE_{c_enum(name)}:\n"
        "        result = (CettaSequenceOrderPlan){.children = {" +
        ", ".join("plan." + coordinate for coordinate in coordinates) +
        "}}; break;"
        for name, coordinates in spec.sequence_orders)
    control_fold_cases = "\n".join(
        f"    if (plan.operation == CETTA_CONTROL_{c_enum(operation)} &&\n"
        f"        plan.source == {control_coordinate(coordinates[0])} &&\n"
        f"        plan.first == {control_coordinate(coordinates[1])} &&\n"
        f"        plan.last == {control_coordinate(coordinates[2])}) {{\n"
        f"        if (control_out) *control_out = CETTA_GSLT_FOLD_CONTROL_{c_enum(lowering)};\n"
        f"        return true;\n    }}"
        for _, _, _, operation, coordinates, lowering in spec.control_plans
        if lowering != "none")
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
    uncertain_union = " | ".join(
        f"CETTA_GSLT_QUERY_EFFECT_{c_enum(value)}"
        for value in spec.user_head_uncertain)
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

#include "../exact_integer.h"

typedef enum {{
{effect_enum}
}} CettaGsltQueryEffect;

typedef enum {{
{placement_constants}
}} CettaGsltWorkerPlacementAdvice;

typedef enum {{
{progress_constants}
}} CettaGsltProgressAdvice;

typedef struct {{
    CettaGsltWorkerPlacementAdvice placement;
    CettaGsltProgressAdvice progress;
    uint64_t bound;
}} CettaGsltNativeCallAdvice;

#define CETTA_GSLT_NATIVE_CALL_HEAD_ROWS(X) \\
{native_call_rows}

#define CETTA_GSLT_NATIVE_CALL_DEFAULT \\
    ((CettaGsltNativeCallAdvice){{CETTA_GSLT_PLACEMENT_UNRESOLVED, \\
        CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u}})
#define CETTA_GSLT_NATIVE_CALL_CONSTRUCTOR \\
    ((CettaGsltNativeCallAdvice){{CETTA_GSLT_PLACEMENT_{c_enum(constructor_placement)}, \\
        CETTA_GSLT_PROGRESS_{c_enum(constructor_progress)}, {constructor_bound}u}})
#define CETTA_GSLT_NATIVE_CALL_USER_EQUATION \\
    ((CettaGsltNativeCallAdvice){{CETTA_GSLT_PLACEMENT_{c_enum(user_placement)}, \\
        CETTA_GSLT_PROGRESS_{c_enum(user_progress)}, 0u}})

static inline bool cetta_gslt_native_call_head_advice(
    SymbolId head, const BuiltinSyms *builtins, CettaGsltNativeCallAdvice *out) {{
    if (!builtins || !out) return false;
#define CETTA_GSLT_NATIVE_CALL_ADVICE_CASE(field, placement, progress, bound) \\
    if (head == builtins->field) {{ \\
        *out = (CettaGsltNativeCallAdvice){{placement, progress, bound}}; \\
        return true; \\
    }}
    CETTA_GSLT_NATIVE_CALL_HEAD_ROWS(CETTA_GSLT_NATIVE_CALL_ADVICE_CASE)
#undef CETTA_GSLT_NATIVE_CALL_ADVICE_CASE
    *out = CETTA_GSLT_NATIVE_CALL_DEFAULT;
    return false;
}}

static inline bool cetta_gslt_progress_inline(CettaGsltProgressAdvice progress) {{
    return progress == CETTA_GSLT_PROGRESS_BOUNDED ||
           progress == CETTA_GSLT_PROGRESS_YIELDING;
}}

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
    ((CettaGsltQueryEffect)({uncertain_union}))
#define CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL \
    CETTA_GSLT_QUERY_EFFECT_PURE

#define CETTA_GSLT_OPAQUE_EXPRESSION_HEAD_ROWS(X) \
{opaque_expression_head_rows}

static inline bool cetta_gslt_query_effect_children_opaque(
    SymbolId head, const BuiltinSyms *builtins) {{
    return builtins &&
           ({opaque_expression_head_disjuncts});
}}

/* Operands a head compares with a pattern and never evaluates, in the
 * language each row names. */
#define CETTA_GSLT_MATCHED_OPERAND_ROWS(X) \
{matched_operand_rows}

static inline bool cetta_gslt_query_effect_operand_matched(
    SymbolId head, uint32_t arity, uint32_t operand_index,
    bool he_language, const BuiltinSyms *builtins) {{
    return builtins &&
           ({matched_operand_disjuncts});
}}

/* Operands a head reads as a list of (pattern value) pairs: a pattern is
 * matched and never evaluated; a value is evaluated where its pattern
 * matches, so its calls count. */
#define CETTA_GSLT_PATTERN_PAIR_OPERAND_ROWS(X) \
{pattern_pair_operand_rows}

static inline bool cetta_gslt_query_effect_operand_pattern_pairs(
    SymbolId head, uint32_t arity, uint32_t operand_index,
    bool he_language, const BuiltinSyms *builtins) {{
    (void)he_language;
    return builtins &&
           ({pattern_pair_operand_disjuncts});
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

/* Decode immutable operation metadata. Dialect availability, source authority
 * and operand readiness are checked independently by each consumer. */
static inline bool cetta_gslt_register_head_program(
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *kind_out,
    CettaGsltRegisterInstruction *instruction_out,
    const BuiltinSyms *builtins) {{
    if (!builtins || head == SYMBOL_ID_NONE)
        return false;
#define CETTA_REGISTER_HEAD(field, expected_arity, result_kind, instruction) \\
    if (head == builtins->field && arity == (expected_arity)) {{ \\
        if (kind_out) *kind_out = (result_kind); \\
        if (instruction_out) *instruction_out = (instruction); \\
        return true; \\
    }}
    CETTA_GSLT_REGISTER_HEAD_ROWS(CETTA_REGISTER_HEAD)
#undef CETTA_REGISTER_HEAD
    return false;
}}

typedef enum {{
{prepared_intrinsic_instruction_constants}
}} CettaGsltPreparedPureIntrinsicInstruction;

typedef enum {{
{prepared_intrinsic_operand_constants}
}} CettaGsltPreparedPureIntrinsicOperandDiscipline;

typedef enum {{
{intrinsic_dialect_constants}
}} CettaGsltPreparedPureIntrinsicDialect;

/* X(head, arity, operand discipline, instruction, dialects): no dialect
 * reads two rows for one head and arity. */
#define CETTA_GSLT_PREPARED_PURE_INTRINSIC_HEAD_ROWS(X) \
{prepared_intrinsic_head_rows}

/* X(head, arity, absorbing truth value) for every short-circuit row. */
{short_circuit_macro}

/* The row that `dialect`, one CETTA_GSLT_INTRINSIC_DIALECT_* bit, reads for
 * `head` at `arity`: its operand discipline and instruction.  A row scoped
 * to every dialect holds in every language.  This is operation metadata,
 * not source authority or permission to speculate. */
static inline bool cetta_gslt_prepared_intrinsic_program(
    SymbolId head, CettaExprLen arity, uint32_t dialect,
    CettaGsltPreparedPureIntrinsicOperandDiscipline *discipline_out,
    CettaGsltPreparedPureIntrinsicInstruction *instruction_out,
    const BuiltinSyms *builtins) {{
    if (!builtins || head == SYMBOL_ID_NONE)
        return false;
#define CETTA_INTRINSIC_HEAD(field, expected_arity, discipline, instruction, \\
                             dialects) \\
    if (head == builtins->field && arity == (expected_arity) && \\
        ((dialects) == CETTA_GSLT_INTRINSIC_DIALECT_ALL || \\
         ((dialects) & dialect) != 0u)) {{ \\
        if (discipline_out) *discipline_out = (discipline); \\
        if (instruction_out) *instruction_out = (instruction); \\
        return true; \\
    }}
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_HEAD_ROWS(CETTA_INTRINSIC_HEAD)
#undef CETTA_INTRINSIC_HEAD
    return false;
}}

#define CETTA_GSLT_SEQUENCE_VALUE_CONSTRUCTOR_ROWS(X) \
{sequence_value_constructor_rows}

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

/* Source control plans carry only operation and operand coordinates. Source
 * authority, lexical owners, demand and result completion remain independent.
 * Absence is metadata, never an ordinary Empty value. */
typedef enum {{
    CETTA_CONTROL_NONE = 0,
{control_operation_constants}
}} CettaControlOperation;

#define CETTA_CONTROL_OPERAND_ABSENT UINT8_MAX
typedef struct {{
    uint8_t operation;
    uint8_t source;
    uint8_t first;
    uint8_t last;
}} CettaControlOperationPlan;
_Static_assert(sizeof(CettaControlOperationPlan) == 4u, "control plan must remain four bytes");

{implicit_plan_definitions}

/* Control-form plans are distinct from occurrence-level value,
 * construction and invocation plans. This grants no callee admission. */
static inline bool cetta_control_operation_is_control_form(CettaControlOperationPlan plan) {{
    switch (plan.operation) {{
{control_form_cases}
        return true;
    default:
        return false;
    }}
}}

/* The two three-operand sequence layouts have different source positions.
 * Physical lowerings can admit one layout without changing the operation. */
static inline bool cetta_control_operation_binding_layout(
    CettaControlOperationPlan plan, uint8_t producer_index) {{
    return producer_index <= 1u && plan.operation == CETTA_CONTROL_SEQUENCE &&
        plan.source == producer_index && plan.first == 1u - producer_index &&
        plan.last == 2u;
}}

/* Match selection retains two distinct source occurrences. */
static inline bool cetta_control_operation_match_branches_layout(
        CettaControlOperationPlan plan, CettaExprLen arity) {{
    return plan.operation == CETTA_CONTROL_MATCH_BRANCHES &&
        plan.source < arity && plan.first < arity &&
        plan.source != plan.first && plan.last == CETTA_CONTROL_OPERAND_ABSENT;
}}

/* Variadic series select a result without bounding their argument count.
 * Zero denotes absent metadata, never a source argument coordinate. */
static inline CettaExprIndex cetta_control_operation_series_result(
        CettaControlOperationPlan plan, CettaExprLen arity) {{
    if (!arity) return 0u;
    if (plan.operation == CETTA_CONTROL_SEQUENCE_FIRST) return 1u;
    if (plan.operation == CETTA_CONTROL_SEQUENCE_LAST) return arity;
    return 0u;
}}

/* Execution order is selected by admitted source policy. The same program
 * lowers to ordinary goals or activation goals without rereading syntax. */
typedef enum {{
{sequence_order_constants}
}} CettaSequenceOrder;

typedef struct {{ uint8_t children[3]; }} CettaSequenceOrderPlan;

/* A preparation plan selects stages, independently of values or ownership.
 * Type installation and completed-value checking retain distinct positions. */
typedef enum {{
{preparation_constants}
}} CettaCallPreparationStep;
typedef uint8_t CettaCallPreparationPlan;
_Static_assert(sizeof(CettaCallPreparationPlan) == 1u, "preparation plan must remain one byte");

static inline bool cetta_call_preparation_valid(CettaCallPreparationPlan plan) {{
    return (plan & (uint8_t)~31u) == 0u;
}}

#define CETTA_CALL_PREPARATION_REVERSE_ROWS(X) \
{preparation_reverse_rows}

/* Argument preparation and body entry are independent of callee admission. */
typedef enum {{
    CETTA_INVOCATION_NONE = 0,
{invocation_constants}
}} CettaInvocationOperation;

typedef struct {{ uint8_t operation, first, last; }} CettaInvocationPlan;
_Static_assert(sizeof(CettaInvocationPlan) == 3u, "invocation plan must remain three bytes");

/* One computation carries its control operation and optional native body.
 * Source authority, live demand and ownership are supplied by its context. */
typedef struct {{
    CettaControlOperationPlan control;
    CettaInvocationPlan invocation;
}} CettaComputationPlan;
_Static_assert(sizeof(CettaComputationPlan) == 7u, "computation plan must remain seven bytes");

static inline uint32_t cetta_invocation_plan_key(CettaInvocationPlan plan) {{
    return (uint32_t)plan.operation | (uint32_t)plan.first << 8u |
        (uint32_t)plan.last << 16u;
}}

#define CETTA_INVOCATION_HEAD_ROWS(X) \
{invocation_head_rows}

#define CETTA_PETTA_INVOCATION_FORM_ROWS(X) \
{invocation_form_rows}

/* Refusal neither reads operands nor changes the supplied plan. */
static inline bool cetta_invocation_plan_decode(
    SymbolId head, CettaExprLen arity, CettaInvocationPlan *out,
    const BuiltinSyms *builtins) {{
    if (!builtins || head == SYMBOL_ID_NONE)
        return false;
#define CETTA_INVOCATION_HEAD(field, count, body, start, end) \\
    if (head == builtins->field && arity == (count)) {{ \\
        if (out) *out = (CettaInvocationPlan){{(body), (start), (end)}}; \\
        return true; \\
    }}
    CETTA_INVOCATION_HEAD_ROWS(CETTA_INVOCATION_HEAD)
#undef CETTA_INVOCATION_HEAD
    return false;
}}

static inline bool cetta_control_sequence_plan(CettaControlOperationPlan plan,
        CettaSequenceOrder order, CettaSequenceOrderPlan *out) {{
    if (!out || !cetta_control_operation_binding_layout(plan, plan.source))
        return false;
    CettaSequenceOrderPlan result;
    switch (order) {{
{sequence_order_cases}
    default: return false;
    }}
    *out = result;
    return true;
}}

static inline uint32_t cetta_control_operation_key(CettaControlOperationPlan plan) {{
    return (uint32_t)plan.operation | (uint32_t)plan.source << 8u |
        (uint32_t)plan.first << 16u | (uint32_t)plan.last << 24u;
}}

#define CETTA_CONTROL_PLAN_HEAD_ROWS(X) \
{control_plan_rows}

/* Dialect classification admits these forms before reading their operands.
 * The enum names are expanded only by the owning dialect adapter. */
#define CETTA_PETTA_CONTROL_FORM_ROWS(X) \
{control_form_rows}

#define CETTA_PETTA_VARIADIC_FORM_ROWS(X) {variadic_form_rows}

/* Arity admission performs no operand read. Refusal leaves the output intact. */
static inline bool cetta_control_operation_decode(
    SymbolId head, CettaExprLen arity, CettaControlOperationPlan *plan_out,
    const BuiltinSyms *builtins) {{
    if (!builtins || head == SYMBOL_ID_NONE)
        return false;
#define CETTA_CONTROL_HEAD(field, expected_arity, op, source_arg, first_arg, last_arg) \\
    if (head == builtins->field && arity == (expected_arity)) {{ \\
        if (plan_out) *plan_out = (CettaControlOperationPlan){{ \\
            .operation = (op), .source = (source_arg), \\
            .first = (first_arg), .last = (last_arg)}}; \\
        return true; \\
    }}
    CETTA_CONTROL_PLAN_HEAD_ROWS(CETTA_CONTROL_HEAD)
#undef CETTA_CONTROL_HEAD
    return false;
}}

/* This lowering is used only after the caller admits the pure fold fragment.
 * Its direct body entry does not erase a general function's return scope. */
static inline bool cetta_control_operation_lower_fold(
    CettaControlOperationPlan plan, CettaGsltFoldControl *control_out) {{
{control_fold_cases}
    return false;
}}

/* The physical fold instruction inventory is derived from the same plans. */
#define CETTA_GSLT_FOLD_CONTROL_HEAD_ROWS(X) \
{fold_control_rows}

static inline bool cetta_gslt_fold_control_program(
    SymbolId head, CettaExprLen arity, CettaGsltFoldControl *control_out,
    const BuiltinSyms *builtins) {{
    CettaControlOperationPlan plan;
    return cetta_control_operation_decode(head, arity, &plan, builtins) &&
        cetta_control_operation_lower_fold(plan, control_out);
}}

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

/* Shared accelerator admission for call-policy preservation: a call of a
 * head the dialect types falls back to canonical dispatch unless the
 * accelerator implements the head's policy itself, as the prepared machine
 * does for HE's value-class signatures. */
#define CETTA_GSLT_ACCELERATOR_CALL_POLICY_SUPPORTED(space, head, arity) \
    (space_head_call_untyped((space), (head), (arity)))

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


# These are frontend admission contracts, separate from the source fragment's
# fold instructions. Prime must transport its selected Need world through the
# retained return interface; its Atom-only prepared path is not admitted.
FOLD_RUNTIME_RETURN_CARRIERS = {
    "he": "atom",
    "prime": "invocation",
    "petta": "atom",
}


def fold_runtime_expectation(
        language: str, counts: tuple[int | None, ...]) -> tuple[int | None, ...]:
    if FOLD_RUNTIME_RETURN_CARRIERS[language] == "atom":
        return counts
    return tuple(None if count is None else 0 for count in counts)


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
    parser.add_argument("--check-fold-runtime", action="store_true",
                        help="check admitted return paths using a statistics-enabled main build")
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
                    expected_output, *fold_runtime_expectation(language, (
                        spec.fold_expected_admissions,
                        spec.fold_expected_steps, spec.fold_expected_commits,
                        spec.fold_expected_pure_admissions,
                        spec.fold_expected_pure_steps,
                        spec.fold_expected_pure_declines)))
            verify_prepared_fold_runtime(
                args.cetta.resolve(), prime_fold_fixture, "prime",
                prime_fold_expected, *fold_runtime_expectation("prime", (
                    spec.prime_fold_expected_admissions,
                    spec.prime_fold_expected_steps,
                    spec.prime_fold_expected_commits)))
            verify_prepared_fold_runtime(
                args.cetta.resolve(), petta_fold_fixture, "petta",
                petta_fold_expected, *fold_runtime_expectation("petta", (
                    spec.petta_fold_expected_admissions,
                    spec.petta_fold_expected_steps,
                    spec.petta_fold_expected_commits)))
    except (SpecError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    action = "verified" if args.check else "generated"
    print(f"PASS: {action} compositional execution-contract artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
