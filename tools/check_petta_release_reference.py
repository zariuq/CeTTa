#!/usr/bin/env python3
"""Check CeTTa's record of its last stable-fixed alignment and the default contract.

CeTTa `--lang petta` follows PeTTa's stable-fixed branch as it is.
langdef/petta/reference.json records the commit CeTTa last aligned with, the
date and the SWI-Prolog that ran it; it is a record, never a pin.  A checkout
is read as it stands: where it differs from the record, the differences are
listed as alignment work, and only CeTTa's own files can fail this check (its
vendored libraries against their recorded sources, the observation ledger's
probes, the default contract).

This checks source provenance and selected default observations.  It does not
establish conformance.  check_petta_stable_corpus.py records broader
diagnostics; it is not a specification oracle either.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
COMMIT = re.compile(r'[0-9a-f]{40}')
DATE = re.compile(r'\d{4}-\d{2}-\d{2}')
SOURCE_DIRECTORIES = ('src', 'lib', 'examples', 'tests')
SOURCE_FILES = ('run.sh', 'run.py', 'test.sh')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(reference, *arguments):
    run = subprocess.run(['git', '-C', str(reference), *arguments],
                         capture_output=True, text=True)
    return run.stdout.strip() if run.returncode == 0 else None


def check_record(contract):
    if contract['schema'] != 'cetta-petta-reference-v3':
        raise ValueError('unknown reference schema')
    if contract['default_language_version'] != 'stable-fixed':
        raise ValueError('unexpected default language version')
    record = contract['last_alignment']
    if not (COMMIT.fullmatch(record['commit']) and DATE.fullmatch(record['date'])
            and record['swi_prolog'].strip()):
        raise ValueError('malformed last alignment record')
    return record


def source_files(reference):
    members = {p.relative_to(reference).as_posix()
               for name in SOURCE_DIRECTORIES
               for p in (reference / name).rglob('*') if p.is_file()}
    members.update(name for name in SOURCE_FILES
                   if (reference / name).is_file())
    return members


def check_reference(reference, contract):
    """The checkout or snapshot as it is, and the alignment work it shows
    against the record: never a failure."""
    reference = reference.resolve()
    record = contract['last_alignment']
    toplevel = git(reference, 'rev-parse', '--show-toplevel')
    checkout = toplevel is not None and Path(toplevel).resolve() == reference
    head = git(reference, 'rev-parse', 'HEAD') if checkout else None
    members = source_files(reference)
    examples = sorted(Path(p).name for p in members
                      if p.startswith('examples/') and p.count('/') == 1
                      and p.endswith('.metta'))
    work = []
    commits_since = None
    changed = None
    if not checkout:
        work.append('a snapshot names no commit: its alignment with '
                    + record['commit'][:7] + ' is unknown')
    elif head != record['commit']:
        if git(reference, 'cat-file', '-e', record['commit'] + '^{commit}') is None:
            work.append('the recorded alignment ' + record['commit'][:7]
                        + ' is not in this checkout')
        else:
            base = git(reference, 'merge-base', record['commit'], head)
            if base == record['commit']:
                commits_since = int(git(reference, 'rev-list', '--count',
                                        record['commit'] + '..' + head))
                work.append(f'{commits_since} stable-fixed commits since the last '
                            f'alignment {record["commit"][:7]}')
            else:
                work.append('the checkout ' + head[:7] + ' does not descend from '
                            'the last alignment ' + record['commit'][:7])
            listing = git(reference, 'diff', '--name-status', record['commit'], head,
                          '--', *SOURCE_DIRECTORIES, *SOURCE_FILES)
            changed = [line.split('\t', 1) for line in (listing or '').splitlines()]
            if changed:
                work.append(f'{len(changed)} source files differ from the last alignment')
    for name, entry in sorted(contract['library_files'].items()):
        path = reference / 'lib' / name
        if not path.is_file():
            work.append('library ' + name + ' is gone from the checkout')
        elif digest(path) != entry['sha256']:
            work.append('library ' + name + ' changed since CeTTa took it from '
                        + entry['commit'][:7])
    ledger = json.loads((ROOT / contract['qualification_contract']['observations'])
                        .read_text())
    for observation in ledger['observations']:
        path = reference / observation['source']
        if not path.is_file():
            work.append('observation ' + observation['id'] + ': '
                        + observation['source'] + ' is gone from the checkout')
        elif digest(path) != observation['source_sha256']:
            work.append('observation ' + observation['id'] + ': '
                        + observation['source'] + ' changed since its classification')
    for excluded in sorted(contract['corpus_exclusions']):
        if excluded not in members:
            work.append('corpus exclusion ' + excluded + ' is gone from the checkout')
    return {
        'schema': 'cetta.petta-reference-identity.v2',
        'last_alignment': record,
        'source_kind': 'checkout' if checkout else 'snapshot',
        'checkout_commit': head,
        'aligned': checkout and head == record['commit'],
        'commits_since_alignment': commits_since,
        'changed_since_alignment': changed,
        'source_manifest_sha256': hashlib.sha256(json.dumps(
            {p: digest(reference / p) for p in sorted(members)},
            sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
        'files': len(members),
        'examples': len(examples),
        'alignment_work': work,
    }


def check_ledger(contract):
    ledger = json.loads((ROOT / contract['qualification_contract']['observations'])
                        .read_text())
    if ledger['schema'] != 'petta-observation-classification-v2':
        raise ValueError('unknown observation ledger schema')
    if not COMMIT.fullmatch(ledger['classified_at']):
        raise ValueError('observation ledger names no classification commit')
    for observation in ledger['observations']:
        if not re.fullmatch(r'[0-9a-f]{64}', observation['source_sha256']):
            raise ValueError('observation source has no identity: ' + observation['id'])
        if 'contract_probe' in observation and not (ROOT / observation['contract_probe']).is_file():
            raise ValueError('missing contract probe: ' + observation['id'])


def check_libraries(contract):
    for name, entry in contract['library_files'].items():
        if not (COMMIT.fullmatch(entry['commit'])
                and re.fullmatch(r'[0-9a-f]{64}', entry['sha256'])):
            raise ValueError('library source has no identity: ' + name)
        path = ROOT / 'lib/petta' / name
        if name in contract['native_library_helpers']:
            if path.exists():
                raise ValueError('native library helper also has a source copy: ' + name)
            if not (ROOT / contract['native_library_helpers'][name]).is_file():
                raise ValueError('native library helper is missing: ' + name)
            continue
        expected = contract['library_adaptations'].get(name, entry['sha256'])
        if digest(path) != expected:
            raise ValueError('default library changed: ' + name)
    provenance = (ROOT / 'lib/petta/vendored_from_petta.tsv').read_text().splitlines()
    actual = [line.split('\t') for line in provenance
              if line and not line.startswith('#')]
    expected = [['file', 'source_sha256', 'petta_commit']] + [
        [name, entry['sha256'][:16], entry['commit'][:7]]
        for name, entry in sorted(contract['library_files'].items())]
    if actual != expected:
        raise ValueError('library provenance does not match the recorded sources')


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
    # Check its positive, empty, impossible and duplicate-occurrence observations.
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
                        help='print the checkout identity and its alignment work as JSON')
    args = parser.parse_args()
    if args.identity_json and args.reference is None:
        parser.error('--identity-json requires --reference')
    contract = json.loads((ROOT / 'langdef/petta/reference.json').read_text())
    record = check_record(contract)
    check_ledger(contract)
    check_libraries(contract)
    identity = None
    if args.reference:
        identity = check_reference(args.reference.resolve(), contract)
    if args.cetta:
        check_default(args.cetta.resolve())
    if args.identity_json:
        print(json.dumps(identity, sort_keys=True))
        return
    print(f'PASS: CeTTa\'s stable-fixed record (last aligned with {record["commit"][:7]} '
          f'on {record["date"]}, {record["swi_prolog"]}), its libraries and selected '
          'default observations are consistent')
    if identity is not None:
        if identity['aligned']:
            print('ALIGNED: the checkout is the last alignment')
        for item in identity['alignment_work']:
            print('ALIGNMENT WORK: ' + item)


if __name__ == '__main__':
    main()
