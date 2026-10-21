#!/usr/bin/env python3
"""Check owned-tree observations against the ordinary process provider."""
import argparse
import json
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'runtime/process-scope-tests')
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = output / 'fixture'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(ROOT / 'tests/support/proc_fixture_v1.c'), '-o', str(fixture)], check=True)
    count = 0

    def run(language, argv, options):
        source = ('!(import! &self proc)\n!(proc:run (' +
                  ' '.join(json.dumps(str(x)) for x in argv) + ') ' + options + ')\n')
        result = subprocess.run([str(binary), '--lang', language, '-e', source],
                                cwd=ROOT, capture_output=True, timeout=30)
        assert result.returncode == 0, (source, result)
        return result.stdout

    for language in ('he', 'petta@1.0.6', 'prime'):
        for command in ('control', 'bytes', 'duplex', 'signal', 'exit', 'environment'):
            options = (('(stdin "input λ\\n") ' if command == 'duplex' else '') +
                       '(env (("CETTA_PROC_FIXTURE" "isolated"))) '
                       f'(cwd {json.dumps(str(output))})')
            wanted = run(language, [fixture, command], '(' + options + ')')
            actual = run(language, [fixture, command], '((lifetime owner) ' + options + ')')
            assert wanted == actual, (language, command, wanted, actual)
            count += 1
        missing = run(language, [output / 'missing'], '((lifetime owner))')
        assert b'proc:launch-failed' in missing and b'exec' in missing, missing
        count += 1
        for options in ('((lifetime invalid))', '((lifetime owner) (lifetime group))'):
            assert b'proc:invalid-argument' in run(language, [fixture, 'control'], options)
            count += 1
        timed = run(language, [fixture, 'sleep'], '((lifetime owner) (timeout-ms 50))')
        assert b'proc:timed-out' in timed and b'proc:signaled' in timed, timed
        count += 1
        source = ('!(import! &self proc)\n'
                  '!(let $owner (proc:spawn (' + json.dumps(str(fixture)) + ' "sleep") ((lifetime owner))) '
                  '(let $pending (proc:wait $owner 20) (let $cancelled (proc:cancel $owner) '
                  '(let $result (proc:await $owner) (let $second (proc:poll $owner) '
                  '(let $closed (proc:close $owner) (Result $pending $result $second)))))))\n')
        result = subprocess.run([str(binary), '--lang', language, '-e', source],
                                cwd=ROOT, capture_output=True, timeout=30)
        assert result.returncode == 0 and b'proc:cancelled' in result.stdout, result
        assert b'proc:pending' in result.stdout and b'proc:already-consumed' in result.stdout, result
        count += 3
    # Replacing a deployed executable must not append Linux's " (deleted)"
    # annotation to the companion name for processes already running it.
    directory = Path(tempfile.mkdtemp(prefix='replacement-', dir=output))
    live, replacement = directory / 'live', directory / 'next'
    shutil.copy2(binary, live)
    shutil.copy2(binary, replacement)
    companion = Path(str(live) + '.proc-scope')
    shutil.copy2(Path(str(binary) + '.proc-scope'), companion)
    ready = directory / 'ready'
    source = ('!(import! &self proc)\n!(let $first (proc:run (' + json.dumps(str(fixture)) +
              ' "sleep" ' + json.dumps(str(ready)) + ') ((lifetime owner) (timeout-ms 1000))) '
              '(proc:run (' + json.dumps(str(fixture)) + ' "control") ((lifetime owner))))\n')
    caller = subprocess.Popen([str(live), '--lang', 'petta@1.0.6', '-e', source],
                              cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        end = time.monotonic() + 30
        while not ready.exists():
            assert caller.poll() is None and time.monotonic() < end, 'replacement fixture did not start'
            time.sleep(0.01)
        os.replace(replacement, live)
        stdout, stderr = caller.communicate(timeout=30)
        assert caller.returncode == 0 and b'PASS: fixture' in stdout, (stdout, stderr)
        assert b'proc:host-failed' not in stdout and b'proc:launch-failed' not in stdout, stdout
        count += 2
    finally:
        if caller.poll() is None:
            caller.kill()
            caller.communicate(timeout=30)
    invalid = directory / 'invalid'
    shutil.copy2(binary, invalid)
    Path(str(invalid) + '.proc-scope').symlink_to(shutil.which('true'))
    source = '!(import! &self proc)\n!(proc:run ("true") ((lifetime owner)))\n'
    bad = subprocess.run([str(invalid), '--lang', 'petta@1.0.6', '-e', source],
                         cwd=ROOT, capture_output=True, timeout=30)
    assert bad.returncode == 0 and b'proc:host-failed' in bad.stdout, bad
    count += 1
    print(f'(ProcessScopeV1Summary {count} 0)')


if __name__ == '__main__':
    main()
