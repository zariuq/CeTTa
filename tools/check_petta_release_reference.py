#!/usr/bin/env python3
"""Check the pinned corrected PeTTa reference and the current default contract.

This checks source identity and selected default observations. It does not
establish complete conformance; use check_petta_stable_corpus.py for that.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_reference(reference, contract):
    revision = subprocess.check_output(
        ['git', '-C', str(reference), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != contract['reference_commit']:
        raise ValueError('reference checkout is not the pinned corrected stable revision')
    for relative, expected in contract['reference_files'].items():
        if digest(reference / relative) != expected:
            raise ValueError('reference source changed: ' + relative)
    tracked = subprocess.check_output(
        ['git', '-C', str(reference), 'ls-files'], text=True).splitlines()
    members = {p for p in tracked if p.startswith(('src/', 'lib/', 'examples/', 'tests/'))
               or p in ('run.sh', 'run.py', 'test.sh')}
    if members != set(contract['reference_files']):
        raise ValueError('reference source membership changed')
    examples = {Path(p).name for p in members
                if p.startswith('examples/') and p.count('/') == 1
                and p.endswith('.metta')}
    expected = {Path(p).name for p in contract['reference_files']
                if p.startswith('examples/') and p.endswith('.metta')
                and p.count('/') == 1}
    if examples != expected:
        raise ValueError('reference example membership changed')
    return len(expected)


def check_libraries(contract):
    for name, upstream in contract['library_files'].items():
        path = ROOT / 'lib/petta' / name
        if name in contract['native_library_helpers']:
            if path.exists():
                raise ValueError('native library helper also has a source copy: ' + name)
            if not (ROOT / contract['native_library_helpers'][name]).is_file():
                raise ValueError('native library helper is missing: ' + name)
            continue
        expected = contract['library_adaptations'].get(name, upstream)
        if digest(path) != expected:
            raise ValueError('default library changed: ' + name)


def check_default(binary):
    source = ROOT / 'tests/petta/language_versions/default_contract.metta'
    expected = ('is (1 2), should (1 2). ✅ \n'
                'is (3 4), should (3 4). ✅ \n'
                'is (), should (). ✅ \n'
                'is (7 7), should (7 7). ✅ \n'
                'true\ntrue\ntrue\ntrue\n')
    for language in ('petta', 'petta@stable-fixed'):
        run = subprocess.run([str(binary), '--lang', language, str(source)],
                             capture_output=True, text=True, timeout=30)
        if (run.returncode, run.stdout, run.stderr) != (0, expected, ''):
            raise ValueError(('default contract changed', language,
                              run.returncode, run.stdout, run.stderr))
    # The reference's two-argument lambda map is adapted to the native binder map.
    # Pin its positive, empty, impossible and duplicate-occurrence observations.
    source = ROOT / 'tests/petta/language_versions/combinatorics_contract.metta'
    expected = source.with_suffix('.expected').read_text()
    for language in ('petta', 'petta@stable-fixed'):
        run = subprocess.run([str(binary), '--lang', language, str(source)],
                             capture_output=True, text=True, timeout=30)
        if (run.returncode, run.stdout, run.stderr) != (0, expected, ''):
            raise ValueError(('combinatorics adaptation changed', language,
                              run.returncode, run.stdout, run.stderr))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--cetta', type=Path)
    args = parser.parse_args()
    contract = json.loads((ROOT / 'langdef/petta/reference.json').read_text())
    if contract['schema'] != 'cetta-petta-reference-v2':
        raise ValueError('unknown reference schema')
    if contract['default_language_version'] != 'stable-fixed':
        raise ValueError('unexpected default language version')
    check_libraries(contract)
    if args.reference:
        count = check_reference(args.reference.resolve(), contract)
        if count != contract['example_count']:
            raise ValueError('reference example count changed')
    if args.cetta:
        check_default(args.cetta.resolve())
    print('PASS: corrected stable PeTTa source, libraries and selected default observations are pinned')


if __name__ == '__main__':
    main()
