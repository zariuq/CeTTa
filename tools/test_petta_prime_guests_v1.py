"""Replay retained guest controls through the explicit PeTTa-to-Prime adapter."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

from hol_light_petta_v1 import (LIBRARY, call, export_events, library_text,
                               source_term, tagged)
from hol_light_proof_export_v1 import inspect_closure
from hol_light_prime_v1 import receive_program
from library_spelling import Symbol, read_forms, show
from petta_prime_guest_v1 import compile_source, form, lower


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    repository = Path(__file__).resolve().parents[1]
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--eval-hashcons", action="store_true")
    parser.add_argument("--package", action="append", choices=(
        "guest-dialect-controls", "guest-wire-controls", "guest-sequence-controls",
        "hol-kernel-controls", "hol-beta-controls", "hol-type-instantiation-controls",
        "hol-term-instantiation-controls", "hol-publication-controls", "hol-cache-authorization-controls", "hol-mixed-history-controls",
        "hol-axiom-execution-controls", "hol-axiom-event-controls", "hol-declaration-history-controls", "hol-definition-history-controls", "hol-parameter-order-controls", "hol-type-definition-publication-controls", "guest-bound-call-controls",
        "hol-library-imports", "hol-event-resumption", "hol-event-resumption-imports",
        "hol-source-primitives", "hol-source-type-definition",
        "hol-sticky-refusal", "mm0-core-service", "mm0-private-space-controls",
        "mm0-textual-service", "mm0-binary-service", "mm0-mmu-service"))
    parser.add_argument("--mm0-root", type=Path,
                        default=repository.parent / "cetta-mm0-metta-20261004")
    args = parser.parse_args()
    binary, work = args.binary.resolve(), args.workdir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    packages = []

    def execute(label: str, source: str, expected: list[str],
                prepared: tuple[str, dict] | None = None,
                administrative: int = 0, cwd: Path | None = None,
                source_expected: list[str] | None = None) -> None:
        if args.package and label not in args.package:
            return
        original = work / (label + ".petta.metta")
        intermediate = work / (label + ".views.metta")
        port = work / (label + ".prime.metta")
        original.write_text(source)
        source_evidence = None
        if source_expected is not None:
            source_result = subprocess.run([str(binary), "--lang", "petta", str(original)],
                cwd=cwd or repository, text=True, capture_output=True, timeout=args.timeout)
            (work / (label + ".source.stdout")).write_text(source_result.stdout)
            (work / (label + ".source.stderr")).write_text(source_result.stderr)
            if (source_result.returncode or source_result.stderr or
                    source_result.stdout.splitlines() != source_expected):
                raise AssertionError(f"{label}: source contract replay differs; inspect retained output")
            source_evidence = {"dialect": "cetta-petta", "observations": len(source_expected),
                               "stdout_sha256": digest(work / (label + ".source.stdout"))}
        translated, inventory = prepared or compile_source(source, binary, intermediate,
            sort_values="closed-strings" if label.startswith("hol-") else None)
        port.write_text(translated)
        started = time.monotonic()
        try:
            command = [str(binary), "--lang", "prime"]
            if args.eval_hashcons:
                command.append("--eval-hashcons")
            result = subprocess.run([*command, str(port)],
                                    cwd=cwd or repository, text=True, capture_output=True,
                                    timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            for suffix, output in (("stdout", error.stdout), ("stderr", error.stderr)):
                if isinstance(output, bytes):
                    output = output.decode(errors="replace")
                (work / (label + "." + suffix)).write_text(output or "")
            raise AssertionError(f"{label}: exceeded the execution budget; "
                                 "partial output retained") from error
        (work / (label + ".stdout")).write_text(result.stdout)
        (work / (label + ".stderr")).write_text(result.stderr)
        actual = result.stdout.splitlines()
        if result.returncode != 0 or result.stderr or actual != expected:
            mismatches = [(i + 1, got, want) for i, (got, want)
                          in enumerate(zip(actual, expected)) if got != want]
            raise AssertionError(f"{label}: exit={result.returncode}, "
                                 f"answers={len(actual)}/{len(expected)}, "
                                 f"mismatches={mismatches[:5]}; inspect retained output")
        packages.append({"package": label, "observations": len(expected) - administrative,
                         "administrative_answers": administrative,
                         **inventory, "source_sha256": digest(original),
                         "prime_sha256": digest(port),
                         "source_execution": source_evidence,
                         "seconds": round(time.monotonic() - started, 3)})
        print(f"{label}: {len(expected) - administrative} observations pass", flush=True)

    dialect_fixture = repository / "tests" / "support" / "fixtures" / \
        "petta_prime_guest_closed.metta"
    dialect_labels = ("empty", "conditional", "committed", "inert-quotation", "raw-formal",
                      "captured-value", "passive-fields", "compound-head", "sequence-view",
                      "empty-view")
    execute("guest-dialect-controls", dialect_fixture.read_text(),
            [f"[(GuestControl {label} True)]" for label in dialect_labels])

    bound_fixture = repository / "tests" / "support" / "fixtures" / "petta_bound_calls.metta"
    bound_labels = ("captured-symbol", "nested-binding", "strict-argument", "alternatives",
                    "repeated-occurrences", "unknown-head", "unknown-strict-fields", "quotation")
    execute("guest-bound-call-controls", bound_fixture.read_text(),
            [f"[(BoundCallControl {label} True)]" for label in bound_labels],
            source_expected=[f"(BoundCallControl {label} true)" for label in bound_labels])

    if not args.package or "guest-wire-controls" in args.package:
        wire_fixture = repository / "tests" / "support" / "fixtures" / "petta_prime_guest_wire.metta"
        wire_source = wire_fixture.read_text()
        wire_prepared = compile_source(wire_source, binary, work / "guest-wire-controls.views.metta",
                                       cons_values="spine-or-flat")
        wire_labels = ("flat", "spine", "improper", "nested", "once")
        execute("guest-wire-controls", wire_source,
                [f"[(GuestWire {label} True)]" for label in wire_labels], wire_prepared,
                source_expected=[f"(GuestWire {label} true)" for label in wire_labels])

    fixtures = repository / "tests" / "hol_light"
    sequence_source = (repository / "tests" / "support" / "fixtures" /
                       "petta_prime_guest_sequences.metta").read_text()
    sequence_values = ("member True", "missing False", "duplicate (True True)",
                       "empty False", "reverse (3 2 1)", "reverse-empty ()")
    execute("guest-sequence-controls", sequence_source,
            [f"[(GuestSequence {value})]" for value in sequence_values],
            source_expected=[f"(GuestSequence {value.replace('True', 'true').replace('False', 'false')})"
                             for value in sequence_values])

    resumption_source = (repository / "lib" / "hol_light" / "resumption.metta").read_text()
    resumption_fixture = (fixtures / "resumption.metta").read_text()
    execute("hol-event-resumption", library_text() + "\n" + resumption_source +
            "\n" + resumption_fixture,
            ["[(ProofResumption True True 1 2 True 11)]",
             "[(IncompletePreserved True False)]", "[None]", "[(StickyWork None HOL:Nil 2)]"],
            source_expected=["(ProofResumption true true 1 2 true 11)",
                             "(IncompletePreserved true false)", "None", "(StickyWork None HOL:Nil 2)"])
    resumption_example = repository / "examples" / "prime" / "hol_light_resumption.metta"
    resumption_expected = resumption_example.with_suffix(".expected")
    def located_example(path: Path) -> str:
        forms = read_forms(path.read_text())
        for node in forms:
            if (isinstance(node, tuple) and isinstance(node[1], list)
                    and node[1][0] == "import!" and len(node[1]) == 3):
                node[1][2] = json.dumps(str((path.parent / str(node[1][2]).strip('"')).resolve()))
        return "\n".join(map(show, forms)) + "\n"
    execute("hol-event-resumption-imports", library_text() + "\n" + resumption_source +
            "\n" + resumption_fixture, resumption_expected.read_text().splitlines(),
            (located_example(resumption_example), {"target_module_sha256": {
                name: digest(repository / "lib" / "prime" / "hol_light" / (name + ".metta"))
                for name in (*LIBRARY, "resumption")}}), 8)
    forms = read_forms((fixtures / "kernel.metta").read_text())
    controls = [node[1] for node in forms if isinstance(node, tuple)
                and isinstance(node[1], list) and node[1][0] == "test"]
    if len(controls) != 58 or any(len(node) != 3 for node in controls):
        raise AssertionError("retained HOL kernel control inventory changed")
    queries = [(Symbol("!"), form("PrimeGuestControl", Symbol(str(i)),
                                 form("==", node[1], node[2])))
               for i, node in enumerate(controls)]
    execute("hol-kernel-controls", library_text() + "\n" +
            "\n".join(map(show, queries)) + "\n",
            [f"[(PrimeGuestControl {i} True)]" for i in range(len(controls))])

    beta_forms = read_forms((fixtures / "beta-bindings.metta").read_text())
    beta_controls = [node for node in beta_forms if isinstance(node, tuple)
                     and isinstance(node[1], list) and node[1][0] == "BetaControl"]
    beta_definitions = [node for node in beta_forms if isinstance(node, list)
                        and node and node[0] == "="]
    beta_labels = ("polymorphic", "nested-outer", "inner-shadow", "two-binders",
                   "same-name-free-type", "capture-refused", "wrong-name",
                   "wrong-typed-identity", "nonvariable-argument", "closed-opening")
    if tuple(str(node[1][1]) for node in beta_controls) != beta_labels:
        raise AssertionError("source BETA binding control inventory changed")
    execute("hol-beta-controls", library_text() + "\n" +
            "\n".join(map(show, beta_definitions + beta_controls)) + "\n",
            [f"[(BetaControl {label} True)]" for label in beta_labels],
            source_expected=[f"(BetaControl {label} true)" for label in beta_labels])

    type_inst_forms = read_forms((fixtures / "type-instantiation.metta").read_text())
    type_inst_controls = [node for node in type_inst_forms if isinstance(node, tuple)
                          and isinstance(node[1], list) and node[1][0] == "TypeInstantiationControl"]
    type_inst_definitions = [node for node in type_inst_forms if isinstance(node, list)
                             and node and node[0] == "="]
    type_inst_labels = ("polymorphic-identity", "capture-avoided", "captured-claim-refused",
                        "first-maplet", "identity-shadows", "nonvariable-key-ignored",
                        "empty-substitution", "hypothesis-merge", "axiom-dependencies",
                        "shadowed-invalid-maplet", "ignored-invalid-key", "unresolved-key", "missing-premise")
    if tuple(str(node[1][1]) for node in type_inst_controls) != type_inst_labels:
        raise AssertionError("source type-instantiation control inventory changed")
    execute("hol-type-instantiation-controls", library_text() + "\n" +
            "\n".join(map(show, type_inst_definitions + type_inst_controls)) + "\n",
            [f"[(TypeInstantiationControl {label} True)]" for label in type_inst_labels],
            source_expected=[f"(TypeInstantiationControl {label} true)" for label in type_inst_labels])

    term_inst_forms = read_forms((fixtures / "term-instantiation.metta").read_text())
    term_inst_controls = [node for node in term_inst_forms if isinstance(node, tuple)
                          and isinstance(node[1], list) and node[1][0] == "TermInstantiationControl"]
    term_inst_definitions = [node for node in term_inst_forms if isinstance(node, list)
                             and node and node[0] == "="]
    term_inst_labels = ("simultaneous", "first-maplet", "identity-shadows", "capture-avoided",
                       "captured-claim-refused", "bound-unchanged", "typed-key", "empty-substitution",
                       "hypothesis-merge", "axiom-dependencies", "shadowed-invalid-maplet",
                       "unused-invalid-maplet", "nonvariable-key", "wrong-replacement-type", "missing-premise")
    if tuple(str(node[1][1]) for node in term_inst_controls) != term_inst_labels:
        raise AssertionError("source term-instantiation control inventory changed")
    execute("hol-term-instantiation-controls", library_text() + "\n" +
            "\n".join(map(show, term_inst_definitions + term_inst_controls)) + "\n",
            [f"[(TermInstantiationControl {label} True)]" for label in term_inst_labels],
            source_expected=[f"(TermInstantiationControl {label} true)" for label in term_inst_labels])

    publication_forms = read_forms((fixtures / "publication.metta").read_text())
    publication_controls = [node for node in publication_forms if isinstance(node, tuple)
                            and isinstance(node[1], list) and node[1][0] == "PublicationControl"]
    publication_definitions = [node for node in publication_forms if isinstance(node, list)
                               and node and node[0] == "="]
    publication_labels = ("fresh-refl", "command-origin", "ordered-repeated-premises", "receipt-only-occupied",
                          "cache-only-occupied", "old-cache-retained", "receipt-order", "context-retained",
                          "dependencies-retained", "refused-result", "malformed-result", "malformed-receipt",
                          "malformed-tail", "missing-premise", "duplicate-run-key", "structured-keys", "duplicate-before-dispatch")
    if tuple(str(node[1][1]) for node in publication_controls) != publication_labels:
        raise AssertionError("source publication control inventory changed")
    execute("hol-publication-controls", library_text() + "\n" +
            "\n".join(map(show, publication_definitions + publication_controls)) + "\n",
            [f"[(PublicationControl {label} True)]" for label in publication_labels],
            source_expected=[f"(PublicationControl {label} true)" for label in publication_labels])

    cache_forms = read_forms((fixtures / "cache-authorization.metta").read_text())
    cache_controls = [node for node in cache_forms if isinstance(node, tuple)
                      and isinstance(node[1], list) and node[1][0] == "CacheAuthorizationControl"]
    cache_definitions = [node for node in cache_forms if isinstance(node, list)
                         and node and node[0] == "="]
    cache_labels = ("earned-readback", "earned-conclusion", "forged-conclusion", "forged-hypothesis",
                    "shared-conclusion", "repeated-premise-receipt", "same-fact-distinct-origin",
                    "assumption-retained", "assumption-cannot-disappear", "missing-key",
                    "fresh-declaration-retains-cache")
    if tuple(str(node[1][1]) for node in cache_controls) != cache_labels:
        raise AssertionError("source cache authorization control inventory changed")
    execute("hol-cache-authorization-controls", library_text() + "\n" +
            "\n".join(map(show, cache_definitions + cache_controls)) + "\n",
            [f"[(CacheAuthorizationControl {label} True)]" for label in cache_labels],
            source_expected=[f"(CacheAuthorizationControl {label} true)" for label in cache_labels])

    mixed_forms = read_forms((fixtures / "mixed-history.metta").read_text())
    mixed_controls = [node for node in mixed_forms if isinstance(node, tuple)
                      and isinstance(node[1], list) and node[1][0] == "MixedHistoryControl"]
    mixed_definitions = [node for node in mixed_forms if isinstance(node, list)
                         and node and node[0] == "="]
    mixed_labels = ("earned-boolean", "retained-polymorphic-premise", "exact-ordered-receipts",
                    "complete-state-resumption", "occupied-key-refused", "missing-premise-refused",
                    "wrong-resumption-state-refused", "wrong-type-claim-refused")
    if tuple(str(node[1][1]) for node in mixed_controls) != mixed_labels:
        raise AssertionError("source mixed-history control inventory changed")
    execute("hol-mixed-history-controls", library_text() + "\n" +
            "\n".join(map(show, mixed_definitions + mixed_controls)) + "\n",
            [f"[(MixedHistoryControl {label} True)]" for label in mixed_labels],
            source_expected=[f"(MixedHistoryControl {label} true)" for label in mixed_labels])

    axiom_forms = read_forms((fixtures / "axiom-execution.metta").read_text())
    axiom_controls = [node for node in axiom_forms if isinstance(node, tuple)
                      and isinstance(node[1], list) and node[1][0] == "AxiomExecutionControl"]
    axiom_definitions = [node for node in axiom_forms if isinstance(node, list)
                         and node and node[0] == "="]
    axiom_labels = ("selected-formula", "wrong-formula", "non-boolean", "missing-label",
                    "malformed-selected", "first-commitment", "dependency-and-origin", "no-proof-premises")
    if tuple(str(node[1][1]) for node in axiom_controls) != axiom_labels:
        raise AssertionError("source axiom execution control inventory changed")
    execute("hol-axiom-execution-controls", library_text() + "\n" +
            "\n".join(map(show, axiom_definitions + axiom_controls)) + "\n",
            [f"[(AxiomExecutionControl {label} True)]" for label in axiom_labels],
            source_expected=[f"(AxiomExecutionControl {label} true)" for label in axiom_labels])

    axiom_event_forms = read_forms((fixtures / "axiom-events.metta").read_text())
    axiom_event_controls = [node for node in axiom_event_forms if isinstance(node, tuple)
                           and isinstance(node[1], list) and node[1][0] == "AxiomEventControl"]
    axiom_event_definitions = [node for node in axiom_event_forms if isinstance(node, list)
                              and node and node[0] == "="]
    axiom_event_labels = ("accepted", "formula-mismatch", "non-boolean", "undeclared", "occupied-cache",
                          "occupied-receipt", "duplicate-event", "payload-and-receipt", "first-commitment")
    if tuple(str(node[1][1]) for node in axiom_event_controls) != axiom_event_labels:
        raise AssertionError("source axiom event control inventory changed")
    execute("hol-axiom-event-controls", library_text() + "\n" +
            "\n".join(map(show, axiom_event_definitions + axiom_event_controls)) + "\n",
            [f"[(AxiomEventControl {label} True)]" for label in axiom_event_labels],
            source_expected=[f"(AxiomEventControl {label} true)" for label in axiom_event_labels])

    declaration_forms = read_forms((fixtures / "declaration-history.metta").read_text())
    declaration_controls = [node for node in declaration_forms if isinstance(node, tuple)
                            and isinstance(node[1], list) and node[1][0] == "DeclarationHistoryControl"]
    declaration_definitions = [node for node in declaration_forms if isinstance(node, list)
                              and node and node[0] == "="]
    declaration_labels = ("accepted", "old-evidence", "reuse", "dependency-and-origin", "receipt",
                          "new-constant", "duplicate-name", "wrong-arity", "duplicate-event", "declaration-is-not-axiom")
    if tuple(str(node[1][1]) for node in declaration_controls) != declaration_labels:
        raise AssertionError("source declaration history control inventory changed")
    execute("hol-declaration-history-controls", library_text() + "\n" +
            "\n".join(map(show, declaration_definitions + declaration_controls)) + "\n",
            [f"[(DeclarationHistoryControl {label} True)]" for label in declaration_labels],
            source_expected=[f"(DeclarationHistoryControl {label} true)" for label in declaration_labels])

    definition_forms = read_forms((fixtures / "definition-history.metta").read_text())
    definition_controls = [node for node in definition_forms if isinstance(node, tuple)
                           and isinstance(node[1], list) and node[1][0] == "DefinitionHistoryControl"]
    definition_equations = [node for node in definition_forms if isinstance(node, list)
                           and node and node[0] == "="]
    definition_labels = ("accepted", "old-evidence", "reuse", "exact-definition", "receipt", "polymorphic-instance",
                         "duplicate-name", "wrong-schema", "free-variable", "hidden-input-formed", "hidden-input-parameter",
                         "hidden-parameter", "duplicate-event",
                         "definition-is-not-axiom")
    if tuple(str(node[1][1]) for node in definition_controls) != definition_labels:
        raise AssertionError("source definition history control inventory changed")
    execute("hol-definition-history-controls", library_text() + "\n" +
            "\n".join(map(show, definition_equations + definition_controls)) + "\n",
            [f"[(DefinitionHistoryControl {label} True)]" for label in definition_labels],
            source_expected=[f"(DefinitionHistoryControl {label} true)" for label in definition_labels])

    type_publication_forms = read_forms((fixtures / "type-definition-publication.metta").read_text())
    type_publication_controls = [node for node in type_publication_forms if isinstance(node, tuple)
                                 and isinstance(node[1], list) and node[1][0] == "TypePublicationControl"]
    type_publication_equations = [node for node in type_publication_forms if isinstance(node, list)
                                  and node and node[0] == "="]
    type_publication_labels = ("accepted", "first-origin", "second-origin", "ordered-receipts",
                               "first-premise", "second-premise", "dependencies", "old-evidence",
                               "first-receipt-refused", "first-cache-refused",
                               "second-receipt-refused", "second-cache-refused",
                               "reserved-function-refused", "arrow-spelling-distinct")
    if tuple(str(node[1][1]) for node in type_publication_controls) != type_publication_labels:
        raise AssertionError("source type-definition publication control inventory changed")
    execute("hol-type-definition-publication-controls", library_text() + "\n" +
            "\n".join(map(show, type_publication_equations + type_publication_controls)) + "\n",
            [f"[(TypePublicationControl {label} True)]" for label in type_publication_labels],
            source_expected=[f"(TypePublicationControl {label} true)" for label in type_publication_labels])

    parameter_forms = read_forms((fixtures / "parameter-order.metta").read_text())
    parameter_controls = [node for node in parameter_forms if isinstance(node, tuple)
                          and isinstance(node[1], list) and node[1][0] == "ParameterOrderControl"]
    parameter_equations = [node for node in parameter_forms if isinstance(node, list)
                           and node and node[0] == "="]
    parameter_labels = ("empty", "roundtrip", "duplicates", "not-deduplication", "lexical", "unicode",
                        "input-type", "collector-distinct", "parameter-order", "wrong-order")
    if tuple(str(node[1][1]) for node in parameter_controls) != parameter_labels:
        raise AssertionError("source parameter order control inventory changed")
    if not args.package or "hol-parameter-order-controls" in args.package:
        try:
            lower('!(sort-atom ("A2" "A10"))')
        except ValueError as error:
            if "closed string-list contract" not in str(error):
                raise
        else:
            raise AssertionError("unqualified source sorting was silently translated")
        quoted, quotation_inventory = lower('!(quote (sort-atom ("A2" "A10")))')
        if quotation_inventory["lexical_string_sorts"] or 'sort-strings' in quoted:
            raise AssertionError("sorting translation entered passive quoted syntax")
        authored, authored_inventory = lower('(= (sort-atom $x) $x) !(sort-atom ("A2" "A10"))')
        if authored_inventory["lexical_string_sorts"] or 'sort-strings' in authored:
            raise AssertionError("sorting translation replaced an authored equation")
        print("Source sorting contract: 3 boundary controls pass", flush=True)
    execute("hol-parameter-order-controls", library_text() + "\n" +
            "\n".join(map(show, parameter_equations + parameter_controls)) + "\n",
            [f"[(ParameterOrderControl {label} True)]" for label in parameter_labels],
            source_expected=[f"(ParameterOrderControl {label} true)" for label in parameter_labels])

    if not args.package or "hol-library-imports" in args.package:
        full, imports_inventory = compile_source(library_text() + "\n" +
            "\n".join(map(show, queries)) + "\n", binary,
            work / "hol-library-imports.views.metta", sort_values="closed-strings")
        declarations = [repository / "lib" / "prime" / "hol_light" / (name + ".metta")
                        for name in LIBRARY]
        imported = "(scope:profile lexical-fresh per-call reference)\n" + \
            "\n".join(show((Symbol("!"), form("import!", Symbol("&self"),
                 json.dumps(str(path))))) for path in declarations) + "\n" + \
            "\n".join(line for line in full.splitlines() if line.startswith("!")) + "\n"
        execute("hol-library-imports", library_text() + "\n" +
                "\n".join(map(show, queries)) + "\n",
                ["[()]"] * len(LIBRARY) +
                [f"[(PrimeGuestControl {i} True)]" for i in range(len(controls))],
                (imported, {**imports_inventory, "target_module_sha256":
                            {name: digest(path) for name, path in
                             zip(LIBRARY, declarations, strict=True)}}), len(LIBRARY))

    standard = json.loads((fixtures / "source" / "standard-theory.json").read_text())
    nodes = [json.loads(line) for line in
             (fixtures / "source" / "primitives.nodes.jsonl").read_text().splitlines()]
    events, addresses = export_events(nodes, standard)
    if len(events) != 190 or len(addresses) != 190:
        raise AssertionError("retained primitive closure inventory changed")
    def receive_source(selected: list[str], body: str) -> str:
        return (library_text() + "\n" + call("=", call("test:events"),
                call("quote", tagged(selected))) + "\n!" +
                call("let", "$events", call("test:events"), body) + "\n")
    received = receive_program(events, binary, work / "hol-source-primitives.views.metta",
        form("PrimitiveClosure", form("hol-light:accepted", Symbol("$received")),
             form("hol-light:receipt-count", Symbol("$received"))))
    execute("hol-source-primitives", receive_source(events,
            call("hol-light:accepted", call("hol-light:standard-receive", "$events"))),
            ["[()]"] * (len(events) + 1) + ["[(PrimitiveClosure True 199)]"],
            received, len(events) + 1)

    arrow = [json.loads(line) for line in
             (fixtures / "source" / "arrow-type.nodes.jsonl").read_text().splitlines()]
    arrow_events, arrow_keys = export_events(arrow, standard)
    roots = inspect_closure(fixtures / "source" / "arrow-type",
                            ["ARROW_ABS_LAW", "ARROW_REP_LAW"])["roots"]
    claims = {node["id"]: node["theorem"] for node in arrow}
    laws = [call("hol-light:check-result", "$result", arrow_keys[root["id"]], "HOL:Nil",
                 source_term(claims[root["id"]]["conclusion"])) for root in roots]
    body = call("let", "$result", call("hol-light:standard-receive", "$events"),
                call("ArrowType", call("hol-light:accepted", "$result"), *laws,
                     call("hol-light:dependencies", "$result", arrow_keys[roots[1]["id"]]),
                     call("hol-light:receipt-count", "$result")))
    native_observation = read_forms(body)[0][3]
    def use_received(node: object) -> object:
        if node == Symbol("$result"):
            return Symbol("$received")
        if isinstance(node, list):
            return [use_received(child) for child in node]
        return node
    received_arrow = receive_program(arrow_events, binary,
        work / "hol-source-type-definition.views.metta", use_received(native_observation))
    execute("hol-source-type-definition", receive_source(arrow_events, body),
            ["[()]"] * (len(arrow_events) + 1) +
            ["[(ArrowType True True True HOL:Nil 19)]"],
            received_arrow, len(arrow_events) + 1)

    forged = call("HOL:CheckedStep", call("HOL:Source", "424242"),
        call("REFL", call("Var", '"x"', call("Tyvar", '"A"'))), "HOL:Nil",
        call("Const", '"T"', call("Tyapp", '"bool"', "HOL:Nil")))
    rejection = [events[0], forged, events[1]]
    refused = receive_program(rejection, binary, work / "hol-sticky-refusal.views.metta",
        form("StickyRefusal", form("hol-light:accepted", Symbol("$received")),
             form("hol-light:receipt-count", Symbol("$received"))))
    execute("hol-sticky-refusal", receive_source(rejection,
        call("hol-light:accepted", call("hol-light:standard-receive", "$events"))),
        ["[()]"] * (len(rejection) + 1) + ["[(StickyRefusal False None)]"],
        refused, len(rejection) + 1)

    mm0 = args.mm0_root.resolve()
    specification_fixture = mm0 / "tests" / "mm0-service" / "mmb-session.metta"
    witness_fixture = mm0 / "tests" / "mm0-service" / "textual-session.metta"
    source_forms = read_forms(specification_fixture.read_text())
    specification_names = {"mm0:mmb:test:sort", "mm0:mmb:test:constant",
                           "mm0:mmb:test:assertion", "mm0:mmb:test:spec"}
    specification = [node for node in source_forms if isinstance(node, list)
                     and len(node) == 3 and node[0] == "="
                     and node[1][0] in specification_names]
    if len(specification) != 4:
        raise AssertionError("retained MM0 specification inventory changed")
    # This package exercises the core service with its independently supplied
    # specification. The two textual-parser observations have their own
    # frontend qualification and are not counted as core-service observations.
    witnesses = read_forms(witness_fixture.read_text())[:-2]

    def core_fixture(node: object) -> object:
        if isinstance(node, list):
            if len(node) == 2 and node[0] == "mm0:text:start-file":
                if node[1] != '"tests/mm0-service/controls.mm0"':
                    raise AssertionError("MM0 fixture specification changed")
                return form("let", Symbol("$__guest_specification"),
                            form("mm0:mmb:test:spec"),
                            form("mm0:start", Symbol("$__guest_specification")))
            return [core_fixture(child) for child in node]
        if isinstance(node, tuple):
            query = core_fixture(node[1])
            if not isinstance(query, list) or query[0] != "println!":
                raise AssertionError("MM0 core observation is not an explicit print")
            return node[0], query[1]
        return node

    core_sources = [mm0 / "lib" / "mm0" / name for name in
                    ("data.metta", "kernel.generated.metta", "service.metta")]
    core = "\n".join(path.read_text() for path in core_sources) + "\n" + \
        "\n".join(map(show, specification + [core_fixture(node) for node in witnesses])) + "\n"
    expected_core = ["[()]", "[(MM0TextualPrepared True True True True True)]",
                     "[(MM0TextualUnfinished False)]", "[(MM0TextualOrdinaryAccepted True)]",
                     "[(MM0TextualOrdinaryFinished True)]",
                     "[(MM0TextualPrepared True True True True True)]",
                     "[(MM0TextualSharedAccepted True)]", "[(MM0TextualSharedFinished True)]",
                     "[(MM0TextualPrepared True True True True True)]",
                     "[(MM0TextualBadRootRefused False)]", "[(MM0TextualRefusalSticky False)]"]
    execute("mm0-core-service", core, expected_core, administrative=1)

    reset_fixture = mm0 / "tests" / "mm0-service" / "reset-spaces.metta"
    reset = (mm0 / "lib" / "mm0" / "data.metta").read_text() + "\n" + \
        reset_fixture.read_text()
    execute("mm0-private-space-controls", reset, ["[()]"] + ["[True]"] * 7,
            administrative=1)

    frontend_sources = [mm0 / "langdef" / "petta" / "generated" /
                        "mm0_assertion_environment_v1.metta", *core_sources,
                        mm0 / "lib" / "mm0" / "textual.metta"]
    def project_print(node: object) -> object:
        if isinstance(node, tuple):
            if not isinstance(node[1], list) or node[1][0] != "println!":
                raise AssertionError("MM0 observation is not an explicit print")
            return node[0], node[1][1]
        return node

    def frontend_unit(sources: list[Path], fixtures: list[Path]) -> str:
        # Module services are an explicit environment boundary. Source proof
        # rules, parsers and declarations remain in their original closure.
        forms = [node for path in sources for node in read_forms(path.read_text())
                 if not (isinstance(node, tuple) and isinstance(node[1], list)
                         and node[1][0] == "import!")]
        observations = [project_print(node) for path in fixtures
                        for node in read_forms(path.read_text())]
        return "\n".join(map(show, forms + observations)) + "\n"

    if not args.package or "mm0-textual-service" in args.package:
        frontend_body = frontend_unit(frontend_sources, [witness_fixture])
        translated, inventory = compile_source(frontend_body, binary,
            work / "mm0-textual-service.views.metta", ("langdef:load", "langdef:parse-file"),
            cons_values="spine-or-flat")
        module_boundary = "!(import! &self langdef)\n"
        expected_textual = ["[()]", *expected_core,
            "[(MM0TextualInvalidSpecification None)]", "[(MM0TextualInterleavedSpecification True)]"]
        source_textual = ["true"] + [row[1:-1].replace("True", "true").replace("False", "false")
                                     for row in expected_textual[2:]]
        execute("mm0-textual-service", module_boundary + frontend_body, expected_textual,
                (module_boundary + translated, inventory), 2, cwd=mm0,
                source_expected=source_textual)

    def target_observation(row: str) -> str:
        def boolean_view(node: object) -> object:
            if isinstance(node, Symbol) and node in ("true", "false"):
                return Symbol("True" if node == "true" else "False")
            if isinstance(node, list):
                return [boolean_view(child) for child in node]
            return node
        forms = read_forms(row)
        if len(forms) != 1:
            raise AssertionError("a fixture observation must contain one term")
        return "[" + show(boolean_view(forms[0])) + "]"

    extended_sources = [*frontend_sources, mm0 / "lib" / "mm0" / "mmu.metta"]
    binary_fixtures = [mm0 / "tests" / "mm0-service" / (name + ".metta") for name in
                       ("mmb-bytes", "mmb-decoder", "mmb-session", "mmb-layout", "mmb-features")]
    mmu_fixture = mm0 / "tests" / "mm0-service" / "mmu-resolution.metta"
    expected_binary_file = mm0 / "tests" / "mm0-service" / "mmb-expected.txt"
    mmu_source_harness = mm0 / "tools" / "test_mm0_mmu_resolution.sh"
    native_modules = "!(import! &self fs)\n!(import! &self str)\n!(import! &self langdef)\n"
    external_heads = ("fs:read-text", "str:from-codepoints", "langdef:load", "langdef:parse-file",
                      "str:byte-length", "str:byte-slice", "str:concat", "str:join",
                      "str:sha256", "str:unescape")
    error_heads = ("fs:read-text", "str:from-codepoints", "langdef:load", "langdef:parse-file")

    for label, sources, selected_fixtures in (
            ("mm0-binary-service", [*extended_sources, mm0 / "lib" / "mm0" / "mmb.metta"],
             binary_fixtures),
            ("mm0-mmu-service", extended_sources, [mmu_fixture])):
        if args.package and label not in args.package:
            continue
        if label == "mm0-binary-service":
            source_observations = expected_binary_file.read_text().splitlines()
            expected_count = 88
        else:
            block = re.search(r"<<'EXPECTED'\n(.*?)\nEXPECTED\n",
                              mmu_source_harness.read_text(), re.S)
            if block is None:
                raise AssertionError("MMU source harness expectation table is missing")
            source_observations = block.group(1).splitlines()
            expected_count = 53
        if len(source_observations) != expected_count:
            raise AssertionError(f"{label}: original observation inventory changed")
        body = frontend_unit(sources, selected_fixtures)
        translated, inventory = compile_source(body, binary, work / (label + ".views.metta"),
            external_heads, cons_values="spine-or-flat", capture_external_errors=error_heads)
        execute(label, native_modules + body,
                ["[()]"] * 4 + [target_observation(row) for row in source_observations],
                (native_modules + translated, inventory), 4, cwd=mm0,
                source_expected=["true"] * 3 + source_observations)

    receipt = {"schema": "petta-prime-guest-qualification-v1",
               "binary_sha256": digest(binary), "packages": packages,
               "evaluation_hashcons": args.eval_hashcons,
               "selected_packages": args.package,
               "complete_runtime_qualification": args.package is None,
               "sources": {str(repository / "lib" / "hol_light" / (name + ".metta")):
                           digest(repository / "lib" / "hol_light" / (name + ".metta"))
                           for name in LIBRARY},
               "resumption_source_sha256": digest(repository / "lib" / "hol_light" / "resumption.metta"),
               "mm0_sources": {str(path): digest(path) for path in
                               [*extended_sources, mm0 / "lib" / "mm0" / "mmb.metta",
                                *binary_fixtures, mmu_fixture, expected_binary_file,
                                mmu_source_harness, specification_fixture, witness_fixture,
                                reset_fixture]},
               "full_checker_soundness_proved": False,
               "prime_typed_hosting_proved": False}
    (work / "qualification.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"packages": len(packages), "observations":
                      sum(package["observations"] for package in packages)}))


if __name__ == "__main__":
    main()
