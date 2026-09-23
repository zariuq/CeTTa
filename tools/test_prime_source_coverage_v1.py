#!/usr/bin/env python3
"""Admission coverage from the existing Megalodon importer.

Positive documents are admitted by megalodon_declaration_import_v1, which
checks every declaration through the NIK authority. Sources the exporter
rejects are reported as rejected or unsupported and are not counted as
admitted. The HOTG family document is replayed by the existing native
projection qualifier when --family is set.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

import megalodon_declaration_import_v1 as importer
import test_megalodon_native_projection_v1 as native_tests


ROOT = Path(__file__).resolve().parents[1]
THEORY = Path("/home/aimama/aihub/Mettapedia/megalodon/theory")
REJECTED = (
    THEORY / "category/CategoryNov2022.mg",
    THEORY / "mizar/mml/card_1.mg",
    THEORY / "mizar/mml/schems_1.mg",
    THEORY / "logic/GoedelGod.mg",
)


def hosted_projection(cetta: Path, megalodon: Path, source: Path) -> None:
    """Retain the importer's conversion article and require the consumer to read it."""
    import megalodon_native_projection_v1 as native
    exported = importer.export_document(megalodon, source)
    items = importer.document(exported)
    projection = native.Projection(items)
    for index, item in enumerate(items):
        if item.kind in {"AXIOM", "THM"}:
            projection.materialize(index)
    rendered = projection.render()
    theorems = [item for item in items if item.kind == "THM"]
    axioms = [item for item in items if item.kind == "AXIOM"]
    if not theorems:
        raise SystemExit(f"{source.name} has no theorem to host")
    retained = []
    for command in projection.commands:
        if (isinstance(command, tuple) and command
                and command[0] == importer.sx.Symbol("add-atom")
                and isinstance(command[2], tuple)
                and command[2]
                and command[2][0] == importer.sx.Symbol("MegalodonHostedConversionV1")):
            retained.append(command[2])
    if len(retained) != len(theorems):
        raise SystemExit(
            f"retained {len(retained)} conversion articles for {len(theorems)} theorems")
    for index, item in enumerate(items):
        if item.kind != "THM":
            continue
        body = native.open_prefix(item.body, "typeAll", (), importer.poly.type_substitute_term)[0]
        inferred, _proof, _articles = projection.proof(item.proof, index, ())
        _common, article = importer.evidence.demand_conversion_article(
            projection.declarations(index), inferred, body)
        article_text = importer.sx.render(article)
        match = [record for record in retained
                 if record[3] == importer.sx.StringLiteral(item.label)]
        if len(match) != 1 or match[0][6].text != article_text:
            raise SystemExit(
                f"theorem {item.label} did not retain the importer conversion article")
    for item in axioms:
        if f'(MegalodonHostedAssumptionV1 ' not in rendered or item.label not in rendered:
            raise SystemExit(f"axiom {item.label} was not recorded as a hosted assumption")
    program = rendered + """
!(match &self (MegalodonHostedConversionV1 $name THM $label $inferred $declared $article)
   (HostedTheoremConversion $label))
!(match &self (MegalodonHostedAssumptionV1 $name AXIOM $label $formula)
   (HostedAssumption $label))
!(match &self (MegalodonHostedConversionV1 $name AXIOM $label $inferred $declared $article)
   (AxiomMisreadAsTheorem $label))
"""
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as handle:
        handle.write(program)
        program_path = Path(handle.name)
    completed = subprocess.run(
        [str(cetta), "--lang", "prime", str(program_path)], cwd=ROOT,
        capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.stderr or completed.stdout)
    output = completed.stdout
    for item in theorems:
        if f'(HostedTheoremConversion "{item.label}")' not in output:
            raise SystemExit(f"consumer did not require {item.label}'s conversion:\n{output}")
    for item in axioms:
        if f'(HostedAssumption "{item.label}")' not in output:
            raise SystemExit(f"consumer did not see hosted assumption {item.label}:\n{output}")
        if "AxiomMisreadAsTheorem" in output:
            raise SystemExit(f"axiom {item.label} was consumed as a proved conversion:\n{output}")
    print(f"hosted-not-selected {source.name} theorems={len(theorems)} axioms={len(axioms)} conversion-retained")


