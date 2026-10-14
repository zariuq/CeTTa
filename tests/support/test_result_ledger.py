#!/usr/bin/env python3
"""A ledger must not hide failed work or reuse another binary's qualification."""
import copy
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('result_ledger',
    ROOT / 'benchmarks/mam_families/result_ledger.py')
ledger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ledger)


def record():
    return dict(schema='cetta-regional-result-ledger-v1',
        artifacts={'old': dict(kind='binary', sha256='a'*64),
                   'new': dict(kind='binary', sha256='b'*64)},
        evidence={'checks': dict(file='checks.json', sha256='c'*64)},
        qualifications=[dict(artifact='old', scope='full gate', status='passed', evidence=['checks']),
                        dict(artifact='new', scope='full gate', status='pending', evidence=[])],
        results=[dict(id='duplicate-rows', task='two equal rows', observer='ordered_occurrences',
            phase='query', quality='checked_samples', evidence=['checks'], comparison='matched',
            variants=[dict(name='baseline', artifact='old', correct='passed', trials=5,
                           contract_sha256='d'*64, metrics=dict(wall_s=2, peak_rss_mib=10)),
                      dict(name='candidate', artifact='new', correct='passed', trials=5,
                           contract_sha256='d'*64, metrics=dict(wall_s=1, peak_rss_mib=12))])],
        findings=[dict(priority=1, title='memory growth', status='open', evidence=['checks'],
                       next='retain and release real replies', controls='cancel and resume')])


class LedgerContract(unittest.TestCase):
    def test_time_and_memory_remain_separate(self):
        data = record()
        ledger.validate(data)
        self.assertEqual(ledger.ratios(data['results'][0], 'wall_s'), {'candidate': .5})
        self.assertEqual(ledger.ratios(data['results'][0], 'peak_rss_mib'), {'candidate': 1.2})

    def test_relinked_artifact_does_not_inherit_full_gate(self):
        data = record()
        ledger.validate(data)
        text = ledger.render(data)
        self.assertIn('passed  old: full gate', text)
        self.assertIn('pending new: full gate', text)

    def test_failed_missing_and_nonfinite_measurements(self):
        for change in [dict(correct='failed'), dict(correct='pending'), dict(trials=0),
                       dict(metrics={'wall_s': float('nan')}), dict(metrics={'wall_s': -1}),
                       dict(metrics={'wall_s': True}), dict(artifact='unknown')]:
            data = record()
            data['results'][0]['variants'][1].update(change)
            with self.assertRaises(ValueError): ledger.validate(data)

    def test_different_observation_contract_is_not_a_matched_comparison(self):
        data = record()
        data['results'][0]['variants'][1]['contract_sha256'] = 'e'*64
        with self.assertRaises(ValueError): ledger.validate(data)
        data['results'][0]['comparison'] = 'unresolved'
        ledger.validate(data)
        self.assertEqual(ledger.ratios(data['results'][0], 'wall_s'), {})

    def test_unknown_measurement_is_not_zero(self):
        data = record()
        data['results'][0]['variants'][1]['metrics']['wall_s'] = None
        ledger.validate(data)
        self.assertEqual(ledger.ratios(data['results'][0], 'wall_s'), {})

    def test_evidence_identity_and_bundle_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'checks.json').write_bytes(b'{"checks":2}')
            data = record()
            data['evidence']['checks']['sha256'] = hashlib.sha256(b'{"checks":2}').hexdigest()
            ledger.validate(data, root)
            (root/'checks.json').write_bytes(b'{"checks":1}')
            with self.assertRaises(ValueError): ledger.validate(data, root)
        for path in ('/checks.json', '../checks.json'):
            data = record()
            data['evidence']['checks']['file'] = path
            with self.assertRaises(ValueError): ledger.validate(data)

    def test_duplicate_workloads_and_missing_evidence(self):
        data = record()
        data['results'].append(copy.deepcopy(data['results'][0]))
        with self.assertRaises(ValueError): ledger.validate(data)
        data = record()
        data['qualifications'][0]['evidence'] = []
        with self.assertRaises(ValueError): ledger.validate(data)


if __name__ == '__main__': unittest.main()
