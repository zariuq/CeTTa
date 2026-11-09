#!/usr/bin/env python3
"""HE transform: held carriers, streaming results, errors and earlier effects."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def term(*parts):
    return '(' + ' '.join(parts) + ')'


def handle(expression, success='normal'):
    return term('case', expression,
                term(term(term('Error', '$site', '$reason'), 'caught'),
                     term('$_', success)))


def directive(expression):
    return '!' + expression + '\n'


def run(binary, body, reference=False, profile='he', route=None):
    library = str(ROOT / 'tests/reference/lib_transform_he.metta') if reference \
        else '(library lib_transform)'
    source = directive(term('import!', '&self', library)) + body
    result = subprocess.run([binary, '--lang', 'he', '--profile', profile, '-e', source],
        cwd=ROOT, env=dict(os.environ, CETTA_TRANSFORM_TRACE='1', **(route or {})),
        text=True, capture_output=True, timeout=90)
    assert result.returncode == 0, (result.returncode, source, result.stdout, result.stderr)
    if reference:
        assert '[transform]' not in result.stderr, ('reference entered native transform', result.stderr)
    return result.stdout.strip().splitlines()


def main():
    binary = str(Path(sys.argv[1]).resolve())
    setup = ('!(bind! &q (new-space))\n'
             '!(add-atom &q (job a))\n')
    jobs = directive(term('collapse', term('match', '&q', term('job', '$j'), '$j')))
    seen = directive(term('collapse', term('match', '&q', 'seen', 'yes')))
    err = term('Error', 'bad', 'boom')
    mixed = term('superpose', term(err, '1'))
    errors = term('superpose', term(term('Error', 'a', 'A'), term('Error', 'b', 'B')))
    cases = [
        ('empty source streams one result', directive('(transform &self () x)'), ['[x]']),
        ('template is computed', directive('(transform &self () (+ 1 2))'), ['[3]']),
        ('unused default space', directive('(transform 42 () x)'), ['[x]']),
        ('empty instances do not write', directive('(transform &self () (,) 42)'), ['[0]']),
        ('invalid source space', directive(handle('(transform 42 (item $x) $x)')), ['[caught]']),
        ('invalid destination', directive(handle('(transform &self () x 42)')), ['[caught]']),
        ('malformed take', directive(handle('(transform &self (take) x)')), ['[caught]']),
        ('malformed let', directive(handle('(transform &self (let x) y)')), ['[caught]']),
        ('earlier effects survive error', setup + directive(handle(term('transform', '&self',
            term(',', term('let', '$_', '(add-atom &q seen)'), '(read 42 p)'), 'x'))) + jobs + seen,
            ['[()]', '[()]', '[caught]', '[(a)]', '[(yes)]']),
        ('unreached invalid source is not read', directive(term('collapse', term('transform', '&self',
            term(',', term('let', '$v', '(empty)'), '(read 42 p)'), 'x'))), ['[()]']),
        ('source error prevents take', setup + directive(handle(term('transform', '&q',
            term(',', term('let', '$v', err), '(take (job $j))'), 'done'))) + jobs,
            ['[()]', '[()]', '[caught]', '[(a)]']),
        ('routing error retains completed take', setup +
            directive(handle('(transform &q (take (job $j)) done 42)')) + jobs,
            ['[()]', '[()]', '[caught]', '[()]']),
        ('values displace source errors', directive(term('transform', '&self',
            term('let', '$v', mixed), 'yes')), ['[yes]']),
        ('pattern failure does not revive an error', directive(term('transform', '&self',
            term('let', '2', mixed), 'yes')), []),
        ('error multiplicity', directive(term('transform', '&self', term('let', '$v', errors), 'yes')),
            ['[(Error a A), (Error b B)]']),
        ('space supplied by let', setup + directive(term('transform', '&self',
            term(',', term('let', '$s', '&q'), '(take $s (job $j))'), '$j')) + jobs,
            ['[()]', '[()]', '[a]', '[()]']),
    ]
    observations = 0
    for name, body, expected in cases:
        want = ['[()]', *expected]
        reference = run(binary, body, reference=True)
        assert reference == want, (name, 'reference', reference, want)
        for profile in ('he', 'extended'):
            for route in ({}, {'CETTA_HE_SHARED_EXECUTION': '0'}):
                actual = run(binary, body, profile=profile, route=route)
                assert actual == reference, (name, profile, route, actual, reference)
                observations += 1

    first_cases = [
        ('computed first template', directive('(once (transform &self () (+ 1 2)))'), ['[3]']),
        ('collected first template', directive('(collapse (once (transform &self () (+ 1 2))))'), ['[(3)]']),
        ('function destination', '(= (inc $x) (+ $x 1))\n' +
            directive('(once (transform &self () 4 inc))'), ['[5]']),
        ('first error is visible', directive(handle('(once (transform &self () x 42))')), ['[caught]']),
        ('empty first firing continues', setup + '!(add-atom &q (job b))\n' +
            directive('(once (transform &q (take (job $j)) (if (== $j a) (empty) (got $j))))') + jobs,
            ['[()]', '[()]', '[()]', '[(got b)]', '[()]']),
        ('no instances means no first answer', setup +
            directive('(collapse (once (transform &q (take (job $j)) (,))))') + jobs,
            ['[()]', '[()]', '[()]', '[()]']),
        ('error stops later firings', setup + '!(add-atom &q (job b))\n' +
            directive(handle('(once (transform &q (take (job $j)) done 42))')) + jobs,
            ['[()]', '[()]', '[()]', '[caught]', '[(b)]']),
    ]
    for name, body, expected in first_cases:
        for route in ({}, {'CETTA_HE_SHARED_EXECUTION': '0'}):
            actual = run(binary, body, profile='extended', route=route)
            assert actual == ['[()]', *expected], (name, route, actual, expected)
            observations += 1
    print(f'PASS: {observations} HE transform boundary observations preserve streaming, errors, multiplicity, earlier effects and first-answer routing')


if __name__ == '__main__':
    main()
