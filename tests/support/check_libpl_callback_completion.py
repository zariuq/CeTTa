#!/usr/bin/env python3
"""Qualify completion and query ownership through the installed Prolog bridge."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
KEEP = {'CETTA_TERM_UNIVERSE_COLLECTION', 'CETTA_EVAL_GC',
        'CETTA_OEM_COLLECT_STRESS', 'CETTA_PETTA_COLLECT_STRESS'}
FUEL_ERROR = 'error: observation incomplete: fuel-exhausted\n'


def observes(run, stdout, stderr, status):
    return (run.returncode, run.stdout, run.stderr) == (status, stdout, stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--stats', action='store_true')
    args = parser.parse_args()
    binary, probe = args.cetta.resolve(), args.probe.resolve()
    checks = 0
    env = {key: value for key, value in os.environ.items()
           if not key.startswith('CETTA_') or key in KEEP}
    with tempfile.TemporaryDirectory(prefix='libpl-callback-', dir=ROOT / 'runtime') as tmp:
        directory = Path(tmp)
        fixture = directory / 'completion.pl'
        # The probe observes installed callbacks; it does not replace eval/2.
        fixture.write_text(':- use_foreign_library(' +
                           "'" + str(probe).replace("'", "''") + "').\n" +
                           (ROOT / 'tests/petta/fixtures/libpl_callback_completion.pl').read_text())
        setup = ('!(import! &self (library lib_import))\n'
                 '!(import_prolog_functions_from_file ' + json.dumps(str(fixture)) +
                 ' (callback-complete callback-limited callback-signatures callback-injected))\n'
                 '(= (callback-spin) (callback-spin))\n'
                 '(= (callback-repeat) 7)\n'
                 '(= (callback-repeat) (callback-repeat))\n'
                 '(= (callback-tick) (println! callback-tick-must-not-run))\n')

        def run(name, expression, fuel=None, ordinary=False, query=False):
            nonlocal checks
            source = directory / (name + '.metta')
            imports = '!(import! &self lib_prolog)\n' if query else ''
            source.write_text(setup + imports + '!'+expression+'\n')
            command = [str(binary), '--lang', 'petta']
            if fuel is not None:
                command += ['--fuel', str(fuel)]
            if args.stats:
                command += ['--emit-runtime-stats']
            route = {'CETTA_OPEN_EQUATIONS_REFERENCE': '1',
                     'CETTA_PREPARED_PURE_REFERENCE': '1'} if ordinary else {}
            result = subprocess.run(command + [str(source)], cwd=ROOT, env=env | route,
                                    capture_output=True, text=True, timeout=30)
            if args.stats and result.returncode == 0:
                counters = {}
                errors = []
                for line in result.stderr.splitlines(keepends=True):
                    if line.startswith('runtime-counter '):
                        _, key, value = line.split()
                        counters[key] = int(value)
                    else:
                        errors.append(line)
                result.stderr = ''.join(errors)
                live = counters.get('petta-libpl-eval-state-live')
                peak = counters.get('petta-libpl-eval-state-live-peak')
                if counters.get('runtime-statistics-recorded') != 1 or live != 0 or peak is None or peak > 4:
                    raise AssertionError(('callback ownership', name, live, peak))
                checks += 1
            # The CLI prints counters only for completed documents. Interrupted
            # query ownership is observed inside the native resume/close probe.
            return result

        for ordinary in (False, True):
            for mode, wanted in [('data', '(Error payload datum)'), ('empty', '()'),
                                 ('duplicates', '(7 7 9)'), ('raised', 'caught'),
                                 ('nested', '(7 7 9)'), ('first', '7')]:
                result = run('complete-'+mode, '(callback-complete '+mode+')',
                             ordinary=ordinary)
                if result.returncode or result.stderr or result.stdout.splitlines() != [
                        'true', 'true', 'true', 'true', 'true', wanted]:
                    raise AssertionError((mode, ordinary, result))
                checks += 1
            result = run('caught', '(callback-complete caught)', ordinary=ordinary)
            if result.returncode or result.stderr or not result.stdout.splitlines()[-1].startswith('(Error '):
                raise AssertionError(('caught Error must be data', result))
            checks += 1
            result = run('signatures-complete', '(callback-signatures &self)',
                         ordinary=ordinary)
            if not observes(result, 'callback-entered\ncallback-cleanup\ncallback-after\n' +
                            'true\ntrue\ntrue\ntrue\ntrue\ndone\n', '', 0):
                raise AssertionError(('completed signature registration', ordinary, result))
            checks += 1
            for code, cause in [(2, 'cancelled'), (3, 'stack-exhausted'),
                                (4, 'capacity-exhausted'), (5, 'revision-invalidated'),
                                (6, 'host-failure')]:
                result = run('injected-'+cause, '(callback-injected '+str(code)+')',
                             ordinary=ordinary)
                if not observes(result, 'callback-cleanup\n',
                                'error: observation incomplete: '+cause+'\n',
                                2 if code in (3, 4) else 1):
                    raise AssertionError(('first cause with unused fuel', ordinary, cause, result))
                checks += 1
            for mode in ['branch', 'catch', 'partial', 'cleanup', 'cleanup_throw',
                         'cleanup_reenter', 'cleanup_signatures', 'nested', 'resume', 'noyield']:
                for fuel in (64, 96, 128):
                    expected = 'callback-entered\n'
                    if mode == 'nested':
                        expected += 'callback-entered\n'
                    if mode.startswith('cleanup') or mode == 'nested':
                        expected += 'callback-cleanup\n'
                    if mode == 'resume':
                        expected += ('callback-inner-effect\n'
                                     'callback-resume-0=255\ncallback-resume-1=255\n'
                                     'callback-resume-2=255\ncallback-resume-close=1\n')
                    if mode == 'noyield':
                        expected += ('callback-unwind-recovery\n'
                                     'callback-noyield-status=-1\ncallback-noyield-close=1\n')
                    result = run(mode, '(callback-limited '+mode+')', fuel, ordinary)
                    if not observes(result, expected, FUEL_ERROR, 1):
                        raise AssertionError((mode, ordinary, fuel, result))
                    checks += 1
            result = run('guest-yield', '(callback-limited guest_yield)', ordinary=ordinary)
            if not observes(result, 'callback-entered\ncallback-guest-pruned\n',
                            'error: observation incomplete: host-failure\n', 1):
                raise AssertionError(('unscheduled yield is incomplete', ordinary, result))
            checks += 1
            # The library query owns a packed result list and a separate query
            # cursor; interruption must not become its query-failed datum.
            for mode in ['branch', 'partial', 'cleanup_reenter', 'guest_yield']:
                expression = ('(prolog:query (quote (prolog:compound '
                              '(callback-limited '+mode+' $r))) (quote $r))')
                expected = 'callback-entered\n'
                if mode == 'cleanup_reenter':
                    expected += 'callback-cleanup\n'
                cause = 'fuel-exhausted'
                if mode == 'guest_yield':
                    expected += 'callback-guest-pruned\n'
                    cause = 'host-failure'
                result = run('query-'+mode, expression, 128, ordinary, query=True)
                if not observes(result, expected,
                                'error: observation incomplete: '+cause+'\n', 1):
                    raise AssertionError(('query completion', ordinary, mode, result))
                checks += 1
    print(f'PASS: {checks} Prolog callback controls preserve completion, data, '
          'cleanup, terminal resume and query ownership')


if __name__ == '__main__':
    main()
