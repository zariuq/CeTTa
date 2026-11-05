#!/usr/bin/env python3
"""The compiled reader built with its trace reads as the reader built without.

usage: check_gslt_trace.py MG_GSLT_NATIVE MG_GSLT_NATIVE_TRACE WORK FILE...

For each FILE, tokens and items: both builds print the same readings and the
same refusal, and the trace is well formed.  Every line is a mode entered
(call MODE INPUT...), a rule that answers or misses (answer RULE),
(miss RULE), or a mode without an answer (none MODE), indented two spaces
per open call: a call opens one, an answer or a none closes it (an answer
by a jump closes the rule's call before the call it jumps to), and every
call is closed by the end of the run.
"""

import pathlib
import re
import subprocess
import sys

LINE = re.compile(r'^( *)\((call|answer|miss|none) (\S+)( .*)?\)$')


def trace_problem(text):
    """Why TEXT is not a well-formed trace, or None."""
    depth = 0
    calls = 0
    for number, line in enumerate(text.splitlines(), 1):
        m = LINE.match(line)
        if not m or len(m.group(1)) % 2:
            return 'line %d is not an event: %r' % (number, line[:120])
        kind = m.group(2)
        if kind in ('answer', 'none'):
            if depth == 0:
                return 'line %d closes no call' % number
            depth -= 1
        if len(m.group(1)) != 2 * depth:
            return 'line %d is at depth %d, not %d' % (number, len(m.group(1)) // 2, depth)
        if kind == 'call':
            depth += 1
            calls += 1
    if calls == 0:
        return 'no call'
    if depth:
        return '%d calls left open' % depth
    return None


def main():
    if len(sys.argv) < 5:
        print(__doc__, file=sys.stderr)
        return 64
    plain, traced, work = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])
    work.mkdir(parents=True, exist_ok=True)
    trace = work / 'trace'
    passed = failed = 0
    for name in sys.argv[4:]:
        for mode in ('--tokens', '--items'):
            a = subprocess.run([plain, mode, name], capture_output=True, timeout=600)
            b = subprocess.run([traced, mode, '--trace', str(trace), name], capture_output=True, timeout=600)
            problem = None
            if (a.returncode, a.stdout, a.stderr) != (b.returncode, b.stdout, b.stderr):
                problem = 'the traced build reads differently'
            else:
                problem = trace_problem(trace.read_text(encoding='utf-8', errors='replace'))
            if problem:
                failed += 1
                print('FAIL %s %s: %s' % (name, mode, problem))
            else:
                passed += 1
    trace.unlink(missing_ok=True)
    print('(MegalodonGsltTrace %d passed %d failed)' % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
