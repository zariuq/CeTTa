#!/usr/bin/env python3
"""Compare the pinned stable PeTTa corpus; every disagreement fails the check."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

from check_petta_release_reference import ROOT, check_reference, digest

sys.path.insert(0, str(ROOT / 'scripts'))
from petta_corpus_manifest import (  # noqa: E402
    normalize_cetta_stdout, normalize_oracle_stdout, semantic_stdout,
)


def run(command, directory, timeout):
    process = subprocess.Popen(command, cwd=directory, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True,
                               start_new_session=True)
    expired = False
    try:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            expired = True
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
    finally:
        # A fixture's background descendants belong to this invocation too.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    return dict(returncode=process.returncode, timed_out=expired,
                stdout=stdout, stderr=stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--cetta', type=Path, default=ROOT / 'cetta')
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    contract = json.loads((ROOT / 'langdef/petta/reference.json').read_text())
    reference, binary = args.reference.resolve(), args.cetta.resolve()
    check_reference(reference, contract)
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    for relative, expected in contract['corpus_fixtures'].items():
        if digest(ROOT / relative) != expected:
            raise ValueError('corpus fixture changed: ' + relative)
    programs = sorted(p for p in contract['reference_files']
                      if p.endswith('.metta') and
                      ((p.startswith('examples/') and p.count('/') == 1)
                       or p.startswith('tests/regression/')))
    result = dict(reference=contract['reference_commit'],
                  binary_sha256=digest(binary), cases={},
                  excluded=contract['corpus_exclusions'])
    with tempfile.TemporaryDirectory(prefix='cetta-petta-corpus-') as directory:
        stage = Path(directory)
        for relative in contract['reference_files']:
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((reference / relative).read_bytes())
        # Keep the local package import local, and control the three clock reads.
        package = 'repos/test_metta_lib/test.metta'
        (stage / package).parent.mkdir(parents=True, exist_ok=True)
        (stage / package).write_bytes((ROOT / package).read_bytes())
        (stage / 'examples/test_datetime.metta').write_bytes(
            (ROOT / 'tests/petta/fixtures/test_datetime_fixed.metta').read_bytes())
        for relative in programs:
            if relative in result['excluded']:
                continue
            oracle = run(['sh', 'run.sh', relative, '--silent'], stage, args.timeout)
            native = run([str(binary), '--lang', 'petta', str(stage / relative)],
                         stage, args.timeout)
            expected = semantic_stdout(normalize_oracle_stdout(oracle['stdout'], stage))
            actual = semantic_stdout(normalize_cetta_stdout(native['stdout'], stage))
            matched = (oracle['returncode'] == native['returncode'] == 0
                       and not oracle['timed_out'] and not native['timed_out']
                       and '❌' not in oracle['stdout'] and '❌' not in native['stdout']
                       and expected == actual)
            result['cases'][relative] = dict(match=matched, reference=oracle,
                                             native=native)
            if relative == 'examples/test_datetime.metta':
                result['cases'][relative]['fixture'] = 'fixed-clock'
            print(('PASS: ' if matched else 'FAIL: ') + relative, flush=True)
    result['failures'] = [p for p, case in result['cases'].items() if not case['match']]
    result['binary_sha256_after'] = digest(binary)
    if result['binary_sha256_after'] != result['binary_sha256']:
        raise ValueError('binary changed during comparison')
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{len(result['cases'])} programs; {len(result['failures'])} disagreements")
    return bool(result['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
