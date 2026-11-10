#!/usr/bin/env python3
"""Run the same object/stream fixture in both native language adapters."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    args = parser.parse_args()
    env = dict(os.environ)
    env['PYTHONPATH'] = str(ROOT / 'tests/support') + os.pathsep + env.get('PYTHONPATH', '')
    # A library's explicitly named Python member must reach the same format
    # loader as a path import. lib_llm's import only defines helpers; these
    # checks perform local arithmetic and never request a provider.
    source = '''!(import! &self (library lib_llm.py))
!(import! &self (library lib_llm))
!(py-eval "6 * 7")
!(import! &self (library lib_llm.py))
!(py-eval "7 * 8")
'''
    run = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', '-e', source],
                         env=env, text=True, capture_output=True, timeout=60)
    assert run.returncode == 0 and run.stdout.splitlines() == [
        'true', 'true', '42', 'true', '56'], (run.stdout, run.stderr)
    missing = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', '-e',
        '!(import! &self (library cetta_absent_python_member.py))'],
        env=env, text=True, capture_output=True, timeout=60)
    assert missing.returncode != 0 and 'existence_error source_sink' in missing.stderr, (
        missing.stdout, missing.stderr)
    print('PASS: library Python members load before authored helpers; missing members still fail')
    fixture = ROOT / 'tests/support/python_objects.metta'
    for language in ('petta', 'he'):
        run = subprocess.run([str(args.binary.resolve()), '--lang', language, str(fixture)],
                             env=env, text=True, capture_output=True, timeout=60)
        lines = run.stdout.strip().splitlines()
        expected = '19' if language == 'petta' else '[19]'
        if run.returncode or not lines or lines[-1] != expected or 'Error' in run.stdout:
            raise AssertionError((language, run.returncode, run.stdout, run.stderr))
        print(f'PASS: Python objects and streaming ({language}, native)')
        truth, false = ('true', 'false') if language == 'petta' else ('True', 'False')
        source = f'''!(import! &self (library py))
!(bind! &b (py:import "builtins"))
!(py:call (py:get &b "repr") {false})
!(py:call (py:get &b "repr") (== 1 2))
!(py:call (py:get &b "repr") (py:call (py:get &b "bool") 0))
!(py:call (py:get &b "repr") (py:item (py:call (py:get &b "dict") (py:kwargs ("flag" {false}))) "flag"))
!(py-call (builtins.repr {false}))
!(py:call (py:get &b "repr") "false")
!(let ({truth} {false}) (py:to-tuple (py:list True False)) matched)
'''
        run = subprocess.run([str(args.binary.resolve()), '--lang', language, '-e', source],
                             env=env, text=True, capture_output=True, timeout=60)
        answers = ['"False"'] * 5 + ['"\'false\'"']
        if language == 'he': answers = [f'[{value}]' for value in answers]
        answers.append('matched' if language == 'petta' else '[matched]')
        assert run.returncode == 0 and run.stdout.strip().splitlines()[-7:] == answers, (language, run.stdout, run.stderr)
        print(f'PASS: Python boolean values and ordinary strings ({language}, native)')
    failure = '(py:call (py:get (py:import "math") "sqrt") -1.0)'
    for label, expression in (
        ('direct', failure), ('collapse', f'(collapse {failure})'),
        ('case', f'(case {failure} (((Error $call $reason) handled) ($_ missed)))'),
    ):
        run = subprocess.run([str(args.binary.resolve()), '--lang', 'he', '-e',
            f'!(import! &self (library py)) !{expression}'], env=env, text=True, capture_output=True, timeout=60)
        assert run.returncode == 0, (label, run.stderr)
        if label == 'case': assert run.stdout.strip().endswith('[handled]'), run.stdout
        else:
            assert 'Error' in run.stdout and 'ValueError' in run.stdout and 'py:call' in run.stdout, run.stdout
            if label == 'collapse': assert '[((Error ' in run.stdout, run.stdout
    print('PASS: HE Python errors are visible directly, under collapse, and to case')
    # PeTTa's once cuts an active producer. HE cancellation is exercised by
    # the retained query API; once is ordinary uninterpreted syntax in HE.
    source = fixture.read_text() + '''
!(fixture:check (once (py:iter (py:call (py:get &fixture "stream")))) 4)
!(fixture:check (py:to-tuple (py:call (py:get &fixture "event_tail"))) ("yield4" "closed"))
!(fixture:check (once (py:with (py:call (py:get &fixture "Context")) context-items)) 7)
!(fixture:check (py:to-tuple (py:call (py:get &fixture "event_tail"))) ("enter" "exit"))
!(py:call (py:get &fixture "total"))
'''
    run = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', '-e', source],
                         env=env, text=True, capture_output=True, timeout=60)
    assert run.returncode == 0 and run.stdout.strip().endswith('23') and 'Error' not in run.stdout, (run.stdout, run.stderr)
    print('PASS: native PeTTa cut closes Python iterators and contexts')


if __name__ == '__main__': main()
