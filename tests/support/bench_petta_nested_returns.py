#!/usr/bin/env python3
"""Scaling of a PeTTa reader loop that builds its result through nested returns.

The loop is the shape langdef-compile petta-direct generates for a GSLT
lexer relation (MgDelimitedV1 in langdef/megalodon): read one element,
check it against the delimiter with a guard, recurse on the rest, and
build the result after the recursive call returns,

    (= (scan $d $input)
       (let (C $c $r) $input
       (let $_ (different-guard (different $c $d))
       (let (res $text $after) (scan $d $r)
       (quote (res (S $c $text) $after))))))

so the recursion is as deep as the input is long and each level runs one
guard.  Read linearly, doubling the input doubles the time.  The program
runs typed (Atom parameters, as the generator declares them) and untyped;
the input is built from constructors without equations, so both read the
same data.

usage: bench_petta_nested_returns.py CETTA [--sizes 1000,2000,4000] [--work DIR]
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

PROGRAM = '''
(= (different-guard (different $left $right))
   (if (== (quote $left) (quote $right)) (empty) (quote (different $left $right))))
(= (scan $d $input)
   (let (C $d $r) $input
   (quote (res Nil $r))))
(= (scan $d $input)
   (let (C $c $r) $input
   (let $_ (different-guard (different $c $d))
   (let (res $text $after) (scan $d $r)
   (quote (res (S $c $text) $after))))))
'''

TYPES = '''
(: different-guard (-> Atom %Undefined%))
(: scan (-> Atom Atom %Undefined%))
'''


def source(n, typed):
    data = '(C (cp 97) ' * n + '(C (cp 34) E)' + ')' * n
    return (TYPES if typed else '') + PROGRAM + \
        '!(let (res $text $after) (scan (cp 34) %s) (println! scan-done))\n' % data


def run(cetta, path, timeout):
    started = time.perf_counter()
    try:
        p = subprocess.run([cetta, '--lang', 'petta', str(path)], capture_output=True, timeout=timeout,
                           env={'CETTA_PETTA_SEARCH_MACHINE': '1', 'PATH': '/usr/bin:/bin'})
    except subprocess.TimeoutExpired:
        return None, 'timeout'
    elapsed = time.perf_counter() - started
    ok = p.returncode == 0 and b'scan-done' in p.stdout
    return elapsed, 'ok' if ok else 'exit %d %s' % (p.returncode, p.stderr.decode('utf-8', 'replace')[-120:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cetta')
    ap.add_argument('--sizes', default='1000,2000,4000')
    ap.add_argument('--work', default='runtime/bench-petta-nested-returns')
    ap.add_argument('--timeout', type=float, default=900.0)
    args = ap.parse_args()
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    sizes = [int(s) for s in args.sizes.split(',')]
    print('%-8s %8s %10s %8s' % ('variant', 'n', 'seconds', 'ratio'))
    for typed in (True, False):
        previous = None
        for n in sizes:
            path = work / ('scan_%s_%d.metta' % ('typed' if typed else 'untyped', n))
            path.write_text(source(n, typed))
            elapsed, status = run(os.path.abspath(args.cetta), path, args.timeout)
            ratio = '' if previous is None or elapsed is None else '%.1fx' % (elapsed / previous)
            print('%-8s %8d %10s %8s %s' % ('typed' if typed else 'untyped', n,
                                            '-' if elapsed is None else '%.2f' % elapsed, ratio,
                                            '' if status == 'ok' else status), flush=True)
            previous = elapsed
    return 0


if __name__ == '__main__':
    sys.exit(main())
