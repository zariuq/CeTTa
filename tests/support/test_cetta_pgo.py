"""PGO workload separation and failed/changed training are enforced."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from cetta_pgo_train import workloads


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'runtime/pgo-controls')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='training-', dir=args.output.resolve()))
    manifest = ROOT / 'tools/cetta_pgo_workloads.json'
    document = workloads(manifest, ROOT)
    assert any(row['language'] == 'prime' and row['repeat'] > 1 for row in document['training'])
    duplicate = dict(document, held_out=[document['training'][0]])
    (work / 'duplicate.json').write_text(json.dumps(duplicate))
    try:
        workloads(work / 'duplicate.json', ROOT)
    except ValueError:
        pass
    else:
        raise AssertionError('training/measurement overlap accepted')
    for name, code in [('good', 'print("answer")'), ('changed', 'print("wrong")'), ('failed', 'raise SystemExit(7)')]:
        path = work / name
        path.write_text('#!/usr/bin/env python3\n' + code + '\n'); path.chmod(0o755)
    for binary, success in [('good', True), ('changed', False), ('failed', False)]:
        result = subprocess.run([sys.executable, str(ROOT / 'tools/cetta_pgo_train.py'), '--manifest', str(manifest),
                                 '--root', str(ROOT), '--binary', str(work / binary), '--reference', str(work / 'good'),
                                 '--output', str(work / (binary + '.json'))], capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stderr
    assert not (work / 'changed.json').exists() and not (work / 'failed.json').exists()
    for binary, success in [('good', True), ('changed', False), ('failed', False)]:
        result = subprocess.run([sys.executable, str(ROOT / 'tools/cetta_pgo_measure.py'), '--manifest', str(manifest),
                                 '--root', str(ROOT), '--binary', 'base=' + str(work / 'good'),
                                 '--binary', 'candidate=' + str(work / binary), '--wall-only', '--repeats', '1',
                                 '--output', str(work / ('measure-' + binary + '.json'))], capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stderr
    print('PASS: disjoint Prime training/measurement sets; changed and failed executions cannot qualify PGO')


if __name__ == '__main__':
    main()
