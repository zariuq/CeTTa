#!/usr/bin/env python3
"""The compiled reader traced reads as the reader not traced, and its traces
are derivations.

usage: check_gslt_trace.py MG_GSLT_NATIVE RULEC_TRACECHECK WIRE WORK FILE...

For each FILE, tokens and items: mg-gslt-native prints the same readings and
the same refusal with --trace PREFIX as without, and each file PREFIX.ENTRY
it writes (the derivations of the steps of entry ENTRY, one after another, as
rulec's trace events) is checked by rulec_tracecheck against WIRE, the rule
module the presentations were compiled from: every step's trace is a
derivation of ENTRY by the module's rules.
"""

import pathlib
import re
import subprocess
import sys


def main():
    if len(sys.argv) < 6:
        print(__doc__, file=sys.stderr)
        return 64
    reader, tracecheck, wire, work = sys.argv[1], sys.argv[2], sys.argv[3], pathlib.Path(sys.argv[4])
    work.mkdir(parents=True, exist_ok=True)
    passed = failed = 0
    for name in sys.argv[5:]:
        for mode in ('--tokens', '--items'):
            for old in work.glob('trace.*'):
                old.unlink()
            prefix = work / 'trace'
            a = subprocess.run([reader, mode, name], capture_output=True, timeout=600)
            b = subprocess.run([reader, mode, '--trace', str(prefix), name], capture_output=True, timeout=600)
            problem = None
            if (a.returncode, a.stdout, a.stderr) != (b.returncode, b.stdout, b.stderr):
                problem = 'the traced reader reads otherwise'
            traces = sorted(work.glob('trace.*'))
            if not problem and not traces:
                problem = 'no trace written'
            for trace in traces:
                if problem:
                    break
                entry = trace.name[len('trace.'):]
                checked = subprocess.run([tracecheck, wire, entry, entry, '--many', str(trace)],
                                         capture_output=True, text=True, timeout=600)
                m = re.search(r': (\d+) traces not a derivation', checked.stdout)
                if checked.returncode != 0 or not m or m.group(1) != '0':
                    problem = '%s: %s' % (entry, (checked.stdout + checked.stderr).strip()[-400:])
            if problem:
                failed += 1
                print('FAIL %s %s: %s' % (name, mode, problem))
            else:
                passed += 1
    print('PASS: %d traced readings read as untraced, every step a derivation' % passed if not failed
          else 'FAIL: %d traced readings, %d failed' % (passed + failed, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
