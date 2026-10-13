#!/usr/bin/env python3
"""Render an explicit typed rewrite-rule view for PeTTaChainer.

The view is a consumer of ModuleRewriteRulesV1, not a language definition or
search engine. Admission uses the supplied live LanguageDef authority; rule
origins remain proof tokens. The caller supplies the knowledge base and its
evidence value explicitly. Constructor equations are not installed in the host.
"""
import argparse
from pathlib import Path

from gslt2parse_schema_v1 import StringLiteral, Symbol, Variable, parse_sexprs, render
from test_lbnf_values_v1 import headed


def render_metta(value):
    """Render variables as MeTTa variables, not the schema codec's ? symbols."""
    if isinstance(value, Variable):
        render(value)  # Check the portable name before changing its sigil.
        return "$" + value.name
    if isinstance(value, tuple):
        return "(" + " ".join(render_metta(item) for item in value) + ")"
    return render(value)


def finite_rows(value):
    rows = []
    while headed(value, "LCons", 2):
        rows.append(value[1])
        value = value[2]
    if value != Symbol("LNil"):
        raise ValueError("Expected a finite declaration list")
    return rows


def export_rules(bundle, authority, namespace, knowledge_base, evidence):
    """Compile the first-order raw rewrite observation, preserving occurrences.

    Queries must bind the source term before admission. Context premises are
    interpreted as the same explicit step relation; no transitive or context
    closure is inserted. Other premise/binding profiles require their own
    declared adapter and are refused atomically here.
    """
    if not headed(bundle, "ModuleRewriteRulesV1", 2) or not headed(bundle[1], "GSLTLanguageDefWireV1", 5):
        raise ValueError("Expected origin-bearing module rewrite rules")
    core, occurrences = bundle[1], finite_rows(bundle[2])
    rows = finite_rows(core[5])
    if any(not headed(row, "ModuleRuleOccurrenceV1", 2) for row in occurrences) or \
            [row[2] for row in occurrences] != rows:
        raise ValueError("Rule occurrences must cover the exact ordered core rules")
    carriers = {}
    for declaration in finite_rows(core[2]):
        if not headed(declaration, "TypeDecl", 2) or not isinstance(declaration[1], StringLiteral):
            raise ValueError("Malformed type authority")
        name = declaration[1]
        if name in carriers:
            raise ValueError("Ambiguous type authority")
        carriers[name] = declaration[2]
    constructors = {}
    for rule in finite_rows(core[3]):
        if not headed(rule, "GrammarRule", 5) or not isinstance(rule[1], StringLiteral):
            raise ValueError("Malformed constructor authority")
        key = rule[1].text
        signature = (rule[2], tuple(finite_rows(rule[3])))
        if key in constructors:
            raise ValueError("Ambiguous constructor authority")
        constructors[key] = signature

    statements = []
    for occurrence in occurrences:
        if not headed(occurrence, "ModuleRuleOccurrenceV1", 2) or not headed(occurrence[1], "SyntaxOriginV1", 2):
            raise ValueError("Expected portable rule origins")
        rule = occurrence[2]
        if not headed(rule, "RewriteRule", 5):
            raise ValueError("Malformed rewrite rule")
        variables = {}
        for binding in finite_rows(rule[2]):
            if not headed(binding, "TypeBinding", 2) or not isinstance(binding[1], StringLiteral) or not headed(binding[2], "TBase", 1):
                raise ValueError("This adapter requires base-sorted variable bindings")
            if binding[1].text in variables:
                raise ValueError("Duplicate rule variable")
            variables[binding[1].text] = (Variable(f"module_v{len(variables)}"), binding[2][1])

        def pattern(value, expected=None):
            if headed(value, "FVar", 1) and isinstance(value[1], StringLiteral):
                if value[1].text not in variables:
                    raise ValueError("Unbound rule variable")
                variable, category = variables[value[1].text]
                if expected is not None and category != expected:
                    raise ValueError("Rule variable has the wrong category")
                return variable, category
            if not headed(value, "PApp", 2) or not isinstance(value[1], StringLiteral):
                raise ValueError("This adapter requires first-order constructor patterns")
            signature = constructors.get(value[1].text)
            if signature is None or (expected is not None and signature[0] != expected):
                raise ValueError("Unknown or wrongly sorted rule constructor")
            if carriers.get(signature[0]) != Symbol("CarrierAst"):
                raise ValueError("This adapter requires the declared constructor carrier")
            arguments, params = finite_rows(value[2]), signature[1]
            if len(arguments) != len(params):
                raise ValueError("Rule constructor arity mismatch")
            lowered = []
            for argument, param in zip(arguments, params):
                if not headed(param, "TermSimple", 2) or not headed(param[2], "TBase", 1):
                    raise ValueError("This adapter requires first-order base parameters")
                lowered.append(pattern(argument, param[2][1])[0])
            return (Symbol(value[1].text), *lowered), signature[0]

        left, category = pattern(rule[4])
        right, _ = pattern(rule[5], category)
        premises = []
        for index, premise in enumerate(finite_rows(rule[3])):
            if not headed(premise, "Congruence", 2):
                raise ValueError("A relation/freshness premise needs an explicit provider adapter")
            source, sort = pattern(premise[1])
            target, _ = pattern(premise[2], sort)
            premises.append((Symbol("ModuleRewriteStepV1"), namespace, sort,
                             Variable(f"module_e{index}"), source, target))
        # These are checks of the selected core, not guessed typing predicates.
        for value in (left, right):
            premises.append((Symbol("Compute"), Symbol("langdef:admit-term-data"),
                             (authority, category, value), Symbol("->"),
                             (Symbol("LangDef:TermAdmitted"),)))
        step = (Symbol("ModuleRewriteStepV1"), namespace, category, occurrence[1], left, right)
        # PeTTaChainer indexes evidence by the proof head. Use an injective
        # portable spelling of this occurrence, not one head for every rule.
        identity = render((namespace, occurrence[1], rule[1])).encode("utf-8").hex()
        proof = (Symbol("ModuleRule_" + identity),
                 (Symbol("ModuleRuleOriginV1"), occurrence[1], rule[1]))
        implication = (Symbol("Implication"), (Symbol("And"), *premises), step)
        statements.append((Symbol(":"), proof, implication, evidence))
    return tuple((Symbol("compileadd"), knowledge_base, statement) for statement in statements)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--authority", default="$authority")
    parser.add_argument("--namespace", required=True)
    parser.add_argument("--knowledge-base", required=True)
    parser.add_argument("--evidence", required=True)
    args = parser.parse_args()
    values = parse_sexprs(args.bundle.read_text(encoding="utf-8"))
    if len(values) != 1:
        raise ValueError("Expected exactly one rule bundle")
    def operand(source):
        parsed = parse_sexprs(source)
        if len(parsed) != 1:
            raise ValueError("Expected exactly one operand")
        return parsed[0]
    calls = export_rules(values[0], operand(args.authority), StringLiteral(args.namespace),
                         operand(args.knowledge_base), operand(args.evidence))
    for call in calls:
        print(render_metta(call))


if __name__ == "__main__":
    main()
