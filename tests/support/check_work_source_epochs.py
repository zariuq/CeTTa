#!/usr/bin/env python3
"""Check source-epoch activation and mutation through the public work library."""

import argparse
import json
import os
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms


PROGRAM = r'''
!(import! &self (library work))
!(let* (($source (new-space)) ($other (new-space)) ($epoch (work:source-epoch $source)))
   (println! (SourceEpochCase 0 (work:source-status $source $epoch) (work:source-status $other $epoch))))
!(let* (($source (new-space)) ($epoch (work:source-epoch $source))
         ($added (add-atom $source (row 1))))
   (println! (SourceEpochCase 1 (work:source-status $source $epoch)
     (work:source-status $source (work:source-epoch $source)))))
!(println! (SourceEpochCase 2 (work:source-epoch not-a-source)))
!(let $source (new-space)
   (println! (SourceEpochCase 3 (work:source-status $source (Work:SourceEpoch "1" "0" "0" extra)))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    expected = [['SourceEpochCase', '0', 'Work:Current', 'Work:Changed'],
                ['SourceEpochCase', '1', 'Work:Changed', 'Work:Current'],
                ['SourceEpochCase', '2', ['Work:Fault', 'InvalidSource']],
                ['SourceEpochCase', '3', ['Work:Fault', 'InvalidSourceEpoch']]]
    cases = []
    for language in ('he', 'petta', 'prime'):
        source = args.output/f'source-epochs-{language}.metta'
        module = '(library work)' if language == 'petta' else 'work'
        source.write_text(PROGRAM.replace('(library work)', module))
        for machine in ('0', '1'):
            result = subprocess.run([str(args.cetta.resolve()), '--lang', language, str(source.resolve())],
                                    cwd=ROOT, env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine),
                                    capture_output=True, text=True, timeout=30)
            stem = f'{language}-{machine}'
            (args.output/f'{stem}.stdout').write_text(result.stdout)
            (args.output/f'{stem}.stderr').write_text(result.stderr)
            observations = [value for value in parse_forms(result.stdout)
                            if isinstance(value,list) and value[:1] == ['SourceEpochCase']]
            cases.append({'language': language, 'machine': machine,
                          'passed': result.returncode == 0 and not result.stderr and observations == expected,
                          'observations': observations})
    report = {'passed': all(case['passed'] for case in cases), 'checks': len(cases)*len(expected), 'routes': cases}
    (args.output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
