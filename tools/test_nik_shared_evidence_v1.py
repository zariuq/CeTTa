"""Exact-shape, eviction and proof-DAG controls for evidence sharing."""

import gc
import unittest

import gslt2parse_schema_v1 as sx
import nik_proof_dag_v1 as dag
from nik_shared_evidence_v1 import EvidenceBuilder


def reference_list(nil, cons, values):
    result = sx.Symbol(nil)
    for value in reversed(values):
        result = (sx.Symbol(cons), value, result)
    return result


def reference_app(head, *arguments):
    return (sx.Symbol("PApp"), sx.StringLiteral(head),
            reference_list("LNil", "LCons", arguments))


def reference_proof(name, arguments, children):
    return (sx.Symbol("GProof"),
            (sx.Symbol("GRuleInst"), sx.StringLiteral(name),
             reference_list("LNil", "LCons", arguments)),
            reference_list("PrNil", "PrCons", children))


class SharedEvidenceTests(unittest.TestCase):
    def test_exact_shape_and_dag_chronology_at_every_cache_size(self):
        for capacity in (0, 1, 3, 64):
            with self.subTest(capacity=capacity):
                builder = EvidenceBuilder(capacity)
                a, b = builder.app("A"), builder.app("B")
                ra, rb = reference_app("A"), reference_app("B")
                left, right = builder.proof_node("leaf", [a], []), builder.proof_node("leaf", [b], [])
                rleft, rright = reference_proof("leaf", [ra], []), reference_proof("leaf", [rb], [])
                proof = builder.proof_node("root", [builder.app("Pair", a, b)], [left, right, left])
                reference = reference_proof("root", [reference_app("Pair", ra, rb)], [rleft, rright, rleft])
                self.assertEqual(proof, reference)
                self.assertEqual(sx.render(proof), sx.render(reference))
                self.assertEqual(dag.compile_shared_article(a, proof),
                                 dag.compile_shared_article(ra, reference))
                self.assertLessEqual(len(builder.nodes), capacity)

    def test_repeated_nodes_share_without_erasing_premise_occurrences(self):
        builder = EvidenceBuilder(128)
        value = builder.app("Value", builder.nat(7))
        self.assertIs(value, builder.app("Value", builder.nat(7)))
        leaf = builder.proof_node("leaf", [value], [])
        self.assertIs(leaf, builder.proof_node("leaf", [value], []))
        single = builder.proof_node("root", [], [leaf])
        repeated = builder.proof_node("root", [], [leaf, leaf])
        self.assertNotEqual(single, repeated)
        compiled = dag.compile_shared_article(value, repeated)
        self.assertEqual((compiled.raw_proof_nodes, compiled.unique_proof_nodes), (3, 2))

    def test_order_names_and_arguments_remain_distinct(self):
        builder = EvidenceBuilder()
        a, b = builder.app("A"), builder.app("B")
        left, right = builder.proof_node("leaf", [a], []), builder.proof_node("leaf", [b], [])
        self.assertNotEqual(builder.proof_node("root", [], [left, right]),
                            builder.proof_node("root", [], [right, left]))
        self.assertNotEqual(builder.proof_node("r", [a], []), builder.proof_node("s", [a], []))
        self.assertNotEqual(builder.app("Pair", a, b), builder.app("Pair", b, a))

    def test_eviction_and_collection_do_not_reinterpret_old_nodes(self):
        builder = EvidenceBuilder(3)
        retained = []
        for index in range(1000):
            value = builder.app("Payload", (sx.Symbol("External"), index))
            self.assertEqual(value, reference_app("Payload", (sx.Symbol("External"), index)))
            if index % 100 == 0:
                retained.append((index, value))
                gc.collect()
            self.assertLessEqual(len(builder.nodes), 3)
        for index, value in retained:
            self.assertEqual(value, reference_app("Payload", (sx.Symbol("External"), index)))

    def test_invalid_arguments_rejected(self):
        with self.assertRaises(ValueError):
            EvidenceBuilder(-1)
        with self.assertRaises(ValueError):
            EvidenceBuilder().nat(-1)
        with self.assertRaises(TypeError):
            EvidenceBuilder().app("Mutable", [])


if __name__ == "__main__":
    unittest.main()
