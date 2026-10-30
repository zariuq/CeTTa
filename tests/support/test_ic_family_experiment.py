#!/usr/bin/env python3
"""Partition leakage and whole-source clone controls."""

from copy import deepcopy
import unittest

from check_ic_family_experiment import CORPORA, canonical_source, validate_manifest


class FamilyPartitionTests(unittest.TestCase):
    def setUp(self):
        self.manifest = {'version': 1, 'training': [], 'evaluation': []}
        self.sources = {}
        for partition, offset in (('training', 0), ('evaluation', 10)):
            for index, corpus in enumerate(sorted(CORPORA)):
                label = f'{partition}-{index}'
                path = label + '.metta'
                self.manifest[partition].append({'id': label, 'corpus': corpus,
                                                 'family': label, 'source': path})
                self.sources[path] = f'(= (predicate-{label} $x) (+ $x {index + offset}))'

    def test_all_three_disjoint_families(self):
        validate_manifest(self.manifest, self.sources)

    def test_variable_and_defined_head_renaming_is_not_a_new_program(self):
        first = '(= (first $x) (+ $x 1)) (= (second $y) (first $y))'
        second = '; comment\n(= (other $a) (+ $a 1)) (= (last $b) (other $b))'
        self.assertEqual(canonical_source(first), canonical_source(second))

    def test_changed_literal_is_not_a_renamed_clone(self):
        self.assertNotEqual(canonical_source('(= (f $x) (+ $x 1))'),
                            canonical_source('(= (g $y) (+ $y 2))'))

    def test_changed_operation_is_not_a_renamed_clone(self):
        self.assertNotEqual(canonical_source('(= (f $x) (+ $x 1))'),
                            canonical_source('(= (g $y) (* $y 1))'))

    def test_patterned_and_typed_definition_heads_are_renamed_together(self):
        first = '(: first (-> Atom Atom)) (= (first (A $x)) $x) (= second first)'
        second = '(: other (-> Atom Atom)) (= (other (A $y)) $y) (= last other)'
        self.assertEqual(canonical_source(first), canonical_source(second))

    def test_program_family_cannot_cross(self):
        self.manifest['evaluation'][0]['family'] = self.manifest['training'][0]['family']
        with self.assertRaisesRegex(ValueError, 'program family crosses'):
            validate_manifest(self.manifest, self.sources)

    def test_generated_variant_cannot_cross(self):
        self.manifest['training'][0]['variants'] = [self.manifest['evaluation'][0]['source']]
        with self.assertRaisesRegex(ValueError, 'generated variant crosses'):
            validate_manifest(self.manifest, self.sources)

    def test_renamed_clone_cannot_cross(self):
        source = self.manifest['evaluation'][0]['source']
        self.sources[source] = '(= (renamed $variable) (+ $variable 0))'
        with self.assertRaisesRegex(ValueError, 'renamed source clone crosses'):
            validate_manifest(self.manifest, self.sources)

    def test_missing_corpus_is_not_full_family_coverage(self):
        self.manifest['evaluation'].pop()
        with self.assertRaisesRegex(ValueError, 'all three corpora'):
            validate_manifest(self.manifest, self.sources)

    def test_duplicate_identity_is_refused(self):
        self.manifest['evaluation'][0]['id'] = self.manifest['training'][0]['id']
        with self.assertRaisesRegex(ValueError, 'duplicate source identity'):
            validate_manifest(self.manifest, self.sources)

    def test_unknown_corpus_is_refused(self):
        self.manifest['evaluation'][0]['corpus'] = 'Unknown'
        with self.assertRaisesRegex(ValueError, 'missing corpus'):
            validate_manifest(self.manifest, self.sources)

    def test_unknown_manifest_version_is_refused(self):
        self.manifest['version'] = 2
        with self.assertRaisesRegex(ValueError, 'unknown manifest version'):
            validate_manifest(self.manifest, self.sources)

    def test_empty_partition_is_refused(self):
        self.manifest['evaluation'] = []
        with self.assertRaisesRegex(ValueError, 'both partitions'):
            validate_manifest(self.manifest, self.sources)

    def test_validation_does_not_mutate_manifest(self):
        before = deepcopy(self.manifest)
        validate_manifest(self.manifest, self.sources)
        self.assertEqual(before, self.manifest)


if __name__ == '__main__':
    unittest.main()
