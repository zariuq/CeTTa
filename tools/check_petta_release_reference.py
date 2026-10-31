#!/usr/bin/env python3
"""Check the pinned corrected PeTTa reference and the current default contract.

This checks source identity and selected default observations. It does not
establish complete conformance. check_petta_stable_corpus.py records broader
diagnostics; it is not a specification oracle either.

A reference may be a checkout or a source snapshot. Both must have exactly
the pinned executable source files and hashes. A checkout also needs the
pinned Git revision; a snapshot does not inherit an enclosing repository.
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
    reference = reference.resolve()
    repository = subprocess.run(
        ['git', '-C', str(reference), 'rev-parse', '--show-toplevel'],
        capture_output=True, text=True)
    checkout = (repository.returncode == 0 and
                Path(repository.stdout.strip()).resolve() == reference)
    revision = None
    if checkout:
        revision = subprocess.check_output(
            ['git', '-C', str(reference), 'rev-parse', 'HEAD'], text=True).strip()
        if revision != contract['reference_commit']:
            raise ValueError('reference checkout is not the pinned corrected stable revision')
    actual = {}
    for relative, expected in contract['reference_files'].items():
        actual[relative] = digest(reference / relative)
        if actual[relative] != expected:
            raise ValueError('reference source changed: ' + relative)
    members = {p.relative_to(reference).as_posix()
               for name in ('src', 'lib', 'examples', 'tests')
               for p in (reference / name).rglob('*') if p.is_file()}
    members.update(name for name in ('run.sh', 'run.py', 'test.sh')
                   if (reference / name).is_file())
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
    return {
        'schema': 'cetta.petta-reference-identity.v1',
        'reference_commit': contract['reference_commit'],
        'source_kind': 'checkout' if checkout else 'snapshot',
        'checkout_commit': revision,
        'source_manifest_sha256': hashlib.sha256(
            json.dumps(actual, sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
        'files': len(actual),
        'examples': len(expected),
    }


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
    provenance = (ROOT / 'lib/petta/vendored_from_petta.tsv').read_text().splitlines()
    actual = [line.split('\t') for line in provenance
              if line and not line.startswith('#')]
    expected = [['file', 'source_sha256', 'petta_commit']] + [
        [name, sha[:16], contract['reference_commit'][:7]]
        for name, sha in sorted(contract['library_files'].items())]
    if actual != expected:
        raise ValueError('library provenance does not match the current reference')


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
    parser.add_argument('--identity-json', action='store_true',
                        help='print the validated reference identity as JSON')
    args = parser.parse_args()
    if args.identity_json and args.reference is None:
        parser.error('--identity-json requires --reference')
    contract = json.loads((ROOT / 'langdef/petta/reference.json').read_text())
    if contract['schema'] != 'cetta-petta-reference-v2':
        raise ValueError('unknown reference schema')
    if contract['default_language_version'] != 'stable-fixed':
        raise ValueError('unexpected default language version')
    qualification = contract['qualification_contract']
    observations = ROOT / qualification['observations']
    if digest(observations) != qualification['observations_sha256']:
        raise ValueError('observation contract changed without updating its identity')
    ledger = json.loads(observations.read_text())
    if ledger['reference_commit'] != contract['reference_commit']:
        raise ValueError('observation contract names another reference')
    for observation in ledger['observations']:
        if observation['source_sha256'] != contract['reference_files'][observation['source']]:
            raise ValueError('observation source identity changed: ' + observation['id'])
        if 'contract_probe' in observation and not (ROOT / observation['contract_probe']).is_file():
            raise ValueError('missing contract probe: ' + observation['id'])
    check_libraries(contract)
    identity = None
    if args.reference:
        identity = check_reference(args.reference.resolve(), contract)
        if identity['examples'] != contract['example_count']:
            raise ValueError('reference example count changed')
    if args.cetta:
        check_default(args.cetta.resolve())
    if args.identity_json:
        print(json.dumps(identity, sort_keys=True))
        return
    print('PASS: corrected stable PeTTa source, libraries and selected default observations are pinned')


if __name__ == '__main__':
    main()
