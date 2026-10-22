#!/usr/bin/env python3
"""The application panel must preserve semantic and metric failures."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

panel = load('application_panel', ROOT / 'benchmarks/mam_families/application_panel.py')
panel.cost = load('application_cost', ROOT / 'benchmarks/mam_families/cost_panel.py')
panel.corpus = load('application_corpus', ROOT / 'scripts/petta_corpus_manifest.py')

def samples(candidate=10):
    return {label: {i: dict(native_wall=(10 if label == 'baseline' else candidate),
                           perf_wall=(10.0 if label == 'baseline' else float(candidate)),
                           rss_kib=(10 if label == 'baseline' else candidate),
                           rc=0, timed_out=False, correct=True,
                           observation_sha256='a' * 64)
                   for i in range(5)} for label in panel.cost.CALIBRATED}

class PanelContract(unittest.TestCase):
    def test_chainer_profiles_are_explicit_manifest_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            chainer = Path(directory) / 'chainer'
            (chainer / 'examples').mkdir(parents=True)
            (chainer / 'examples/smokes.metta').write_text('')
            locations = {'chainer': chainer, 'petta': Path(directory) / 'petta'}
            selected = ['chainer-smokes']
            plain = panel.cases(locations, selected, 'plain', 'default')[0]
            self.assertEqual(plain['cetta_args'], ['--lang', 'petta'])
            self.assertEqual(plain['oracle_args'], [])
            self.assertEqual(plain['invocation_policy'],
                             {'cetta_profile': 'plain', 'oracle_profile': 'default'})

            extended = panel.cases(
                locations, selected, 'extended', 'strict-det')[0]
            self.assertEqual(extended['cetta_args'],
                             ['--lang', 'petta', '--profile', 'extended'])
            self.assertEqual(extended['oracle_args'], ['--strict-det'])

    def test_wall_and_memory_are_independently_gated(self):
        rows = samples(9)
        self.assertEqual(panel.metric_gate(rows, 'native_wall')['status'], 'improvement')
        self.assertEqual(panel.metric_gate(rows, 'perf_wall')['status'], 'improvement')
        for label in ('candidate', 'control_a', 'control_b'):
            for row in rows[label].values(): row['rss_kib'] = 11
        self.assertEqual(panel.metric_gate(rows, 'rss_kib')['status'], 'regression')

    def test_failed_missing_or_unresolved_measurement_is_not_a_gain(self):
        for change in [dict(rc=1), dict(timed_out=True), dict(correct=False),
                       dict(rss_kib=None), dict(rss_kib=0), dict(observation_sha256='b' * 64)]:
            rows = samples(1)
            rows['candidate'][2].update(change)
            self.assertFalse(panel.metric_gate(rows, 'rss_kib')['accepted'])
        rows = samples(1)
        del rows['control_a'][0]
        self.assertFalse(panel.metric_gate(rows, 'native_wall')['accepted'])

    def test_measurement_medians_keep_all_available_cost_axes(self):
        rows = [dict(perf_wall=0.3, instructions=30, rss_kib=300),
                dict(perf_wall=0.1, instructions=10, rss_kib=100),
                dict(perf_wall=0.2, instructions=20, rss_kib=200)]
        self.assertEqual(panel.measurement_medians(rows),
                         {'perf_wall': 0.2, 'instructions': 20,
                          'rss_kib': 200})
        rows[0]['perf_wall'] = None
        self.assertNotIn('perf_wall', panel.measurement_medians(rows))

    def test_oracle_comparison_does_not_invent_missing_axes(self):
        compared = panel.compare_candidate_to_oracle(
            {'perf_wall': 2.0, 'instructions': 10},
            {'perf_wall': 1.0, 'instructions': 20, 'rss_kib': 30})
        self.assertFalse(compared['perf_wall']['candidate_faster'])
        self.assertTrue(compared['instructions']['candidate_faster'])
        self.assertNotIn('rss_kib', compared)

    def test_engine_environments_are_explicit_and_cetta_scoped(self):
        parsed = panel.engine_environments([
            'baseline:CETTA_PETTA_NATIVE_PRIORITY_HEAP=0',
            'candidate:CETTA_PETTA_NATIVE_PRIORITY_HEAP=1'])
        self.assertEqual(parsed['baseline'],
                         {'CETTA_PETTA_NATIVE_PRIORITY_HEAP': '0'})
        self.assertEqual(parsed['candidate'],
                         {'CETTA_PETTA_NATIVE_PRIORITY_HEAP': '1'})
        self.assertEqual(parsed['control_a'], {})
        with self.assertRaises(ValueError):
            panel.engine_environments(['candidate:PATH=/elsewhere'])
        with self.assertRaises(ValueError):
            panel.engine_environments(['unknown:CETTA_X=1'])

    def test_proof_content_order_and_multiplicity_are_observed(self):
        roots = {'source': ROOT, 'library': ROOT / 'lib'}
        norm = lambda text: panel.normalized(text, 'cetta', roots)
        original = '(proof a)\n(proof b)\n'
        self.assertEqual(norm(original), original)
        for changed in ['(proof b)\n(proof a)\n', '(proof a)\n',
                        '(proof a)\n(proof b)\n(proof b)\n',
                        '(proof a)\n(proof c)\n']:
            self.assertNotEqual(norm(original), norm(changed))
        self.assertNotEqual(norm('(Error call reason-a)\n'), norm('(Error call reason-b)\n'))

if __name__ == '__main__': unittest.main()