def hosted_specialization(cetta: Path, megalodon: Path, source: Path) -> None:
    """A polymorphic instance keeps the substitution articles open_prefix produces."""
    import megalodon_native_projection_v1 as native
    exported = importer.export_document(megalodon, source)
    items = importer.document(exported)
    projection = native.Projection(items)
    instances = []
    for typ in [("prop",), ("base", 0)]:
        for index, item in enumerate(items):
            if item.kind == "THM":
                instances.append(projection.materialize(index, (typ,)))
    if len(instances) < 2:
        raise SystemExit(f"{source.name} did not produce two specialized theorems")
    retained = []
    for command in projection.commands:
        if (isinstance(command, tuple) and command
                and command[0] == importer.sx.Symbol("add-atom")
                and isinstance(command[2], tuple) and command[2]
                and command[2][0] == importer.sx.Symbol("MegalodonHostedSpecializationV1")):
            retained.append(command[2])
    seen = set()
    for instance in instances:
        _body, articles = native.open_prefix(
            instance.declaration.body, "typeAll", instance.arguments,
            importer.poly.type_substitute_term)
        texts = tuple(importer.sx.render(article) for article in articles)
        match = [record for record in retained if record[1] == importer.sx.Symbol(instance.name)]
        if len(match) != 1:
            raise SystemExit(f"{instance.declaration.label} at {instance.arguments} "
                             f"retained {len(match)} specialization records")
        stored = tuple(literal.text for literal in match[0][4])
        if stored != texts or not texts:
            raise SystemExit(f"{instance.name} specialization article does not match open_prefix")
        seen.add(stored)
    if len(seen) < 2:
        raise SystemExit("distinct type instances retained the same specialization article")
    monomorphic = native.Projection(importer.document(importer.export_document(
        megalodon, ROOT / "tests/support/megalodon/positive_imp_identity.mg")))
    for index, item in enumerate(monomorphic.document):
        if item.kind == "THM":
            monomorphic.materialize(index)
    if any(isinstance(command, tuple) and len(command) > 2
           and isinstance(command[2], tuple) and command[2]
           and command[2][0] == importer.sx.Symbol("MegalodonHostedSpecializationV1")
           for command in monomorphic.commands):
        raise SystemExit("a monomorphic theorem was marked as a specialization")
    program = projection.render() + """
!(match &self (MegalodonHostedSpecializationV1 $name THM $arguments $articles)
   (HostedSpecialization $name))
"""
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as handle:
        handle.write(program)
        program_path = Path(handle.name)
    completed = subprocess.run(
        [str(cetta), "--lang", "prime", str(program_path)], cwd=ROOT,
        capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.stderr or completed.stdout)
    uses = completed.stdout.count("(HostedSpecialization ")
    if uses != len(instances):
        raise SystemExit(
            f"consumer read {uses} specializations, expected {len(instances)}:\n{completed.stdout}")
    print(f"specialization-retained {source.name} instances={len(instances)} distinct-articles={len(seen)}")


def _proof_step_records(projection, importer):
    records = []
    for command in projection.commands:
        if (isinstance(command, tuple) and command
                and command[0] == importer.sx.Symbol("add-atom")
                and isinstance(command[2], tuple) and command[2]
                and command[2][0] == importer.sx.Symbol("MegalodonHostedProofStepsV1")):
            records.append(command[2])
    return records


def _run_consumer(cetta: Path, program: str) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as handle:
        handle.write(program)
        program_path = Path(handle.name)
    completed = subprocess.run(
        [str(cetta), "--lang", "prime", str(program_path)], cwd=ROOT,
        capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.stderr or completed.stdout)
    return completed.stdout


