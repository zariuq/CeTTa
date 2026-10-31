#!/usr/bin/env python3
"""Check complete callback publication through the real Python provider."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap


ROOT = Path(__file__).resolve().parents[2]
MARKER = 'callback-completion '
COLLECTION_SETTINGS = {
    'CETTA_TERM_UNIVERSE_COLLECTION', 'CETTA_EVAL_GC', 'CETTA_OEM_COLLECT_STRESS',
}


def python_call(body):
    return '!(py-call (builtins.exec ' + json.dumps(body) + ' (py-call (builtins.dict))))\n'


COMPLETE = textwrap.dedent('''\
    import hyperon, json
    runner = hyperon.MeTTa()
    checks = 0
    def rows(source):
        return [[str(atom) for atom in row] for row in runner.run(source)]
    def expect(source, wanted):
        global checks
        got = rows(source)
        if got != wanted:
            raise AssertionError((source, got, wanted))
        checks += 1
    def refuses(source, text):
        global checks
        try:
            got = rows(source)
        except RuntimeError as error:
            if text not in str(error):
                raise AssertionError((source, str(error)))
        else:
            raise AssertionError(('incomplete callback returned', source, got))
        checks += 1

    expect('(Error payload datum)', [['(Error payload datum)']])
    expect('Empty', [['Empty']])
    expect('(empty)', [[]])
    expect('(superpose (7 7 9))', [['7', '7', '9']])
    expect('(quote (println! callback-data-must-stay-held))',
           [['(println! callback-data-must-stay-held)']])
    expect('(superpose (7 7 9)) (empty) (Error payload datum)',
           [['7', '7', '9'], [], ['(Error payload datum)']])
    expect('(let $x 1 $x) (let $x 2 $x)', [['1'], ['2']])
    expect('17 ; an incomplete (form inside a comment', [['17']])
    expect(' ; comment only', [])
    refuses('(+ 1 "s")', '(Error ')
    caught = rows('(catch (+ 1 "s"))')
    if len(caught) != 1 or len(caught[0]) != 1 or not caught[0][0].startswith('(Error '):
        raise AssertionError(('caught error is data', caught))
    checks += 1
    expect('41', [['41']])
    refuses('(callback-tick) (+ 1 "s") (callback-tick)', '(Error ')
    expect('(get-state &callback-counter)', [['1']])
    refuses('(callback-tick) (unclosed', 'could not be parsed')
    expect('(get-state &callback-counter)', [['2']])
    refuses('7 )', 'could not be parsed')
    expect('(get-state &callback-counter)', [['2']])
    import weakref, gc
    original_init = hyperon.CettaAtom.__init__
    loans = []
    def refuse_publication(self, text):
        original_init(self, text)
        loans.append(weakref.ref(self))
        if text == '9':
            raise ValueError('callback publication refused')
    hyperon.CettaAtom.__init__ = refuse_publication
    try:
        try:
            rows('(callback-tick) (superpose (7 7 9)) (callback-tick)')
        except ValueError as error:
            if str(error) != 'callback publication refused':
                raise AssertionError(str(error))
        else:
            raise AssertionError('a refused row was returned')
    finally:
        hyperon.CettaAtom.__init__ = original_init
    checks += 1
    gc.collect()
    if not loans or any(loan() is not None for loan in loans):
        raise AssertionError('failed callback retained a private row')
    checks += 1
    expect('(get-state &callback-counter)', [['3']])
    expect('41', [['41']])
    nested = 'len(__import__("hyperon").MeTTa().run("(superpose (4 4 5))")[0])'
    expect('(py-call (builtins.eval ' + json.dumps(nested) +
           ' (py-call (builtins.dict))))', [['3']])
    for i in range(64):
        expect('(Pair 3 3)', [['(Pair 3 3)']])
    print('callback-completion ' + json.dumps({'checks': checks}))
''')


def limited(source):
    return textwrap.dedent('''\
        import hyperon, json
        runner = hyperon.MeTTa()
        caught = 0
        for source in SOURCES:
            try:
                result = runner.run(source)
            except RuntimeError as error:
                if 'fuel-exhausted' not in str(error):
                    raise AssertionError(str(error))
                caught += 1
            else:
                raise AssertionError(('exhaustion was hidden', result))
        print('callback-completion ' + json.dumps({'fuel_errors': caught}))
    ''').replace('SOURCES', repr([source, '(println! callback-after-fuel-must-not-run)']))


def observe(binary, path, language='petta', fuel=None, extra=None):
    env = {k: v for k, v in os.environ.items()
           if not k.startswith('CETTA_') or k in COLLECTION_SETTINGS}
    env.update(extra or {})
    command = [str(binary), '--lang', language]
    if fuel is not None:
        command += ['--fuel', str(fuel)]
    run = subprocess.run(command + [str(path)], cwd=ROOT, env=env,
                         capture_output=True, text=True, timeout=120)
    reports = [json.loads(line[len(MARKER):]) for line in run.stdout.splitlines()
               if line.startswith(MARKER)]
    return run, reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    checks = 0
    with tempfile.TemporaryDirectory(prefix='python-callback-', dir=ROOT / 'runtime') as tmp:
        directory = Path(tmp)
        setup = ('!(bind! &callback-counter (new-state 0))\n'
                 '(= (callback-tick) (change-state! &callback-counter '
                 '(+ (get-state &callback-counter) 1)))\n')
        source = directory / 'complete.metta'
        source.write_text(setup + python_call(COMPLETE) + '!(+ 40 2)\n')
        for ordinary in (False, True):
            extra = {'CETTA_OPEN_EQUATIONS_REFERENCE': '1',
                     'CETTA_PREPARED_PURE_REFERENCE': '1'} if ordinary else {}
            run, reports = observe(binary, source, extra=extra)
            if (run.returncode or run.stderr or reports != [{'checks': 87}] or
                    '42' not in run.stdout.splitlines() or
                    'callback-data-must-stay-held' in run.stdout):
                raise AssertionError((ordinary, run.returncode, run.stdout, run.stderr))
            checks += 87
        for source_text in ('(callback-spin)', '(callback-repeat)'):
            source = directory / ('partial.metta' if 'repeat' in source_text else 'empty.metta')
            source.write_text('(= (callback-spin) (callback-spin))\n'
                              '(= (callback-repeat) 7)\n'
                              '(= (callback-repeat) (callback-repeat))\n' +
                              python_call(limited(source_text)))
            for fuel in (16, 32, 64, 128):
                run, reports = observe(binary, source, fuel=fuel)
                if (run.returncode != 1 or reports != [{'fuel_errors': 2}] or
                        'fuel-exhausted' not in run.stderr or
                        'callback-after-fuel-must-not-run' in run.stdout):
                    raise AssertionError((source_text, fuel, run.returncode,
                                          run.stdout, run.stderr))
                checks += 2
        # The existing Prime adapter retains Error-shaped data as a value.
        source = directory / 'prime-data.metta'
        source.write_text(python_call(
            "import hyperon\n"
            "rows=hyperon.MeTTa().run('(Error payload datum)')\n"
            "if [[str(a) for a in r] for r in rows] != [['(Error payload datum)']]:\n"
            " raise AssertionError(rows)\n"
            "print('callback-completion {\\\"prime_data\\\": true}')\n"))
        run, reports = observe(binary, source, language='prime')
        if run.returncode or run.stderr or reports != [{'prime_data': True}]:
            raise AssertionError((run.returncode, run.stdout, run.stderr))
        checks += 1
    print(f'PASS: {checks} Python callback controls retain data, raised control, '
          'completion, effect prefixes and enclosing authority')


if __name__ == '__main__':
    main()
