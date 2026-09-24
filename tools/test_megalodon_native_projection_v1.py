#!/usr/bin/env python3
"""Actual source admission, retained-proof projection and dependent use."""

import argparse
from dataclasses import replace
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import megalodon_native_projection_v1 as native

source, sx = native.source, native.sx


def normalized_use(instance):
    """Compute dependent identity evidence while retaining the source package."""
    return f'''!(let $package (set:native-proof {instance.name})
 (unify $package (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
  (let $consumer (Lam $type (Refl (idx 0)))
   (let (SetNativeNormalFormV1 $checked $body $application $normal $source-type $normal-type)
     (set:native-normalize $package $consumer)
    (let (Refl $value) $normal
     (let (Id $carrier $left $right) $normal-type
      (ImportedNativeNormal {instance.position}
        (== $checked $package) (== $application (App $consumer $term))
        (== $left $value) (== $right $value)
        (size-atom $assumptions))))))
  (ImportedNativeNormalMalformedPackage {instance.position})))\n'''


class NativeProjectionTests(unittest.TestCase):
    cetta: Path
    megalodon: Path
    hotg_preamble: Path

    def test_retained_article_rechecks_source_before_dependent_use(self):
        exported = '''
        (PARAM "p" "p" 0 (PROP) 1)
        (AXIOM "a" "a" 0 (TMH "p") 2)
        (THM "t" "t" "proof-t" 0 (TMH "p") 3)
        (PROOF "t" (KNOWN "a")) (QED)
        '''
        library = native.SourceLibrary.select(exported, "empty", [], [], [])
        goal, proof = source.compile_document(source.State(), source.document(exported))
        article = source.dag.compile_shared_article(goal, proof).article
        with patch.object(source, "compile_document",
                          side_effect=AssertionError("retained evidence was regenerated")):
            projection, instances, count = library.replay(self.cetta, admission_article=article)
            self.assertEqual(count, 3)
            output = self.run_program(projection.render() + native.dependent_use(instances[0])
                                      + normalized_use(instances[0]))
            self.assertIn("[(ImportedNativeUse 2 True True 1)]", output)
            self.assertIn("[(ImportedNativeNormal 2 True True True True 1)]", output)
            # No native declarations may be produced when the source no longer
            # requests the proposition established by this retained article.
            changed = exported.replace('(TMH "p") 3', '(ALL (PROP) (DB 0)) 3')
            other = native.SourceLibrary.select(changed, "empty", [], [], [])
            with patch.object(native.Projection, "materialize",
                              side_effect=AssertionError("projection preceded admission")):
                with self.assertRaises(SystemExit):
                    other.replay(self.cetta, admission_article=article)
                with self.assertRaises(SystemExit):
                    replace(library, profile="egal").replay(self.cetta, admission_article=article)
                with self.assertRaises(SystemExit):
                    library.replay(self.cetta, admission_article=sx.Symbol("not-evidence"))
            # The logical admission goal records the theorem's proposition,
            # not its proof syntax. Reusing evidence must not bypass projection
            # and checking of the actual proof retained in the current source.
            malformed_proof = exported.replace('(KNOWN "a")', '(HYP 0)')
            malformed = native.SourceLibrary.select(malformed_proof, "empty", [], [], [])
            with self.assertRaisesRegex(ValueError, "hypothesis index"):
                malformed.replay(self.cetta, admission_article=article)
            alternate_proof = exported.replace('(KNOWN "a")',
                '(PPFAP (PLAM (TMH "p") (HYP 0)) (KNOWN "a"))')
            alternate = native.SourceLibrary.select(alternate_proof, "empty", [], [], [])
            alternative, retained, _ = alternate.replay(self.cetta, admission_article=article)
            self.assertNotEqual(retained[0].name, instances[0].name)
            output = self.run_program(alternative.render() + native.dependent_use(retained[0])
                                      + normalized_use(retained[0]))
            self.assertIn("[(ImportedNativeUse 2 True True 1)]", output)
            self.assertIn("[(ImportedNativeNormal 2 True True True True 1)]", output)

    def run_program(self, program):
        with tempfile.TemporaryDirectory(prefix="megalodon-native-") as directory:
            path = Path(directory) / "input.metta"
            path.write_text(program, encoding="utf-8")
            result = subprocess.run([str(self.cetta.resolve()), "--lang", "prime", str(path)],
                                    text=True, capture_output=True, check=True)
        return result.stdout

    def admit(self, items, initial_primitives=()):
        state = source.State(primitives=list(initial_primitives))
        for item in items:
            goal, proof = state.admit(item)
            article = source.dag.compile_shared_article(goal, proof).article
            source.poly.require_public_result(
                source.poly.run_cetta(self.cetta, goal, article), accepted=True)

    def test_actual_source_proofs_feed_dependent_consumers(self):
        path = Path(__file__).resolve().parents[1] / "tests/support/megalodon/positive_tactics_package.mg"
        output = subprocess.run([str(self.megalodon.resolve()), "-sexprinfo", str(path)],
                                text=True, capture_output=True, check=True).stdout
        items = source.document(output)
        self.admit(items)
        projection = native.Projection(items)
        proofs = [projection.materialize(i) for i, item in enumerate(items) if item.kind == "THM"]
        program = projection.render() + "".join(
            native.dependent_use(proof) + normalized_use(proof) for proof in proofs)
        result = self.run_program(program)
        for proof in proofs:
            if f"[(ImportedNativeUse {proof.position} True True 0)]" not in result:
                diagnostics = projection.render()
                diagnostics += "\n".join("!(try " + sx.render(c) + ")"
                    for c in projection.commands if c[0] == sx.Symbol("set:theorem"))
                diagnostics += f"\n!(try (set:native-proof {proof.name}))\n"
                result += self.run_program(diagnostics)
            self.assertIn(f"[(ImportedNativeUse {proof.position} True True 0)]", result,
                          msg=f"source theorem {proof.declaration.label}:\n{result}")
            self.assertIn(f"[(ImportedNativeNormal {proof.position} True True True True 0)]", result,
                          msg=f"computed source theorem {proof.declaration.label}:\n{result}")
        self.assertEqual(len(proofs), 10)
        self.assertEqual(sum(i.declaration.kind == "AXIOM" for i in projection.instances.values()), 0)
        identity = next(i for i in proofs if i.declaration.label == "test_assume")
        applied = self.run_program(projection.render() + f'''
!(let $package (set:native-proof {identity.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) $formula) $type
   (let (SetNativeUseV1 $checked $consumer $result $result-type)
    (set:native-use $package (Lam $type (App (idx 0) (DeclConst Falsum))))
    (ImportedApplication
     (== $result-type (Pi (App (DeclConst $family) (DeclConst Falsum))
                          (App (DeclConst $family) (DeclConst Falsum)))))))))
''')
        self.assertIn("[(ImportedApplication True)]", applied)

    def test_annotation_checks_formation_and_proof(self):
        result = self.run_program('''
!(set:theorem annotated (all prop (lam p (imp p p)))
 (pf:typed (all prop (lam p (imp p p)))
   (pf:all-intro (pf:imp-intro (pf:hyp 0)))))
!(let $r (try (set:theorem false-annotation (all prop (lam p p))
 (pf:typed (all prop (lam p p))
   (pf:all-intro (pf:imp-intro (pf:hyp 0)))))) (car-atom $r))
!(let $r (try (set:proves (pf:typed Empty (pf:hyp 0))
 (all prop (lam p p)))) (car-atom $r))
''')
        self.assertEqual(result.count("[Refuted]"), 2, result)

    def test_ordered_source_includes_are_replayed_and_retained(self):
        root = Path(__file__).resolve().parents[1] / "tests/support/megalodon"
        signatures = [root / "library_base.mgs", root / "library_extension.mgs"]
        main = root / "positive_library_includes.mg"
        exported = source.export_document(self.megalodon, main, signatures)
        library = native.SourceLibrary.select(exported, "empty", [], [], [])
        items, _ = library.resolve()
        self.assertEqual([i.kind for i in items], ["DEF", "AXIOM", "DEF", "THM"])
        self.assertEqual([i.label for i in items], ["imported_premise", "imported_evidence",
                                                   "imported_family", "imported_function"])
        # Replay the serialized export, not a Python environment supplied by
        # the source checker. The includes are part of the actual checked prefix.
        loaded = native.SourceLibrary.loads(library.dumps())
        projection, instances, prefix_length = loaded.replay(self.cetta)
        self.assertEqual(prefix_length, 4)
        result = self.run_program(projection.render() + normalized_use(instances[0]))
        self.assertIn("[(ImportedNativeNormal 3 True True True True 1)]", result)
        for wrong in [[], list(reversed(signatures))]:
            with self.assertRaises(subprocess.CalledProcessError):
                source.export_document(self.megalodon, main, wrong)

    def test_existential_witness_and_both_proofs_survive_import(self):
        path = Path(__file__).resolve().parents[1] / "tests/support/megalodon/positive_dependent_witness_use.mg"
        output = subprocess.run([str(self.megalodon.resolve()), "-sexprinfo", str(path)],
                                text=True, capture_output=True, check=True).stdout
        items = source.document(output)
        self.admit(items)
        projection = native.Projection(items)
        position = next(i for i, item in enumerate(items) if item.label == "split_witness")
        proofs = [projection.materialize(position, (typ,))
                  for typ in [("prop",), ("base", 0), ("arr", ("base", 0), ("prop",))]]
        result = self.run_program(projection.render() +
                                  "".join(native.dependent_use(proof) + normalized_use(proof)
                                          for proof in proofs))
        self.assertEqual(result.count(f"[(ImportedNativeUse {position} True True 0)]"), 3,
                         result)
        self.assertEqual(result.count(f"[(ImportedNativeNormal {position} True True True True 0)]"),
                         3, result)
        self.assertEqual(len({proof.name for proof in proofs}), 3)
        self.assertFalse(any(item.kind == "AXIOM" for item in items))

        # A witness can be reused; its evidence cannot silently change predicate.
        bad = replace(items[position], body=("typeAll", ("all", ("prop",), ("var", 0))))
        corrupted = [*items[:position], bad, *items[position + 1:]]
        with self.assertRaisesRegex(ValueError, "does not establish"):
            native.Projection(corrupted).materialize(position, (("base", 0),))

    def test_source_definitions_feed_aliased_identity_elimination(self):
        root = Path(__file__).resolve().parents[1] / "tests/support/megalodon"
        exported = source.export_document(self.megalodon,
            root / "positive_library_includes.mg",
            [root / "library_base.mgs", root / "library_extension.mgs"])
        library = native.SourceLibrary.loads(
            native.SourceLibrary.select(exported, "empty", [], [], []).dumps())
        projection, instances, prefix_length = library.replay(self.cetta)
        self.assertEqual(prefix_length, 4)
        theorem = instances[0]
        program = projection.render() + f'''
!(let $result (try (set:define ImportedJSecond
   (-> (A : (u 0)) (x : A)
       (P : (-> (y : A) (q : (id A x y)) (u 0)))
       (d : (P x (refl x))) (y : A) (q : (id A x y)) (P y q))
   (= ImportedJSecond ImportedJFirst)))
   (ForwardDefinition (car-atom $result)
     (== (collapse (match &self (: ImportedJSecond $type) $type)) ())))
!(set:define ImportedJFirst
   (-> (A : (u 0)) (x : A)
       (P : (-> (y : A) (q : (id A x y)) (u 0)))
       (d : (P x (refl x))) (y : A) (q : (id A x y)) (P y q))
   (= ImportedJFirst id:eliminate))
!(set:define ImportedJSecond
   (-> (A : (u 0)) (x : A)
       (P : (-> (y : A) (q : (id A x y)) (u 0)))
       (d : (P x (refl x))) (y : A) (q : (id A x y)) (P y q))
   (= ImportedJSecond ImportedJFirst))
!(let $package (set:native-proof {theorem.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $consumer
   (Lam $type
     (App (App (App (App (App (App (DeclConst ImportedJSecond) $type) (idx 0))
       (Lam $type (Lam (Id $type (idx 1) (idx 0)) (Id $type (idx 2) (idx 1)))))
       (Refl (idx 0))) (idx 0)) (Refl (idx 0))))
   (let (SetNativeNormalFormV1 $checked $body $application $normal $source-type $normal-type)
     (set:native-normalize $package $consumer)
    (let (Refl $value) $normal
     (let (Id $carrier $left $right) $normal-type
      (ImportedAliasedJ (== $checked $package) (== $application (App $consumer $term))
        (== $left $value) (== $right $value) (size-atom $assumptions))))))))
!(let $package (set:native-proof {theorem.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $bad-consumer
   (Lam $type
     (App (App (App (App (App (App (DeclConst ImportedJSecond) $type) (idx 0))
       (Lam $type (Lam (Id $type (idx 1) (idx 0)) (Id $type (idx 2) (idx 1)))))
       (idx 0)) (idx 0)) (Refl (idx 0))))
   (let $result (try (set:native-normalize $package $bad-consumer))
     (ImportedWrongJMethod (car-atom $result))))))
'''
        result = self.run_program(program)
        self.assertIn("[(ForwardDefinition Undetermined True)]", result)
        self.assertIn("[ImportedJFirst]", result)
        self.assertIn("[ImportedJSecond]", result)
        self.assertIn("[(ImportedAliasedJ True True True True 1)]", result)
        self.assertIn("[(ImportedWrongJMethod Refuted)]", result)

    def test_source_function_values_and_returned_closures(self):
        root = Path(__file__).resolve().parents[1] / "tests/support/megalodon"
        exported = source.export_document(self.megalodon,
                                          root / "positive_function_definitions.mg")
        library = native.SourceLibrary.loads(
            native.SourceLibrary.select(exported, "empty", [], [], []).dumps())
        projection, proofs, prefix_length = library.replay(self.cetta)
        self.assertEqual(prefix_length, 6)
        self.assertEqual(len(proofs), 2)
        definitions = {}
        for position, item in enumerate(projection.document):
            if item.kind == "DEF":
                definitions[item.label] = projection.materialize(position).name
        self.assertEqual(len(definitions), 4)
        commands = [command for command in projection.commands
                    if command[0] == sx.Symbol("set:define")]
        self.assertEqual(len(commands), 4)
        for command in commands:
            # A definition names the source value itself, not a saturated
            # clause whose unapplied head has different conversion behaviour.
            self.assertEqual(command[3][1], command[1])
        identity = definitions["imported_identity"]
        keep = definitions["imported_keep"]
        alias = definitions["imported_keep_alias"]
        apply = definitions["imported_apply"]
        program = projection.render() + f'''
!(add-atom &self (: sourceLeft set))
!(add-atom &self (: sourceRight set))
!(set:define sourceIdentity (-> set set) (= sourceIdentity (lam x x)))
!(set:define sourcePartial (-> set set) (= sourcePartial ({keep} sourceLeft)))
!(set:define sourceConstant (-> set set) (= sourceConstant (lam y sourceLeft)))
!(set:define sourceOpenClosure (-> set set set)
   (= sourceOpenClosure (lam x ({keep} x))))
!(set:define sourceKeep (-> set set set) (= sourceKeep (lam x (lam y x))))
!(set:define sourceCaptured (-> set set set) (= sourceCaptured (lam x (lam y y))))
!(SourceFunctions (type:eq &self {identity} sourceIdentity)
    (type:eq &self {keep} {alias}) (type:eq &self sourcePartial sourceConstant)
    (type:eq &self ({apply} {identity} sourceLeft) sourceLeft)
    (type:eq &self ({alias} sourceLeft sourceRight) sourceLeft)
    (type:eq &self ({alias} sourceLeft sourceRight) sourceRight)
    (type:eq &self sourceOpenClosure sourceKeep)
    (type:eq &self sourceOpenClosure sourceCaptured))
'''
        result = self.run_program(program + "".join(
            native.dependent_use(proof) + normalized_use(proof) for proof in proofs))
        self.assertIn("[(SourceFunctions True True True True True False True False)]", result)
        for proof in proofs:
            self.assertIn(f"[(ImportedNativeUse {proof.position} True True 0)]", result)
            self.assertIn(
                f"[(ImportedNativeNormal {proof.position} True True True True 0)]", result)

        # The same higher-order dependent program runs with a function and a
        # proof from the admitted source. Its result computes a set-valued pair
        # and retains identity evidence indexed by that exact source proof.
        pair_program = (root.parent.parent / "prime/scoped/native_higher_order_pair.metta").read_text()
        result = self.run_program(projection.render() + pair_program + "".join(
            f"!(checked-pair-update {proof.name} (DeclConst {identity}))\n"
            f"!(wrong-pair-update {proof.name})\n"
            f"!(checked-universe-domain-pair {proof.name})\n"
            f"!(let $specializer (family-identity (DeclConst Empty))\n"
            f"  (let $specialized (App $specializer (Lam (DeclConst set) (DeclConst set)))\n"
            f"    (let $result (checked-pair-update {proof.name} $specialized)\n"
            f"      (ImportedTypeFamily {proof.position} $result))))\n" for proof in proofs))
        for proof in proofs:
            self.assertIn(
                f"[(UniverseDomainPair {proof.name} True True True True True 0)]", result)
            self.assertIn(
                f"[(NativeHigherOrderPair {proof.name} True True False True True True True 0)]",
                result)
            self.assertIn(
                f"[(ImportedTypeFamily {proof.position} "
                f"(NativeHigherOrderPair {proof.name} True True False True True True True 0))]",
                result)
        self.assertIn("[(TypeFamilyRejection Refuted)]", result)
        self.assertIn("[(UniverseDomainRejection Refuted)]", result)
        self.assertEqual(result.count("[Refuted]"), 3)

    def test_source_beta_eta_redexes_are_preserved_before_runtime_checking(self):
        path = Path(__file__).resolve().parents[1] / "tests/support/megalodon/positive_beta_eta_definitions.mg"
        exported = subprocess.run([str(self.megalodon.resolve()), "-sexprinfo", str(path)],
                                  text=True, capture_output=True, check=True).stdout
        library = native.SourceLibrary.loads(
            native.SourceLibrary.select(exported, "empty", [], [], []).dumps())
        projection, proofs, prefix_length = library.replay(self.cetta)
        self.assertEqual((prefix_length, len(proofs)), (5, 1))
        expected = {
            "eta_outer": "(lam mg-bound-0 (lam mg-bound-1 (mg-bound-0 mg-bound-1)))",
            "eta_inner": "(lam mg-bound-0 (lam mg-bound-1 (lam mg-bound-2 ((mg-bound-0 mg-bound-1) mg-bound-2))))",
            "beta_keep": "(lam mg-bound-0 (lam mg-bound-1 ((lam mg-bound-2 mg-bound-0) mg-bound-1)))",
            "twice_apply": "(lam mg-bound-0 (lam mg-bound-1 (mg-bound-0 (mg-bound-0 mg-bound-1))))",
        }
        names = {}
        for position, item in enumerate(projection.document):
            if item.kind != "DEF":
                continue
            name = projection.materialize(position).name
            names[item.label] = name
            command = next(c for c in projection.commands
                           if c[0] == sx.Symbol("set:define") and c[1] == sx.Symbol(name))
            # Compare complete terms, including variable identity and application
            # order, not just types or binder counts. These expectations are
            # independently transcribed from the source fixture above.
            self.assertEqual(sx.render(command[3][2]), expected[item.label])
        program = projection.render() + f'''
!(add-atom &self (: sourceLeft set))
!(add-atom &self (: sourceRight set))
!(set:define sourceEtaIdentity (-> (-> set set) (-> set set))
   (= sourceEtaIdentity (lam f f)))
!(PreservedSource
   (type:eq &self {names['eta_outer']} sourceEtaIdentity)
   (type:eq &self ({names['beta_keep']} sourceLeft sourceRight) sourceLeft)
   (type:eq &self ({names['beta_keep']} sourceLeft sourceRight) sourceRight)
   (type:eq &self {names['twice_apply']} {names['eta_outer']}))
'''
        result = self.run_program(program + native.dependent_use(proofs[0]) + normalized_use(proofs[0]))
        self.assertIn("[(PreservedSource True True False False)]", result)
        self.assertIn(f"[(ImportedNativeUse {proofs[0].position} True True 0)]", result)
        self.assertIn(f"[(ImportedNativeNormal {proofs[0].position} True True True True 0)]", result)

    def test_source_alias_lookup_keeps_opaque_and_type_boundaries(self):
        definitions = native.definitions
        typ = ("arr", ("base", 0), ("base", 0))
        body = ("lam", ("base", 0), ("var", 0))
        alias = ("shared", typ, ("named", "shared"))
        prior = ("shared", typ, body)
        chosen = definitions.definition_member_article([alias, alias, prior], "shared")
        self.assertEqual(chosen[:2], (typ, body))
        encoded = definitions.encode_declarations([alias, alias, prior])
        step = source.poly.mono.proof_node("megalodon-def-reduce-delta",
            [encoded, source.poly.mono.app("shared"), source.poly.encode_tp(typ),
             source.poly.encode_tm(body)], [chosen[2]])
        goal = source.poly.mono.app("MDefinitionReduces", encoded,
            source.poly.encode_tm(("named", "shared")), source.poly.encode_tm(body))
        source.poly.require_public_result(source.poly.run_cetta(self.cetta, goal, step), accepted=True)
        wrong = source.poly.mono.app("MDefinitionReduces", encoded,
            source.poly.encode_tm(("named", "shared")), source.poly.encode_tm(("named", "forged")))
        source.poly.require_public_result(source.poly.run_cetta(self.cetta, wrong, step), accepted=False)
        self.assertIsNone(definitions.definition_member_article([alias], "shared"))
        self.assertIsNone(definitions.definition_member_article(
            [alias, ("shared", typ, None), prior], "shared"))
        self.assertIsNone(definitions.definition_member_article(
            [alias, ("shared", ("prop",), ("named", "other")), prior], "shared"))
        replacement = ("named", "different")
        chosen = definitions.definition_member_article(
            [("shared", typ, replacement), prior], "shared")
        self.assertEqual(chosen[:2], (typ, replacement))

    def test_multiple_source_type_parameters_keep_declaration_order(self):
        root = Path(__file__).resolve().parents[1] / "tests/support/megalodon"
        exported = source.export_document(self.megalodon,
                                          root / "positive_multiple_type_parameters.mg")
        items = source.document(exported)
        self.assertEqual([i.kind for i in items], ["DEF", "THM", "DEF", "THM"])
        library = native.SourceLibrary(exported, "empty", (
            (1, "THM", ("(SET)", "(PROP)")),
            (3, "THM", ("(SET)", "(PROP)", "(AR (SET) (PROP))"))))
        projection, proofs, prefix = native.SourceLibrary.loads(library.dumps()).replay(self.cetta)
        self.assertEqual(prefix, 4)
        first = (("base", 0), ("prop",))
        three = (*first, ("arr", ("base", 0), ("prop",)))
        keep = projection.materialize(0, first)
        compose = projection.materialize(2, three)
        self.assertEqual(native.open_prefix(items[0].type, "all", first, native.poly.substitute_type)[0],
                         ("arr", first[0], ("arr", first[1], first[0])))
        program = projection.render() + f'''
!(set:define expectedKeep (-> set prop set) (= expectedKeep (lam x (lam y x))))
!(set:define expectedCompose
   (-> (-> prop (-> set prop)) (-> set prop) set (-> set prop))
   (= expectedCompose (lam f (lam g (lam x (f (g x)))))))
!(MultipleParameters (type:eq &self {keep.name} expectedKeep)
                    (type:eq &self {compose.name} expectedCompose))
'''
        result = self.run_program(program + "".join(
            native.dependent_use(proof) + normalized_use(proof) for proof in proofs))
        self.assertIn("[(MultipleParameters True True)]", result)
        for proof in proofs:
            self.assertIn(f"[(ImportedNativeUse {proof.position} True True 0)]", result)
            self.assertIn(
                f"[(ImportedNativeNormal {proof.position} True True True True 0)]", result)

    def test_actual_hotg_universe_assumptions_feed_dependent_consumers(self):
        output = subprocess.run([str(self.megalodon.resolve()), "-sexprinfo",
                                 str(self.hotg_preamble)],
                                text=True, capture_output=True, check=True).stdout
        items = source.declarations(output)
        labels = {"set_ext", "UnivOf_In", "UnivOf_TransSet",
                  "UnivOf_ZF_closed", "UnivOf_Min"}
        selected = [i for i, item in enumerate(items) if item.label in labels]
        self.assertEqual({items[i].label for i in selected}, labels)
        # Admit the real ordered prefix, not a synthetic environment assembled
        # only from the declarations the projection happens to use.
        primitives = source.egal_initial_primitives()
        self.admit(items[:max(selected) + 1], primitives)
        projection = native.Projection(items, primitives)
        instances = [projection.materialize(i) for i in selected]
        self.assertTrue(all(i.declaration.kind == "AXIOM" for i in instances))
        result = self.run_program(projection.render() +
                                  "".join(native.dependent_use(i) + normalized_use(i)
                                          for i in instances))
        for instance in instances:
            self.assertIn(f"[(ImportedNativeUse {instance.position} True True 1)]", result)
            self.assertIn(
                f"[(ImportedNativeNormal {instance.position} True True True True 1)]", result)
        self.assertFalse(any(i.declaration.kind == "THM" for i in projection.instances.values()))
        self.assertTrue(any(i.declaration.label == "ZF_closed"
                            and i.declaration.kind == "DEF"
                            for i in projection.instances.values()))

    def test_source_universe_pipeline_builds_and_consumes_dependent_pair(self):
        path = Path(__file__).resolve().parents[1] / "tests/support/megalodon/positive_universe_pipeline.mg"
        # Keep the actual source prefix verbatim, including its association of
        # conjunctions. Later preamble axioms are not used by this development.
        preamble = self.hotg_preamble.read_text(encoding="utf-8")
        prefix, marker, _ = preamble.partition("Axiom FalseE :")
        self.assertTrue(marker, "HOTG preamble boundary changed")
        with tempfile.TemporaryDirectory(prefix="megalodon-universe-prefix-") as directory:
            signature = Path(directory) / "universe_prefix.mgs"
            signature.write_text(prefix, encoding="utf-8")
            exported = source.export_document(self.megalodon, path, [signature])
        library = native.SourceLibrary.loads(
            native.SourceLibrary.select(exported, "egal", ["pipeline_membership"], [], []).dumps())
        projection, instances, prefix_length = library.replay(self.cetta)
        self.assertEqual(prefix_length, 38)  # 35 original items, three derived theorems.
        theorem, = instances
        by_label = {instance.declaration.label: instance
                    for instance in projection.instances.values()}
        membership = by_label["In"].name
        power = by_label["Power"].name
        union = by_label["Union"].name
        empty = by_label["Empty"].name
        univ = by_label["UnivOf"].name
        support = by_label["UnivOf_ZF_closed"].name
        seed_law = by_label["UnivOf_In"].name
        power_law = by_label["pipeline_power_closure"].name
        union_law = by_label["pipeline_union_closure"].name
        self.assertEqual({i.declaration.label for i in projection.instances.values()
                          if i.declaration.kind == "AXIOM"},
                         {"UnivOf_In", "UnivOf_ZF_closed"})
        program = projection.render() + f'''
(= (imported-universe-consumer
      (App (DeclConst $family) (App (App (DeclConst {membership}) $value) $universe))
      $first $continuation)
   (Lam (App (DeclConst $family) (App (App (DeclConst {membership}) $value) $universe))
     (App (Lam
       (Sigma (DeclConst set)
         (App (DeclConst $family) (App (App (DeclConst {membership}) (idx 0)) $universe)))
       $continuation)
       (Pair $first (idx 0)))))
!(let $package (set:native-proof {theorem.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst {membership}) $value) $universe)) $type
   (let $consumer (imported-universe-consumer $type $value (idx 0))
    (let (SetNativeNormalFormV1 $checked $body $application
       (Pair $first $second) $original-type $result-type)
       (set:native-normalize $package $consumer)
     (let (SetNativeNormalFormV1 $proof-checked $proof-body $proof-application
            $proof-normal $proof-before $proof-after)
          (set:native-normalize $package (Lam $type (idx 0)))
      (ImportedUniversePair (== $checked $package)
        (== $prop (({membership} ({power} ({union} ({power} {empty})))) ({univ} {empty})))
        (== $first $value)
        (== $second $proof-normal)
        (== $result-type (Sigma (DeclConst set)
          (App (DeclConst $family) (App (App (DeclConst {membership}) (idx 0)) $universe))))
        (size-atom $assumptions))))))))
!(let $package (set:native-proof {theorem.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst {membership}) $value) $universe)) $type
   (let $consumer (imported-universe-consumer $type $value (Fst (idx 0)))
    (let (SetNativeNormalFormV1 $checked $body $application $normal $before $after)
       (set:native-normalize $package $consumer)
      (ImportedUniverseValue (== $normal $value) (== $after (DeclConst set))))))))
!(add-atom &self (imported-operation {power} {power_law}))
!(add-atom &self (imported-operation {union} {union_law}))
(= (imported-pipeline Nil $value $proof) (ProvedSet $value $proof))
(= (imported-pipeline (Cons $operation $rest) $value $proof)
   (let $law (match &self (imported-operation $operation $name) $name)
     (let $next-proof
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:known $law) {empty}) $value) $proof)
       (case (set:proves $next-proof (({membership} ($operation $value)) ({univ} {empty})))
         ((True (imported-pipeline $rest ($operation $value) $next-proof)))))))
!(let (ProvedSet $value $proof)
   (imported-pipeline (Cons {power} (Cons {union} (Cons {power} (Cons {power} Nil)))) {empty}
     (pf:all-elim (pf:known {seed_law}) {empty}))
   (set:theorem imported-runtime-membership (({membership} $value) ({univ} {empty})) $proof))
!(let $package (set:native-proof imported-runtime-membership)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst {membership}) $value) $universe)) $type
   (let $consumer (imported-universe-consumer $type $value (Fst (idx 0)))
    (let (SetNativeNormalFormV1 $checked $body $application $normal $before $after)
       (set:native-normalize $package $consumer)
      (ImportedRuntimePipeline
        (== $normal (App (DeclConst {power}) (App (DeclConst {power})
          (App (DeclConst {union}) (App (DeclConst {power}) (DeclConst {empty}))))))
        (== $after (DeclConst set)) (size-atom $assumptions)))))))
!(let $package (set:native-proof {theorem.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst {membership}) $value) $universe)) $type
   (let $consumer (imported-universe-consumer $type (App (DeclConst {power}) $value) (idx 0))
    (let $verdict (try (set:native-normalize $package $consumer))
      (ImportedChangedIndex (car-atom $verdict)))))))
!(let $package (set:native-proof {theorem.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) (App (App (DeclConst {membership}) $value) $universe)) $type
   (let $consumer (imported-universe-consumer $type $value (Fst (idx 0)))
    (let $removed (match &self
      (set:known {support} $p $origin $tree $depends $revision $signature)
      (remove-atom &self (set:known {support} $p $origin $tree $depends $revision $signature)))
     (let $verdict (try (set:native-normalize $package $consumer))
      (ImportedWithdrawn (car-atom $verdict))))))))
'''
        result = self.run_program(program)
        self.assertIn("[(ImportedUniversePair True True True True True 2)]", result)
        self.assertIn("[(ImportedUniverseValue True True)]", result)
        self.assertIn("[(ImportedRuntimePipeline True True 2)]", result)
        self.assertIn("[(ImportedChangedIndex Refuted)]", result)
        self.assertIn("[(ImportedWithdrawn Undetermined)]", result)

    def test_command_line_selection_keeps_axioms_explicit(self):
        command = [sys.executable, str(Path(native.__file__).resolve()),
                   "--megalodon", str(self.megalodon.resolve()),
                   "--source", str(self.hotg_preamble.resolve()),
                   "--profile", "egal", "--cetta", str(self.cetta.resolve()), "--run"]
        accepted = subprocess.run([*command, "--axiom", "UnivOf_Min"],
                                  capture_output=True, text=True, check=True)
        self.assertIn("admitted_prefix=35 selected_theorems=0 selected_axioms=1",
                      accepted.stderr)
        self.assertIn("[(ImportedNativeUse 34 True True 1)]", accepted.stdout)
        rejected = subprocess.run([*command, "--theorem", "UnivOf_Min"],
                                  capture_output=True, text=True)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn("wrong kind", rejected.stderr)
        self.assertNotIn("ImportedNativeUse", rejected.stdout)

    def test_actual_polymorphic_reuse_at_distinct_types(self):
        path = Path(__file__).resolve().parents[1] / "tests/support/megalodon/positive_type_polymorphic_reuse.mg"
        output = subprocess.run([str(self.megalodon.resolve()), "-sexprinfo", str(path)],
                                text=True, capture_output=True, check=True).stdout
        items = source.document(output)
        self.admit(items)
        projection = native.Projection(items)
        instances = []
        for typ in [("prop",), ("base", 0), ("arr", ("base", 0), ("base", 0))]:
            instances.extend(projection.materialize(i, (typ,))
                             for i, item in enumerate(items) if item.kind == "THM")
        result = self.run_program(projection.render() +
                                  "".join(native.dependent_use(i) for i in instances))
        self.assertEqual(len(instances), 6)
        self.assertEqual(len(set(i.name for i in instances)), 6)
        self.assertEqual(result.count(" True True 0)]"), 6, result)
        for arguments in [(), (("var", 0),), (("prop",), ("prop",))]:
            with self.assertRaises((ValueError, SystemExit)):
                projection.materialize(0, arguments)

    def test_assumptions_remain_visible_and_withdrawal_invalidates_use(self):
        items = source.document('''
        (PARAM "p" "source-p" 0 (PROP) 1)
        (AXIOM "given" "source-given" 0 (TMH "source-p") 2)
        (THM "copy" "source-copy" "source-proof" 0 (TMH "source-p") 3)
        (PROOF "copy" (KNOWN "source-given")) (QED)
        ''')
        self.admit(items)
        projection = native.Projection(items)
        theorem = projection.materialize(2)
        assumption = projection.instances[(1, ())]
        result = self.run_program(projection.render() + native.dependent_use(theorem) + f'''
!(let $package (set:native-proof {theorem.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $removed (match &self
    (set:known {assumption.name} $p $origin $tree $depends $revision $signature)
    (remove-atom &self (set:known {assumption.name} $p $origin $tree $depends $revision $signature)))
   (let $out (try (set:native-use $package (Lam $type (Refl (idx 0)))))
     (car-atom $out)))))
''')
        self.assertIn("[(ImportedNativeUse 2 True True 1)]", result)
        self.assertIn("[Undetermined]", result)

    def test_retained_package_survives_unrelated_definition(self):
        exported = '''
        (PARAM "p" "revision-p" 0 (PROP) 1)
        (AXIOM "given" "revision-given" 0 (TMH "revision-p") 2)
        (THM "copy" "revision-copy" "revision-proof" 0 (TMH "revision-p") 3)
        (PROOF "copy" (KNOWN "revision-given")) (QED)
        '''
        library = native.SourceLibrary.select(exported, "empty", ["copy"], [], [])
        projection, instances, _ = library.replay(self.cetta)
        theorem = instances[0]
        result = self.run_program(projection.render() + f'''
!(let $package (set:native-proof {theorem.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $definition (set:define unrelated-identity (-> prop prop)
                     (= (unrelated-identity $p) $p))
   (let (SetNativeNormalFormV1 $checked $body $application $normal $source-type $normal-type)
      (set:native-normalize $package (Lam $type (Refl (idx 0))))
    (UnrelatedDefinitionKeepsProof (== $checked $package)
      (== $normal (Refl $term)) (size-atom $assumptions))))))
!(let $package (set:native-proof {theorem.name})
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $definition (set:define later-dependent-id (-> (A : (u 0)) (x : A) A)
                     (= (later-dependent-id $A $x) $x))
   (let (SetNativeNormalFormV1 $checked $body $application $normal $source-type $normal-type)
      (set:native-normalize $package
        (Lam $type (Refl (App (App (DeclConst later-dependent-id) $type) (idx 0)))))
    (ImportedLaterDeclaredConsumer (== $checked $package)
      (== $normal (Refl $term)) (size-atom $assumptions))))))
''')
        self.assertIn("[(UnrelatedDefinitionKeepsProof True True 1)]", result)
        self.assertIn("[(ImportedLaterDeclaredConsumer True True 1)]", result)

    def test_source_occurrence_and_prefix_order_are_retained(self):
        items = source.document('''
        (THM "select" "same" "proof-a" 2
          (ALL (TPVAR 0) (ALL (TPVAR 1)
            (ALL (PROP) (IMP (DB 0) (DB 0))))) 1)
        (PROOF "select" (TLAM (TPVAR 0) (TLAM (TPVAR 1)
          (TLAM (PROP) (PLAM (DB 0) (HYP 0)))))) (QED)
        (THM "select-again" "same" "proof-b" 2
          (ALL (TPVAR 0) (ALL (TPVAR 1)
            (ALL (PROP) (IMP (DB 0) (DB 0))))) 2)
        (PROOF "select-again" (PTPAP (PTPAP (KNOWN "same") (TPVAR 0)) (TPVAR 1))) (QED)
        ''')
        self.admit(items)
        projection = native.Projection(items)
        args = (("base", 0), ("prop",))
        first, second = projection.materialize(0, args), projection.materialize(1, args)
        self.assertNotEqual(first.name, second.name)
        self.assertEqual(first.proposition, second.proposition)
        self.assertEqual(first.proposition[1], sx.Symbol("set"))
        self.assertEqual(first.proposition[2][2][1], sx.Symbol("prop"))
        result = self.run_program(projection.render() + native.dependent_use(second))
        self.assertIn("[(ImportedNativeUse 1 True True 0)]", result)

    def test_missing_source_dependency_and_forged_proof_rejected(self):
        items = source.document('''
        (THM "bad" "bad" "bad-proof" 0 (ALL (PROP) (DB 0)) 1)
        (PROOF "bad" (KNOWN "later")) (QED)
        ''')
        with self.assertRaises(ValueError):
            native.Projection(items).materialize(0)
        items = source.document('''
        (THM "bad" "bad" "bad-proof" 0 (ALL (PROP) (DB 0)) 1)
        (PROOF "bad" (TLAM (PROP) (PLAM (DB 0) (HYP 0)))) (QED)
        ''')
        with self.assertRaises(ValueError):
            native.Projection(items).materialize(0)

    def test_explicit_initial_primitive_signature(self):
        # Choice is a typed constant here, not a choice axiom. The proof is
        # implication identity at a formula mentioning that constant.
        formula = '(AP (DB 0) (AP (TPAP (PRIM 0) (SET)) (DB 0)))'
        items = source.document(f'''
        (THM "primitive-use" "primitive-formula" "primitive-proof" 0
          (ALL (AR (SET) (PROP)) (IMP {formula} {formula})) 1)
        (PROOF "primitive-use"
          (TLAM (AR (SET) (PROP)) (PLAM {formula} (HYP 0)))) (QED)
        ''')
        primitives = source.egal_initial_primitives()
        self.admit(items, primitives)
        projection = native.Projection(items, primitives)
        instance = projection.materialize(0)
        result = self.run_program(projection.render() + native.dependent_use(instance))
        self.assertIn("[(ImportedNativeUse 0 True True 0)]", result)
        self.assertEqual(len(projection.primitive_instances), 1)
        with self.assertRaises(ValueError):
            native.Projection(items).materialize(0)
        with self.assertRaises(ValueError):
            projection.initial_primitive(1, (("base", 0),))

    def test_source_revision_changes_instance_namespace(self):
        items = source.document('''
        (PARAM "p" "p" 0 (PROP) 1)
        (AXIOM "given" "same-label-and-identity" 0 (TMH "p") 2)
        ''')
        original = native.Projection(items).materialize(1)
        revised = [*items[:1], replace(items[1], body=("imp", ("named", "p"), ("named", "p")))]
        self.assertNotEqual(original.name, native.Projection(revised).materialize(1).name)
        changed_prefix = [replace(items[0], label="renamed-source-parameter"), items[1]]
        self.assertNotEqual(original.name, native.Projection(changed_prefix).materialize(1).name)
        extended = [*items, replace(items[0], identifier="later", line=3)]
        self.assertEqual(original.name, native.Projection(extended).materialize(1).name)

    def test_saved_library_supports_later_development_without_megalodon(self):
        root = Path(__file__).resolve().parents[1]
        command = [sys.executable, str(Path(native.__file__).resolve()),
                   "--cetta", str(self.cetta.resolve())]
        with tempfile.TemporaryDirectory(prefix="megalodon-library-") as directory:
            library = Path(directory) / "library.json"
            exported = subprocess.run([*command, "--megalodon", str(self.megalodon.resolve()),
                "--source", str(root / "tests/support/megalodon/positive_tactics_package.mg"),
                "--theorem", "test_assume", "--save-library", str(library)],
                capture_output=True, text=True, check=True)
            self.assertTrue(library.exists())
            # The second process has neither the source file nor Megalodon as
            # an input. Only the library and the separately authored consumer.
            loaded = subprocess.run([*command, "--load-library", str(library), "--run",
                "--consumer", str(root / "tests/support/megalodon/library_consumer.metta")],
                capture_output=True, text=True, check=True)
            self.assertIn("[(LibraryDerivedProof True 0)]", loaded.stdout)
            self.assertIn("[(LibraryComputedProof True True)]", loaded.stdout)
            self.assertEqual(exported.stderr, loaded.stderr)
            emitted = subprocess.run([*command, "--load-library", str(library)],
                                      capture_output=True, text=True, check=True)
            self.assertEqual(exported.stdout, emitted.stdout)
            # Source and instance overrides would silently change the saved
            # request; require a newly constructed library instead.
            ambiguous = subprocess.run([*command, "--load-library", str(library),
                "--profile", "egal"], capture_output=True, text=True)
            self.assertNotEqual(ambiguous.returncode, 0)
            overwrite = subprocess.run([*command, "--load-library", str(library),
                "--save-library", str(library)], capture_output=True, text=True)
            self.assertNotEqual(overwrite.returncode, 0)

    def test_library_replays_every_prefix_item_and_rejects_bad_proofs(self):
        good = '''
        (THM "identity" "id" "id-proof" 0 (ALL (PROP) (IMP (DB 0) (DB 0))) 1)
        (PROOF "identity" (TLAM (PROP) (PLAM (DB 0) (HYP 0)))) (QED)
        '''
        library = native.SourceLibrary.select(good, "empty", ["identity"], [], [])
        restored = native.SourceLibrary.loads(library.dumps())
        self.assertEqual(restored, library)
        projection, instances, prefix = restored.replay(self.cetta)
        self.assertEqual(prefix, 1)
        self.assertIn("[(ImportedNativeUse 0 True True 0)]", self.run_program(
            projection.render() + native.dependent_use(instances[0])))
        bad = '''
        (THM "bad" "bad" "bad-proof" 0 (ALL (PROP) (DB 0)) 0)
        (PROOF "bad" (TLAM (PROP) (PLAM (DB 0) (HYP 0)))) (QED)
        '''
        # The bad preceding theorem is not a dependency of identity. Ordered
        # source admission must still reject it, not just project the good item.
        corrupted = native.SourceLibrary.select(bad + good, "empty", ["identity"], [], [])
        with self.assertRaises((ValueError, SystemExit)):
            corrupted.replay(self.cetta)
        # Independently reject a faulty proof producer at the actual NIK gate.
        compiler = source.compile_document
        def forged(state, items):
            goal, _ = compiler(state, items)
            return goal, source.node("megalodon-theory-checks-nil", [state.environment()], [])
        with patch.object(source, "compile_document", forged):
            with self.assertRaisesRegex(SystemExit, "expected.*True"):
                restored.replay(self.cetta)

    def test_library_assumptions_and_type_instances_are_explicit(self):
        exported = '''
        (AXIOM "assumed" "assumed" 1 (ALL (TPVAR 0) (ALL (PROP) (IMP (DB 0) (DB 0)))) 1)
        '''
        library = native.SourceLibrary(exported, "empty",
            ((0, "AXIOM", ("(SET)",)), (0, "AXIOM", ("(AR (SET) (PROP))",))))
        projection, instances, _ = native.SourceLibrary.loads(library.dumps()).replay(self.cetta)
        self.assertNotEqual(instances[0].name, instances[1].name)
        result = self.run_program(projection.render() +
                                  "".join(native.dependent_use(i) for i in instances))
        self.assertEqual(result.count("[(ImportedNativeUse 0 True True 1)]"), 2, result)
        wrong_kind = replace(library, requests=((0, "THM", ("(SET)",)),))
        with self.assertRaisesRegex(ValueError, "wrong kind"):
            native.SourceLibrary.loads(wrong_kind.dumps())
        open_type = replace(library, requests=((0, "AXIOM", ("(TPVAR 0)",)),))
        with self.assertRaises((ValueError, SystemExit)):
            open_type.replay(self.cetta)

    def test_hosted_article_address_opens_the_stored_text(self):
        projection = native.Projection([])
        small = (sx.Symbol("conv"), sx.Symbol("a"), sx.StringLiteral("step"))
        small_node = projection.host_article(small)
        self.assertIsInstance(small_node, sx.StringLiteral)
        self.assertEqual(projection.open_hosted_article(small_node), small)
        large = sx.StringLiteral("article-body-" + ("x" * 70000))
        large_node = projection.host_article(large)
        self.assertEqual(large_node[0], sx.Symbol("sha256"))
        digest = large_node[1].text
        self.assertNotEqual(digest, sx.render(large))
        self.assertEqual(projection.open_hosted_article(large_node), large)
        projection.commands.append(native.expr(
            "add-atom", sx.Symbol("&self"), native.expr(
                "MegalodonHostedProofStepsV1", sx.Symbol("thm"),
                sx.Symbol("THM"), sx.StringLiteral("named"),
                (small_node, large_node))))
        self.assertTrue(projection.proof_steps_match("thm", (small, large)))
        saved = projection.hosted_articles[digest]
        projection.hosted_articles[digest] = saved + " "
        self.assertIsNone(projection.open_hosted_article(large_node))
        self.assertFalse(projection.proof_steps_match("thm", (small, large)))
        del projection.hosted_articles[digest]
        self.assertIsNone(projection.open_hosted_article(large_node))
        self.assertFalse(projection.proof_steps_match("thm", (small, large)))
        projection.hosted_articles[digest] = saved

    def test_library_schema_does_not_accept_claimed_authority(self):
        data = {"format": "MegalodonSourceLibraryV1", "source_export": "",
                "profile": "empty", "instances": [], "accepted": True}
        with self.assertRaises(ValueError):
            native.SourceLibrary.loads(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            native.SourceLibrary.loads('{"profile":"empty","profile":"egal"}')

    def test_library_extension_keeps_prefix_instance_and_checks_later_use(self):
        prefix = '''
        (PARAM "p" "p" 0 (PROP) 1)
        (AXIOM "given" "given" 0 (TMH "p") 2)
        '''
        suffix = '''
        (THM "copy" "copy" "copy-proof" 0 (TMH "p") 3)
        (PROOF "copy" (KNOWN "given")) (QED)
        '''
        earlier = native.SourceLibrary.select(prefix, "empty", [], ["given"], [])
        later = native.SourceLibrary.select(prefix + suffix, "empty", ["copy"], ["given"], [])
        first, old, _ = native.SourceLibrary.loads(earlier.dumps()).replay(self.cetta)
        second, new, _ = native.SourceLibrary.loads(later.dumps()).replay(self.cetta)
        self.assertEqual(old[0].name, new[0].name)
        self.assertEqual(first.commands, second.commands[:len(first.commands)])
        result = self.run_program(second.render() + native.dependent_use(new[1]))
        self.assertIn("[(ImportedNativeUse 2 True True 1)]", result)
        reordered = native.SourceLibrary.select(suffix + prefix, "empty", ["copy"], [], [])
        with self.assertRaises((ValueError, SystemExit)):
            reordered.replay(self.cetta)


def qualify_family_library(cetta, megalodon, preamble, development):
    """Replay the actual complete prefix, then use every family theorem."""
    from collections import Counter
    exported = source.export_document(megalodon, development, [preamble])
    library = native.SourceLibrary.loads(
        native.SourceLibrary.select(exported, "egal", [], [], []).dumps())
    items, requests = library.resolve()
    test = NativeProjectionTests()
    test.cetta = cetta
    test.assertEqual({items[i].label for i, _ in requests}, {
        "family_seed_in_universe", "family_base_in_universe", "family_image_in_universe",
        "family_fibre_in_universe", "family_universe_transitive", "family_universe_zf_closed",
        "family_universe_contract", "family_universe_minimal", "family_universe_not_self_member"})
    projection, instances, prefix = library.replay(cetta)
    test.assertEqual(prefix, len(items))
    for instance in instances:
        if instance.declaration.kind != "THM":
            continue
        _prop, _proof, articles = projection.proof(
            instance.declaration.proof, instance.position, instance.arguments)
        test.assertTrue(projection.proof_steps_match(instance.name, articles),
                        instance.declaration.label)
    consumer = (Path(__file__).resolve().parents[1] /
                "tests/support/megalodon/family_library_consumer.metta").read_text()
    output = test.run_program(projection.render() + """
!(match &self (MegalodonHostedProofStepsV1 $name THM $label $articles)
   (HostedProofSteps $label))
""" + "".join(
        native.dependent_use(i) + normalized_use(i) for i in instances) + consumer)
    theorem_instances = [i for i in instances if i.declaration.kind == "THM"]
    test.assertEqual(output.count("(HostedProofSteps "), len(theorem_instances))
    for instance in instances:
        test.assertIn(f"[(ImportedNativeUse {instance.position} True True ", output)
        test.assertIn(f"[(ImportedNativeNormal {instance.position} True True True True ", output)
    test.assertIn("[(FamilyIdentityProgram True True True True True 4)]", output)
    test.assertIn("[(FamilyWrongResult Refuted)]", output)
    test.assertIn("[(FamilyWithdrawnAssumption Undetermined)]", output)
    print("FamilyLibraryQualified", dict(Counter(i.kind for i in items)),
          "admitted", prefix, "used-and-normalized", len(instances),
          "derived-program=1 wrong-conclusion-rejected=1 withdrawn-assumption-rejected=1")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--hotg-preamble", type=Path, required=True)
    parser.add_argument("--family-source", type=Path,
                        help="run the complete source family-library qualification instead of small fixtures")
    args = parser.parse_args()
    if args.family_source:
        return qualify_family_library(args.cetta, args.megalodon,
                                      args.hotg_preamble, args.family_source)
    NativeProjectionTests.cetta = args.cetta
    NativeProjectionTests.megalodon = args.megalodon
    NativeProjectionTests.hotg_preamble = args.hotg_preamble
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(NativeProjectionTests))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