def hosted_proof_steps(cetta: Path, megalodon: Path) -> None:
    """Proof translation keeps the articles it used to discard.

    The stored text has to equal a fresh translation and has to contain the
    article of the underlying source operation, recomputed here rather than
    read back out of the translator.
    """
    import megalodon_native_projection_v1 as native
    modus_source = ROOT / "tests/support/megalodon/positive_imp_modus_ponens.mg"
    modus_items = importer.document(importer.export_document(megalodon, modus_source))
    modus = native.Projection(modus_items)
    modus_index = next(index for index, item in enumerate(modus_items) if item.kind == "THM")
    modus.materialize(modus_index)
    modus_records = _proof_step_records(modus, importer)
    if len(modus_records) != 1:
        raise SystemExit(f"modus ponens retained {len(modus_records)} proof-step records")
    _prop, _proof, modus_articles = modus.proof(modus_items[modus_index].proof, modus_index, ())
    modus_stored = tuple(literal.text for literal in modus_records[0][4])
    modus_fresh = tuple(importer.sx.render(article) for article in modus_articles)
    if modus_stored != modus_fresh or not modus_stored:
        raise SystemExit("modus ponens proof-step record does not match its translation")
    parameter = ("named", modus_items[0].identifier)
    _common, conversion = importer.evidence.demand_conversion_article(
        modus.declarations(modus_index), parameter, parameter)
    if importer.sx.render(conversion) not in modus_stored:
        raise SystemExit("implication elimination dropped its conversion article")
    modus_output = _run_consumer(cetta, modus.render() + """
!(match &self (MegalodonHostedProofStepsV1 $name THM $label $articles)
   (HostedProofSteps $label (size-atom $articles)))
""")
    modus_line = f'(HostedProofSteps "modus_ponens" {len(modus_stored)})'
    if modus_line not in modus_output or len(modus_stored) < 1:
        raise SystemExit(f"consumer did not require the modus ponens articles:\n{modus_output}")

    forall_items = importer.document(importer.export_document(
        megalodon, ROOT / "tests/support/megalodon/positive_term_forall_identity.mg"))
    forall = native.Projection(forall_items)
    forall_index = next(index for index, item in enumerate(forall_items) if item.kind == "THM")
    forall.materialize(forall_index)
    forall_records = _proof_step_records(forall, importer)
    _prop, _proof, forall_articles = forall.proof(forall_items[forall_index].proof, forall_index, ())
    forall_stored = tuple(literal.text for literal in forall_records[0][4])
    if forall_stored != tuple(importer.sx.render(article) for article in forall_articles):
        raise SystemExit("forall identity proof-step record does not match its translation")
    quantified = forall_items[forall_index].body[1]
    _result, substitution = importer.poly.substitute(0, ("var", 0), quantified[2])
    if importer.sx.render(substitution) not in forall_stored:
        raise SystemExit("universal elimination dropped its substitution article")

    reuse_items = importer.document(importer.export_document(
        megalodon, ROOT / "tests/support/megalodon/positive_type_polymorphic_reuse.mg"))
    reuse = native.Projection(reuse_items)
    for index, item in enumerate(reuse_items):
        if item.kind == "THM":
            reuse.materialize(index, (("base", 0),))
    _specialized, specialization = importer.poly.substitute_type(0, ("base", 0), ("var", 0))
    specialization_text = importer.sx.render(specialization)
    reuse_records = _proof_step_records(reuse, importer)
    if len(reuse_records) != 2 or any(specialization_text not in tuple(
            literal.text for literal in record[4]) for record in reuse_records):
        raise SystemExit("a specialized proof dropped its type-argument substitution")

    hosted_items = importer.document(importer.export_document(
        megalodon, ROOT / "tests/support/megalodon/positive_hosted_assumption.mg"))
    hosted = native.Projection(hosted_items)
    for index, item in enumerate(hosted_items):
        if item.kind in {"AXIOM", "THM"}:
            hosted.materialize(index)
    if any(record[3].text == "given" for record in _proof_step_records(hosted, importer)):
        raise SystemExit("an axiom was given a proof-step record")
    hosted_output = _run_consumer(cetta, hosted.render() + """
!(match &self (MegalodonHostedProofStepsV1 $name THM $label $articles)
   (HostedProofSteps $label (size-atom $articles)))
!(match &self (MegalodonHostedProofStepsV1 $name AXIOM $label $articles)
   (AxiomProofSteps $label))
""")
    uses = next(record for record in _proof_step_records(hosted, importer)
                if record[3].text == "uses_given")
    uses_line = f'(HostedProofSteps "uses_given" {len(uses[4])})'
    if uses_line not in hosted_output or "AxiomProofSteps" in hosted_output:
        raise SystemExit(f"axiom or theorem proof steps were misread:\n{hosted_output}")

    parameter = importer.Declaration("PARAM", "carrier", "carrier-id", 1, type=("base", 0))
    body = ("typeApp", ("typeLam", ("named", "carrier-id")), ("prop",))
    definition = importer.Declaration(
        "DEF", "local-beta", "local-beta-id", 2, type=("base", 0), body=body)
    local = native.Projection([parameter, definition])
    local.materialize(1)
    _value, beta = importer.poly.type_substitute_term(0, ("prop",), ("named", "carrier-id"))
    term_records = [
        command[2] for command in local.commands
        if isinstance(command, tuple) and len(command) > 2 and isinstance(command[2], tuple)
        and command[2] and command[2][0] == importer.sx.Symbol("MegalodonHostedTermStepsV1")]
    if (len(term_records) != 1 or term_records[0][3].text != "local-beta"
            or importer.sx.render(beta) not in tuple(literal.text for literal in term_records[0][4])):
        raise SystemExit("a local type beta dropped its substitution article")
    local_output = _run_consumer(cetta, local.render() + """
!(match &self (MegalodonHostedTermStepsV1 $name DEF $label $articles)
   (HostedTermSteps $label (size-atom $articles)))
""")
    if '(HostedTermSteps "local-beta" 1)' not in local_output:
        raise SystemExit(f"consumer did not require the local type beta:\n{local_output}")
    print(f"proof-steps-retained modus-articles={len(modus_stored)} "
          f"forall-substitution specialization term-beta axiom-unmarked")


