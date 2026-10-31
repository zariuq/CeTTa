#!/usr/bin/env python3
"""Check source credit through repeated installed eval/2 callbacks."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
KEEP = {'CETTA_TERM_UNIVERSE_COLLECTION', 'CETTA_EVAL_GC',
        'CETTA_OEM_COLLECT_STRESS', 'CETTA_PETTA_COLLECT_STRESS'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    fixture = ROOT / 'tests/petta/fixtures/libpl_callback_completion.pl'
    setup = ('!(import! &self (library lib_import))\n'
             '!(import_prolog_functions_from_file '+json.dumps(str(fixture))+
             ' (callback-source-steps))\n')
    env = {k: v for k, v in os.environ.items()
           if not k.startswith('CETTA_') or k in KEEP}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='callback-source-', dir=ROOT/'runtime') as tmp:
        source = Path(tmp)/'source.metta'
        for ordinary in (False, True):
            route = {'CETTA_OPEN_EQUATIONS_REFERENCE': '1',
                     'CETTA_PREPARED_PURE_REFERENCE': '1'} if ordinary else {}
            for count in (0, 1, 4):
                source.write_text(setup+'!(callback-source-steps '+str(count)+')\n')
                run = subprocess.run([str(binary), '--lang', 'petta', '--fuel', '1024', str(source)],
                                     cwd=ROOT, env=env|route, capture_output=True, text=True, timeout=30)
                wanted = ['callback-source-paid']*count+['true', 'true', str(count)]
                if run.returncode or run.stderr or run.stdout.splitlines() != wanted:
                    raise AssertionError(('completed callbacks', ordinary, count, run))
                checks += 1
            source.write_text(setup+'!(callback-source-steps 128)\n')
            prefixes = {}
            for fuel in range(64, 82):
                run = subprocess.run([str(binary), '--lang', 'petta', '--fuel', str(fuel), str(source)],
                                     cwd=ROOT, env=env|route, capture_output=True, text=True, timeout=30)
                lines = run.stdout.splitlines()
                if (run.returncode != 1 or
                        run.stderr != 'error: observation incomplete: fuel-exhausted\n' or
                        not lines or any(line != 'callback-source-paid' for line in lines) or
                        2*len(lines) > fuel):
                    raise AssertionError(('callback debit omitted', ordinary, fuel, run))
                prefixes[fuel] = len(lines)
                checks += 1
            # A literal callback has the canonical typed and bare entry visits.
            # Two extra credits admit one further completed callback, regardless
            # of the enclosing query's constant entrance cost. No retry is used.
            for fuel in range(64, 80):
                if prefixes[fuel+2] != prefixes[fuel]+1:
                    raise AssertionError(('callback debit duplicated', ordinary, fuel, prefixes))
                checks += 1
    print(f'PASS: {checks} Prolog callback source controls preserve payment, '
          'completed-value publication and bounded prefixes')


if __name__ == '__main__':
    main()
