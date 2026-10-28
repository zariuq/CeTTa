#!/usr/bin/env python3
"""Record pinned corpus diagnostics with independently framed query observations.

Raw agreement is not conformance. Settled requirements and approved differences
are qualified by the semantic regression targets; original corpus assertions
remain diagnostic evidence, including unresolved/reference-specific assertions.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import uuid

from check_petta_release_reference import ROOT, check_reference, digest

sys.path.insert(0, str(ROOT / 'scripts'))
from petta_corpus_manifest import (  # noqa: E402
    normalize_oracle_stdout, normalize_cetta_stdout, semantic_stdout,
)

sys.path.insert(0, str(ROOT / 'tests/support'))
from compare_answers import framed, observe_framed, compare_completed_runs, representation_queries  # noqa: E402


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
                  binary_sha256=digest(binary), cases={}, controls={},
                  scope='diagnostic observations, not a conformance score',
                  excluded=contract['corpus_exclusions'])
    with tempfile.TemporaryDirectory(prefix='cetta-petta-corpus-') as directory:
        stage = Path(directory)
        for relative in contract['reference_files']:
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((reference / relative).read_bytes())
        package = 'repos/test_metta_lib/test.metta'
        (stage / package).parent.mkdir(parents=True, exist_ok=True)
        (stage / package).write_bytes((ROOT / package).read_bytes())

        def observe(relative, source):
            oracle_cmd = ['sh', 'run.sh', str(source), '--silent']
            native_cmd = [str(binary), '--lang', 'petta', str(source)]
            original = dict(reference=run(oracle_cmd, stage, args.timeout),
                            native=run(native_cmd, stage, args.timeout))
            marker = '(__cetta_answers_' + uuid.uuid4().hex + '__)'
            text, kinds = framed(source.read_text(), marker)
            representations = representation_queries(source.read_text())
            instrumented = source.with_name('.framed-' + source.name)
            instrumented.write_text(text)
            observations = {}
            for name, command in [('reference', ['sh', 'run.sh', str(instrumented), '--silent']),
                                  ('native', [str(binary), '--lang', 'petta', str(instrumented)])]:
                record = run(command, stage, args.timeout)
                normalize = normalize_oracle_stdout if name == 'reference' else normalize_cetta_stdout
                cleaned = semantic_stdout(normalize(record['stdout'], stage))
                try:
                    record['observations'] = observe_framed(cleaned, kinds, marker)
                except ValueError as error:
                    record['framing_error'] = str(error)
                record['completed'] = (record['returncode'] == 0 and not record['timed_out']
                                       and 'observations' in record)
                observations[name] = record
            instrumented.unlink()
            a, b = observations['reference'], observations['native']
            agreement, reason = compare_completed_runs(kinds, a, b, representations)
            # Stdout does not erase a fault, diagnostic or unfinished original.
            for name in original:
                if original[name]['returncode'] != 0 or original[name]['timed_out']:
                    agreement = False
                    reason = 'original execution incomplete or raised'
            if a['stderr'] != b['stderr']:
                agreement = False
                reason = 'stderr observations differ'
            return dict(original=original, framed=observations, agreement=agreement,
                        reason=reason, query_count=len(kinds), source_sha256=digest(source))

        for relative in programs:
            if relative in result['excluded']:
                continue
            case = observe(relative, stage / relative)
            result['cases'][relative] = case
            print(('AGREEMENT: ' if case['agreement'] else 'DIAGNOSTIC: ') + relative,
                  flush=True)
        # The clock-controlled programme is additional evidence. Never replace
        # the corpus's original source or attribute the control's result to it.
        relative = 'examples/qualification_datetime_fixed.metta'
        control = stage / relative
        control.write_bytes((ROOT / 'tests/petta/fixtures/test_datetime_fixed.metta').read_bytes())
        result['controls']['fixed_clock'] = observe(relative, control)
    result['diagnostic_differences'] = [p for p, case in result['cases'].items()
                                        if not case['agreement']]
    result['binary_sha256_after'] = digest(binary)
    if result['binary_sha256_after'] != result['binary_sha256']:
        raise ValueError('binary changed during comparison')
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{len(result['cases'])} original programs recorded; "
          f"{len(result['diagnostic_differences'])} require observation-level classification. "
          "No conformance percentage is inferred.")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