def admit(cetta: Path, megalodon: Path, source: Path) -> str:
    completed = subprocess.run(
        [sys.executable, str(ROOT / "tools/megalodon_declaration_import_v1.py"),
         "--megalodon", str(megalodon), "--cetta", str(cetta),
         "--profile", "empty", "--with-proofs", "--source", str(source)],
        cwd=ROOT, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.stderr or completed.stdout)
    line = [item for item in completed.stdout.splitlines()
            if item.startswith("MegalodonDeclarationImportV1")]
    if len(line) != 1:
        raise SystemExit("importer did not emit one coverage line:\n" + completed.stdout)
    print(f"admitted {source.name} {line[0]}")
    return line[0]


def reject(megalodon: Path, source: Path) -> None:
    try:
        importer.export_document(megalodon, source)
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or exc.stdout or "").strip().splitlines()
        reason = detail[-1] if detail else "export failed"
        print(f"rejected {source.name} {reason}")
        return
    raise SystemExit(f"{source.name} was exported; it is not a rejected source")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--hotg-preamble", type=Path)
    parser.add_argument("--family-source", type=Path)
    args = parser.parse_args()
    args.cetta = args.cetta.expanduser().resolve()
    args.megalodon = args.megalodon.expanduser().resolve()
    coverage = admit(args.cetta, args.megalodon,
                     ROOT / "tests/support/megalodon/positive_imp_identity.mg")
    hosted_projection(args.cetta, args.megalodon,
                      ROOT / "tests/support/megalodon/positive_hosted_assumption.mg")
    hosted_specialization(args.cetta, args.megalodon,
                          ROOT / "tests/support/megalodon/positive_type_polymorphic_reuse.mg")
    hosted_proof_steps(args.cetta, args.megalodon)
    if coverage != "MegalodonDeclarationImportV1 {'PARAM': 1, 'THM': 1}":
        raise SystemExit(f"unexpected positive coverage: {coverage}")
    for source in REJECTED:
        reject(args.megalodon, source)
    if args.family_source:
        if args.hotg_preamble is None:
            raise SystemExit("--family-source requires --hotg-preamble")
        native_tests.qualify_family_library(
            args.cetta, args.megalodon, args.hotg_preamble, args.family_source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
