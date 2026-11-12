#!/usr/bin/env python3
"""Qualify actual retained authored/fused executions against source firings."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import random

import module_ordered_realizations_v1 as client
from module_ordered_realizations_v1 import (
    ROOT, NIK, NIL, GSLT, Package, Session, Symbol, text, form, headed, rows,
    llist, patt, parse_sexprs, render, read_module, native, occurrence_bag)
from generate_ordered_module_fused_v1 import generate


def extend(bundle, added=(), constructors=()):
    occurrences = rows(bundle[2]) + list(added)
    core = bundle[1]
    core = (*core[:3], llist(rows(core[3]) + list(constructors)), core[4], llist(o[2] for o in occurrences))
    return form("ModuleRewriteRulesV1", core, llist(occurrences))


def rule(name, left, right, premises=(), names=("value",)):
    context = llist(form("TypeBinding", text(n), form("TBase", text("Nat"))) for n in names)
    return form("ModuleRuleOccurrenceV1", form("SyntaxOriginV1", text("native-rule-view"), text(name)),
                form("RewriteRule", text(name), context, llist(premises), left, right))


def source_occurrences(package, subject):
    """Direct root firings, independent of both machines and their compiler.

    Environments are maps. The observable field order is the declaration's
    type context; internal matcher association-list order remains in raw state.
    """
    def match(schema, value, bindings):
        if headed(schema, "FVar", 1):
            key = schema[1]
            if key in bindings and bindings[key] != value:
                return None
            return {**bindings, key: value}
        if not headed(schema, "PApp", 2) or not headed(value, "PApp", 2) or schema[1] != value[1]:
            return None
        ps, vs = rows(schema[2]), rows(value[2])
        if len(ps) != len(vs):
            return None
        current = dict(bindings)
        for p, v in zip(ps, vs):
            current = match(p, v, current)
            if current is None:
                return None
        return current
    def instantiate(term, bindings):
        if headed(term, "FVar", 1):
            return bindings.get(term[1], term)
        return form("PApp", term[1], llist(instantiate(t, bindings) for t in rows(term[2])))
    result = []
    provider_rows = rows(package.provider[2])
    for ordinal, occurrence in enumerate(package.occurrences):
        declaration = occurrence[2]
        if package.rule_sort(ordinal) != package.subject_sort(subject):
            continue
        initial = match(declaration[4], subject, {})
        if initial is None:
            continue
        seeds = [(initial, [])]
        for query in rows(declaration[3]):
            next_seeds = []
            for seed, choices in seeds:
                arguments = llist(instantiate(a, seed) for a in rows(query[2]))
                for index, row in enumerate(row for row in provider_rows if row[1] == query[1]):
                    if len(rows(row[2])) != len(rows(query[2])):
                        continue
                    extended = dict(seed)
                    for schema, value in zip(rows(query[2]), rows(row[2])):
                        extended = match(schema, value, extended)
                        if extended is None:
                            break
                    if extended is not None:
                        choice = form("QueryRowChoiceV1", query[1], arguments, Symbol("ProviderRow"), index, row[2])
                        next_seeds.append((extended, choices + [choice]))
            seeds = next_seeds
        def observation(bindings):
            return llist(form("ObservedBindingV1", entry[1], form("Some", bindings[entry[1]]) if entry[1] in bindings else Symbol("None"))
                         for entry in rows(declaration[2]))
        for final, choices in seeds:
            result.append(form("OrderedModuleOccurrenceV1", ordinal, occurrence[1], observation(initial),
                               observation(final), llist(choices), instantiate(declaration[5], final)))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta")
    parser.add_argument("--reader-cetta", type=Path, default=NIK / "runtime/cetta-module-composed-qualified")
    parser.add_argument("--reader-artifact", type=Path, default=NIK / "runtime/generated/parser-artifacts-core.nogmp.json-gslt/433dd5b991f597e56f89414f238079d7f397a5105a0791dc1a1b9c06b3240937/module-core-reader.tpp")
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/ordered-module-qualified")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (ROOT / "lib/ordered_module_fused_v1.metta").read_text() != generate():
        raise AssertionError("fused generated library is stale")
    bundle = read_module(args.reader_cetta, args.reader_artifact,
                         ROOT / "tests/ordered_module/OrderedRealizations.module", "Ordered", output)
    no_provider = form("PremiseProvider", NIL, NIL)
    base = Package.admit(bundle, no_provider)
    count, measurements = 0, []
    def check(name, condition):
        nonlocal count
        if not condition:
            raise AssertionError(name)
        count += 1
        print("PASS", name)
    def refused(name, function):
        try:
            function()
        except ValueError:
            check(name, True)
            return
        raise AssertionError(name + " was accepted")
    def complete(name, package, subject, work=4096):
        expected = source_occurrences(package, subject)
        sessions, observations = [], []
        for route in ("authored", "fused"):
            s = Session(package, args.cetta, output / name, route, subject)
            observed = s.advance(work, 100000)
            check(name + ":" + route + ":source-observer", occurrence_bag(observed["available"]) == occurrence_bag(expected))
            check(name + ":" + route + ":declaration-policy", observed["available"] == expected)
            check(name + ":" + route + ":complete", not observed["pending"])
            sessions.append(s)
            observations.append(observed)
            measurements.append({"case": name, "route": route, **{k: observed[k] for k in ("primitive_work", "matched_rows", "state_bytes")}})
        check(name + ":two-realizations", occurrence_bag(observations[0]["available"]) == occurrence_bag(observations[1]["available"]))
        return sessions, observations
    z = patt("NatZ")
    succ = lambda term: patt("NatS", term)
    add = lambda left, right: patt("NatAdd", left, right)
    var = lambda name: form("FVar", text(name))
    sessions, observations = complete("overlap-duplicate-rules", base, add(z, z))
    expected = source_occurrences(base, add(z, z))
    reversed_worker = Session(base, args.cetta, output / "reverse-complete-worker", "fused", add(z, z),
                              enumeration="reverse-complete")
    withheld = reversed_worker.advance(0, 1)
    check("reverse-worker-retains-unfinished-work", withheld["pending"] and not withheld["available"])
    reversed_first = reversed_worker.advance(4096, 1)
    check("reverse-worker-different-enumeration", reversed_first["available"] == list(reversed(expected)) and
          reversed_first["available"] != expected)
    check("reverse-worker-same-semantic-occurrences", occurrence_bag(reversed_first["available"]) == occurrence_bag(expected))
    check("once-can-select-different-valid-occurrence", reversed_first["emitted"] == expected[-1:] and
          reversed_first["emitted"] != expected[:1])
    reversed_worker.enumeration = "declaration"
    refused("retained-worker-refuses-enumeration-policy-change", lambda: reversed_worker.advance(0, 0))
    reversed_worker.enumeration = "reverse-complete"
    reversed_rest = reversed_worker.advance(0, 100)
    check("reverse-worker-resumes-own-publication", reversed_first["emitted"] + reversed_rest["emitted"] == list(reversed(expected)))
    check("reverse-worker-no-replay", not reversed_worker.advance(0, 100)["emitted"])
    equal = [o for o in observations[0]["available"] if o[6] == z]
    check("duplicate-target-distinct-origins", len(equal) == 2 and equal[0][2] != equal[1][2])
    complete("nonlinear-disagreement", base, add(z, succ(z)))
    ternary_sessions, _ = complete("ternary-bindings", base, patt("NatTri", z, succ(z), z))
    authored_receipt = rows(ternary_sessions[0].publication[1])[0]
    fused_receipt = rows(ternary_sessions[1].publication[1])[0]
    check("raw-binding-layouts-remain-distinct", [b[1] for b in rows(authored_receipt[3])] != [b[1] for b in rows(fused_receipt[2])])
    check("fused-common-column-drop-executes", '"mm-drop"' in render(base.fused_program(text("Nat"))))
    complete("empty-unmatched", base, z)
    wildcard_bundle = read_module(args.reader_cetta, args.reader_artifact,
                                 ROOT / "tests/ordered_module/OrderedRealizations.module", "Wildcards", output / "wildcard-frontend")
    complete("wildcard-root-only", Package.admit(wildcard_bundle, no_provider), succ(z))
    complete("mixed-root-wildcards", Package.admit(extend(bundle, rows(wildcard_bundle[2])), no_provider), add(z, z))
    # A wildcard for Nat must not capture a Bool merely because its untyped
    # structural pattern accepts every term. Include a later Bool rule to
    # check that sort selection keeps the original declaration ordinal.
    bool_type = form("TypeDecl", text("Bool"), Symbol("CarrierAst"))
    bool_constructor = form("GrammarRule", text("BoolF"), text("Bool"), NIL, NIL, Symbol("EvalNone"))
    nat_wildcard = rule("NatWildcard", var("value"), z)
    bool_rule = form("ModuleRuleOccurrenceV1", form("SyntaxOriginV1", text("native-rule-view"), text("BoolIdentity")),
                     form("RewriteRule", text("BoolIdentity"), NIL, NIL, patt("BoolF"), patt("BoolF")))
    def with_bool(occurrences):
        core = bundle[1]
        changed = (*core[:2], llist(rows(core[2]) + [bool_type]),
                   llist(rows(core[3]) + [bool_constructor]), core[4], llist(o[2] for o in occurrences))
        return form("ModuleRewriteRulesV1", changed, llist(occurrences))
    sort_guarded = Package.admit(with_bool([nat_wildcard]), no_provider, [0])
    _, different_sort = complete("wildcard-refuses-another-sort", sort_guarded, patt("BoolF"))
    check("wrong-sort-wildcard-has-no-success", not different_sort[0]["available"] and not different_sort[1]["available"])
    complete("wildcard-accepts-own-sort", sort_guarded, succ(z))
    mixed_sort = Package.admit(with_bool([nat_wildcard, bool_rule]), no_provider, [0, 1])
    _, mixed_sort_result = complete("typed-dispatch-retains-original-origin", mixed_sort, patt("BoolF"))
    check("typed-dispatch-retains-original-rule-index", all(o[1] == 1 for r in mixed_sort_result for o in r["available"]))
    submitted_occurrences = rows(bundle[2])
    reversed_package = Package.admit(bundle, no_provider, [submitted_occurrences.index(o) for o in reversed(base.occurrences)])
    _, reverse = complete("reversed-declared-rule-order", reversed_package, add(z, z))
    check("reversed-rule-readout", [o[2:] for o in reverse[0]["available"]] == [o[2:] for o in reversed(observations[0]["available"])])
    rng = random.Random(20261014)
    for depth in (1, 7, 31, 95):
        term = z
        for _ in range(depth):
            term = succ(term) if rng.randrange(3) else add(z, term)
        complete("generated-depth-" + str(depth), base, add(z, term), max(4096, depth * 96))
    provider = parse_sexprs((ROOT / "tests/ordered_module/provider_snapshot.metta").read_text(), source="provider")[0]
    echo = parse_sexprs((ROOT / "tests/ordered_module/provider_rule_occurrences.metta").read_text(), source="rule view")
    supplied = Package.admit(extend(bundle, echo), provider)
    _, supplied_observation = complete("duplicate-provider-rows", supplied, add(z, z))
    equal_rows = [o for o in supplied_observation[0]["available"] if rows(o[5])]
    check("equal-provider-row-receipts", len(equal_rows) == 2 and rows(equal_rows[0][5])[0][4] != rows(equal_rows[1][5])[0][4])
    unrelated_signature = form("PremiseSignature", text("unrelated"), llist([form("TBase", text("Nat"))]), llist([Symbol("RelationInput")]))
    mixed_provider = form("PremiseProvider", llist([unrelated_signature] + rows(provider[1])),
                          llist([form("PremiseProviderRow", text("unrelated"), llist([z]))] + rows(provider[2])))
    complete("provider-row-origin-projection", Package.admit(extend(bundle, echo), mixed_provider), add(z, z))
    reversed_provider = form("PremiseProvider", provider[1], llist(reversed(rows(provider[2]))))
    complete("reversed-provider-row-order", Package.admit(extend(bundle, echo), reversed_provider), add(z, z))
    output_rule = rule("provider-output", z, var("value"), [form("RelationQuery", text("allowed"), llist([var("value")]))])
    output_signatures = llist([form("PremiseSignature", text("allowed"), llist([form("TBase", text("Nat"))]), llist([Symbol("RelationOutput")]))])
    output_provider = form("PremiseProvider", output_signatures, provider[2])
    output_package = Package.admit(extend(bundle, [output_rule]), output_provider)
    _, out_forward = complete("ordered-provider-output", output_package, z)
    reverse_output_provider = form("PremiseProvider", output_signatures, llist(reversed(rows(provider[2]))))
    _, out_reverse = complete("reversed-provider-output", Package.admit(extend(bundle, [output_rule]), reverse_output_provider), z)
    check("provider-target-order-reverses", [o[6] for o in out_reverse[0]["available"]] == list(reversed([o[6] for o in out_forward[0]["available"]])))
    twice = rule("two-provider-premises", z, var("value"), [form("RelationQuery", text("allowed"), llist([var("value")]))] * 2)
    complete("ordered-two-premise-provider-histories", Package.admit(extend(bundle, [twice]), output_provider), z)
    mixed_mode_query = form("RelationQuery", text("mixed-mode"), llist([var("value"), var("value")]))
    mixed_mode_provider = form("PremiseProvider",
        llist(rows(output_provider[1]) + [form("PremiseSignature", text("mixed-mode"),
            llist([form("TBase", text("Nat"))] * 2),
            llist([Symbol("RelationOutput"), Symbol("RelationInput")]))]),
        llist(rows(output_provider[2]) + [form("PremiseProviderRow", text("mixed-mode"), llist([z, z]))]))
    output_as_input = rule("same-query-output-is-not-prior-input", z, var("value"), [mixed_mode_query])
    refused("same-query-output-cannot-license-input",
            lambda: Package.admit(extend(bundle, [output_as_input]), mixed_mode_provider))
    prior_output = rule("earlier-query-output-licenses-input", z, var("value"),
        [form("RelationQuery", text("allowed"), llist([var("value")])), mixed_mode_query])
    complete("earlier-query-output-licenses-input",
             Package.admit(extend(bundle, [prior_output]), mixed_mode_provider), z)
    for mode in ("RelationInput", "RelationOutput"):
        query = form("RelationQuery", text("structured"), llist([succ(var("value"))]))
        left = succ(var("value")) if mode == "RelationInput" else z
        declaration = rule(mode, left, var("value"), [query])
        table = form("PremiseProvider", llist([form("PremiseSignature", text("structured"), llist([form("TBase", text("Nat"))]), llist([Symbol(mode)]))]),
                     llist([form("PremiseProviderRow", text("structured"), llist([succ(z)])), form("PremiseProviderRow", text("structured"), llist([succ(z)])), form("PremiseProviderRow", text("structured"), llist([succ(succ(z))]))]))
        p = Package.admit(extend(bundle, [declaration]), table)
        complete("constructed-query-" + mode, p, succ(z) if mode == "RelationInput" else z)
        if mode == "RelationInput":
            conflict_table = form("PremiseProvider", table[1], llist([rows(table[2])[-1]]))
            complete("constructed-query-binding-conflict", Package.admit(extend(bundle, [declaration]), conflict_table), succ(z))
    for route in ("authored", "fused"):
        s = Session(supplied, args.cetta, output / "retained-slicing", route, add(z, z))
        beginning = s.advance(0, 0)
        check(route + ":zero-work-zero-delivery-pending", beginning["pending"] and not beginning["emitted"])
        captured = []
        reached = False
        for _ in range(256):
            part = s.advance(1, 1)
            captured.extend(part["emitted"])
            if captured:
                reached = part["pending"]
                break
        check(route + ":pause-after-first-answer", len(captured) == 1 and reached)
        checkpoint = s.state
        check(route + ":select-zero-preserves-delivery", not s.advance(0, 0)["emitted"])
        check(route + ":zero-work-retains-state", s.state == checkpoint)
        for _ in range(256):
            part = s.advance(3, 1)
            captured.extend(part["emitted"])
            if not part["pending"]:
                break
        check(route + ":repeated-slicing-equals-uninterrupted", captured == source_occurrences(supplied, add(z, z)))
        checkpoint = s.state
        check(route + ":completed-does-not-restart", not s.advance(100, 10)["emitted"] and s.state == checkpoint)
        other = Session(supplied, args.cetta, output / "foreign", route, add(z, z))
        refused(route + ":foreign-cursor-even-zero", lambda: s.advance(0, 0, cursor=other.cursor))
        stale = Package.admit(extend(bundle, echo), reversed_provider)
        refused(route + ":changed-provider-snapshot", lambda: s.advance(0, 0, package=stale))
        reset = form("OrderedDeliveryCursorV1", s.lineage, 0, NIL)
        refused(route + ":delivery-restart-mutant", lambda: s.advance(0, 1, cursor=reset))
        malformed = form("OrderedDeliveryCursorV1", s.lineage, s.cursor[2] + 1, s.cursor[3])
        refused(route + ":malformed-consumed-count", lambda: s.advance(0, 1, cursor=malformed))
        # Change private input copies, never libraries or binaries used by
        # other sessions. Even a zero-work call must validate its runtime.
        changed_binary = output / (route + "-runtime-revision-control")
        changed_binary.write_bytes(args.cetta.read_bytes())
        changed_binary.chmod(args.cetta.stat().st_mode & 0o777)
        binary_session = Session(base, changed_binary, output / "runtime-revision", route, z)
        with changed_binary.open("ab") as changed:
            changed.write(b"\0")
        refused(route + ":changed-runtime-even-zero", lambda: binary_session.advance(0, 0))
        original_root = client.ROOT
        private_root = output / (route + "-library-revision-control")
        (private_root / "lib").mkdir(parents=True, exist_ok=True)
        try:
            client.ROOT = private_root
            for changed_name in ("ordered_module_fused_v1.metta", "ordered_module_delivery_v1.metta"):
                for name in ("ordered_module_fused_v1.metta", "ordered_module_delivery_v1.metta"):
                    (private_root / "lib" / name).write_bytes((original_root / "lib" / name).read_bytes())
                library_session = Session(base, args.cetta, output / "library-revision", route, z)
                with (private_root / "lib" / changed_name).open("a") as changed:
                    changed.write("\n; changed revision\n")
                refused(route + ":changed-library-even-zero:" + changed_name,
                        lambda: library_session.advance(0, 0))
        finally:
            client.ROOT = original_root
    # Finite licensed source prefixes of a self-loop retain another actual
    # root job, rather than conflating equal-valued occurrences or termination.
    for route in ("authored", "fused"):
        subject, prefix = succ(z), []
        for step in range(4):
            retained = Session(base, args.cetta, output / ("loop-" + str(step)), route, subject)
            part = retained.advance(512, 1)
            check(route + ":self-loop-step-" + str(step), len(part["emitted"]) == 1 and part["emitted"][0][6] == subject)
            prefix.extend(part["emitted"])
            subject = part["emitted"][0][6]
        pending = Session(base, args.cetta, output / "loop-residual", route, subject)
        check(route + ":self-loop-residual-retained", pending.advance(0, 0)["pending"] and len(prefix) == 4)
    constructor = form("GrammarRule", text("println!"), text("Nat"), llist([form("TermSimple", text("value"), form("TBase", text("Nat")))]), NIL, Symbol("EvalNone"))
    inert_rule = rule("callable-data", patt("println!", var("value")), var("value"))
    complete("callable-looking-constructor-data", Package.admit(extend(bundle, [inert_rule], [constructor]), no_provider), patt("println!", z))
    complete("callable-looking-constructor-child", Package.admit(extend(bundle, [inert_rule], [constructor]), no_provider), add(z, patt("println!", z)))
    # Metadata must decode to the same formal String fields even when the
    # malformed name is unused by this particular matcher execution.
    metadata = rule("metadata", z, z, names=())
    for label, body in (
        ("rule-name", form("RewriteRule", 7, *metadata[2][2:])),
        ("binding-name", form("RewriteRule", text("metadata"),
            llist([form("TypeBinding", 7, form("TBase", text("Nat")))]), NIL, z, z)),
    ):
        malformed = form("ModuleRuleOccurrenceV1", metadata[1], body)
        refused("non-string-" + label,
                lambda malformed=malformed: Package.admit(extend(bundle, [malformed]), no_provider))
    malformed_parameter = form("GrammarRule", text("UnusedMetadata"), text("Nat"),
        llist([form("TermSimple", 7, form("TBase", text("Nat")))]), NIL, Symbol("EvalNone"))
    refused("non-string-constructor-parameter-name",
            lambda: Package.admit(extend(bundle, constructors=[malformed_parameter]), no_provider))
    malformed_core_name = (bundle[1][0], 7, *bundle[1][2:])
    refused("non-string-language-name",
            lambda: Package.admit(form("ModuleRewriteRulesV1", malformed_core_name, bundle[2]), no_provider))
    # Refused syntax is retained verbatim alongside its reason.
    bad_patterns = [form("PLam", text("x"), var("value")), form("PCollection", text("vec"), NIL, Symbol("RNone"))]
    for i, pattern in enumerate(bad_patterns):
        rejected = extend(bundle, [rule("unsupported-" + str(i), pattern, var("value"))])
        (output / ("refused-syntax-" + str(i) + ".metta")).write_text(render(rejected) + "\n")
        refused("unsupported-profile-" + str(i), lambda: Package.admit(rejected, no_provider))
    equation_core = (*bundle[1][:4], llist([form("Equation", text("conversion"), NIL, NIL, z, succ(z))]), bundle[1][5])
    refused("equation-profile", lambda: Package.admit(form("ModuleRewriteRulesV1", equation_core, bundle[2]), no_provider))
    refused("malformed-order-map", lambda: Package.admit(bundle, no_provider, [0]))
    wrong_occurrence = form("ModuleRuleOccurrenceV1", rows(bundle[2])[0][1], rows(bundle[1][5])[-1])
    refused("malformed-origin-metadata", lambda: Package.admit(form("ModuleRewriteRulesV1", bundle[1], llist([wrong_occurrence] + rows(bundle[2])[1:])), no_provider))
    wrong_row = form("PremiseProvider", provider[1], llist([form("PremiseProviderRow", text("allowed"), llist([patt("foreign")]))]))
    refused("provider-row-outside-signature", lambda: Package.admit(bundle, wrong_row))
    eq_rule = rule("builtin-eq", z, var("value"), [form("RelationQuery", text("eq"), llist([var("value"), var("value")]))])
    eq_signature = form("PremiseSignature", text("eq"), llist([form("TBase", text("Nat")), form("TBase", text("Nat"))]), llist([Symbol("RelationOutput"), Symbol("RelationOutput")]))
    eq_provider = form("PremiseProvider", llist([eq_signature]), NIL)
    refused("builtin-eq-profile", lambda: Package.admit(extend(bundle, [eq_rule]), eq_provider))
    missing_input = rule("unbound-input", z, var("value"), [form("RelationQuery", text("allowed"), llist([var("value")]))])
    refused("unbound-provider-input", lambda: Package.admit(extend(bundle, [missing_input]), provider))
    refused("unbound-target", lambda: Package.admit(extend(bundle, [rule("unbound-target", z, var("value"))]), no_provider))
    artifact = base.compiled_artifact()
    sort_programs = rows(base.validate_artifact(artifact)[1])
    check("compiled-program-bound-to-presentation",
          next(row[2] for row in sort_programs if row[1] == text("Nat")) == base.fused_program(text("Nat")))
    refused("malformed-compiled-metadata", lambda: base.validate_artifact((*artifact[:2], text("foreign"), *artifact[3:])))
    refused("changed-compiled-instruction", lambda: base.validate_artifact((*artifact[:-1], patt("mm-failure"))))
    forged_package = Package(base.submitted, base.core, base.occurrences, provider, base.identity, base.snapshot)
    refused("forged-package-snapshot-metadata", lambda: Session(forged_package, args.cetta, output / "forged-package", "fused", z))
    malformed_bindings = llist([form("Binding", text("value"), z), form("Binding", text("value"), z)])
    bound_ordinal = next(i for i, o in enumerate(base.occurrences) if o[2][1] == text("Nonlinear"))
    refused("binding-observer-refuses-duplicates", lambda: base.observe_bindings(bound_ordinal, malformed_bindings))
    refused("binding-observer-refuses-foreign-name", lambda: base.observe_bindings(bound_ordinal, llist([form("Binding", text("foreign"), z)])))
    # Mutate actual native publication after target execution. The differential
    # semantic observer rejects loss and invented successes; a permutation is
    # accepted while the separately advertised declaration policy detects it.
    original_native = client.native
    for mutation in ("deduplicate", "reorder", "extra-success"):
        def mutated_native(binary, source, directory, name, **kwargs):
            replacement = {
                "deduplicate": '(let (ProgramPublication $entries $status) $publication (case $entries (((LCons $first $rest) (ProgramPublication (LCons $first LNil) $status)))))',
                "reorder": '(let (ProgramPublication $entries $status) $publication (let $reverse (ordered:fused:reverse $entries LNil) (ProgramPublication $reverse $status)))',
                "extra-success": '(let (ProgramPublication $entries $status) $publication (case $entries (((LCons $first $rest) (let $extra (gslt:automata:append $entries (LCons $first LNil)) (ProgramPublication $extra $status))))))',
            }[mutation]
            source = source.replace('(ordered:fused:publication $state)',
                '(let $originalPublication (ordered:fused:publication $state) (let $mutated ' +
                replacement.replace('$publication', '$originalPublication') + ' $mutated))')
            return original_native(binary, source, directory, name, **kwargs)
        client.native = mutated_native
        try:
            s = Session(base, args.cetta, output / ("mutant-" + mutation), "fused", add(z, z))
            actual = s.advance(512, 100)
            expected = source_occurrences(base, add(z, z))
            if mutation == "reorder":
                check("native-permutation-preserves-semantic-observer", occurrence_bag(actual["available"]) == occurrence_bag(expected))
                check("detected-advertised-policy-mutation", actual["available"] != expected)
            else:
                check("detected-native-target-" + mutation, occurrence_bag(actual["available"]) != occurrence_bag(expected))
        finally:
            client.native = original_native
    replay = Session(base, args.cetta, output / "mutant-restart", "fused", add(z, z))
    first = replay.advance(512, 1)
    def restart_native(binary, source, directory, name, **kwargs):
        source = source.replace('(ordered:delivery:drop $next $entries)', '(ordered:delivery:drop 0 $entries)')
        return original_native(binary, source, directory, name, **kwargs)
    client.native = restart_native
    try:
        second = replay.advance(0, 1)
        check("detected-native-target-restarts-delivery", first["emitted"] + second["emitted"] != source_occurrences(base, add(z, z))[:2])
    finally:
        client.native = original_native
    summary = {"controls": count, "failures": 0, "cross_machine_observer": "full occurrence multiset; multiplicity, bindings and provenance",
               "enumeration_policies": ["declaration", "reverse-complete"],
               "frontend_boundary": "existing checked NIK reader; executable qualification only",
               "native_boundary": "shared draft PeTTa evaluator; implementation C correctness is not formally proved here",
               "runtime_sha256": hashlib.sha256(args.cetta.read_bytes()).hexdigest(),
               "reader_sha256": hashlib.sha256(args.reader_cetta.read_bytes()).hexdigest(),
               "reader_artifact_sha256": hashlib.sha256(args.reader_artifact.read_bytes()).hexdigest(),
               "authored_source_sha256": client.AUTHORED_HASHES,
               "fused_source_sha256": hashlib.sha256((ROOT / "lib/ordered_module_fused_v1.metta").read_bytes()).hexdigest(),
               "delivery_source_sha256": hashlib.sha256((ROOT / "lib/ordered_module_delivery_v1.metta").read_bytes()).hexdigest(),
               "measurements": measurements}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"(OrderedModuleRealizationsV1Summary {count} 0)")


if __name__ == "__main__":
    main()
