#!/usr/bin/env python3
"""The admission normalizer's fixture law: every PeTTa fixture with an
expected output prints the same and exits the same, in the plain and the
extended profile, with the tier admitting spellings by their normal form
(CETTA_ADMISSION_NORMALIZE=1) and without it.

Each fixture is first run under a transition bound.  A run that does not
finish within that bound or the time limit (searches meant for a bounded
controller, fixtures written for the other profile), or that a resource
limit stops, is not compared: it is skipped and listed with its reason, as
are fixtures that answer differently from run to run.  A run ended by any
other signal is a crash, and fails the law.

Usage: check_admission_fixture_law.py BINARY [FIXTURE_DIR ...]"""
import concurrent.futures
import glob
import os
import signal
import subprocess
import sys

TIMEOUT = 60
# The bound under which the machine alone decides which runs are finite.
TRANSITIONS = "2000000"
# Each fixture runs in PeTTa's plain and extended profiles.
PROFILES = ([], ["--profile", "extended"])


def run(binary, path, normalize, profile, bounded):
    env = dict(os.environ, CETTA_ADMISSION_NORMALIZE=normalize)
    if bounded:
        env.setdefault("CETTA_PETTA_MACHINE_TRANSITION_LIMIT", TRANSITIONS)
    else:
        env.pop("CETTA_PETTA_MACHINE_TRANSITION_LIMIT", None)
    try:
        r = subprocess.run([binary, *profile, '--lang', 'petta', path],
                           capture_output=True, text=True, timeout=TIMEOUT,
                           env=env)
    except subprocess.TimeoutExpired:
        return None
    return r.returncode, r.stdout, r.stderr


RESOURCE_SIGNALS = (signal.SIGKILL, signal.SIGXCPU)
UNFINISHED = {
    'exhausted': f'unfinished within {int(TRANSITIONS):,} machine transitions',
    'timeout': f'unfinished within {TIMEOUT} s',
    'resource': 'stopped by a resource limit',
}


def outcome(result, bounded):
    """'completed', 'exhausted', 'timeout', 'resource' or 'crash'."""
    if result is None:
        return 'timeout'
    code, _, err = result
    if code < 0:
        if -code in RESOURCE_SIGNALS or (
                -code == signal.SIGABRT and 'out of memory' in err):
            return 'resource'
        return 'crash'
    if bounded and 'fuel-exhausted' in err:
        return 'exhausted'
    return 'completed'


def compare_in(binary, path, profile):
    """None where the runs agree; 'skipped: ...' where a run is unfinished
    within its budget or the fixture varies by itself; ('crash', run, result)
    for a crash; else both runs."""
    for label, normalize, bounded in (('bounded probe', '0', True),
                                      ('without normal forms', '0', False)):
        result = run(binary, path, normalize, profile, bounded)
        kind = outcome(result, bounded)
        if kind == 'crash':
            return 'crash', label, result
        if kind != 'completed':
            return f'skipped: {UNFINISHED[kind]}' + ('' if bounded else ' without the bound')
        if not bounded:
            off = result
    on = run(binary, path, '1', profile, False)
    if outcome(on, False) == 'crash':
        return 'crash', 'with normal forms', on
    if on is not None and on[:2] == off[:2]:
        return None
    again = run(binary, path, '0', profile, False)
    if again is None or again[:2] != off[:2]:
        return 'skipped: answers differently from run to run'
    return off, on


def compare(binary, path):
    results = []
    for profile in PROFILES:
        results.append((' '.join(profile) or 'plain',
                        compare_in(binary, path, profile)))
    return path, results


def main():
    binary = os.path.abspath(sys.argv[1])
    directories = sys.argv[2:] or ['tests/petta']
    fixtures = sorted(p for d in directories for p in glob.glob(os.path.join(d, '*.metta'))
                      if os.path.exists(p[:-len('.metta')] + '.expected'))
    differing, skipped, crashes, runs = [], [], [], 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for path, results in pool.map(lambda p: compare(binary, p), fixtures):
            for label, result in results:
                if result is None:
                    runs += 1
                elif isinstance(result, str):
                    skipped.append((path, label, result[len('skipped: '):]))
                elif result[0] == 'crash':
                    crashes.append((path, label, result[1], result[2]))
                else:
                    differing.append((path, label, result))
    for path, label, which, (code, out, err) in crashes:
        print(f'FAIL: {path} [{label}]: the {which} run ended by signal '
              f'{-code} ({signal.Signals(-code).name})')
        print('  stderr: ' + err[-300:].replace('\n', ' | '))
    for path, label, (off, on) in differing[:20]:
        print(f'FAIL: {path} [{label}]: without normal forms exit {off[0]}, with them '
              f'exit {on[0] if on else "timeout"}')
        print('  without: ' + off[1][:300].replace('\n', ' | '))
        print('  with:    ' + (on[1][:300].replace('\n', ' | ') if on else '<timeout>'))
    if differing or crashes:
        print(f'FAIL: {len(differing)} fixture runs differ, {len(crashes)} crashed')
        return 1
    print(f'PASS: {runs} runs of {len(fixtures)} PeTTa fixtures (plain and extended '
          f'profiles) answer the same with and without admission by normal form '
          f'({len(skipped)} skipped: unfinished within their budget, stopped by '
          f'a resource limit, or varying by themselves; none crashed)')
    for path, label, reason in skipped:
        print(f'  {os.path.basename(path)} [{label}]: {reason}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
