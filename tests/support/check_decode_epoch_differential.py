#!/usr/bin/env python3
"""Check that decoded-copy epochs change no program's observable behaviour.

usage: check_decode_epoch_differential.py [--jobs N] [--timeout SECONDS]
           [--poison] [--epoch-kb KB] [--work-dir DIR] [PROGRAM ...]

Every test program (tests/petta and tests/he with their languages, and the
top-level tests with the default one) runs twice: with decoded-copy epochs
off (CETTA_DECODE_EPOCHS=0) and with small epochs (CETTA_DECODE_EPOCH_KB,
16 by default), so that epochs close and are freed while the program runs.
Stdout, stderr and the exit status must be the same.  A program that differs
runs again with epochs off: if it then differs from its own first run it is
nondeterministic, otherwise the difference is real.  With --poison, a freed
epoch's storage is overwritten (CETTA_DECODE_EPOCH_POISON=1), so a read of a
freed decoded copy reads the poison.  Under AddressSanitizer or
UndefinedBehaviorSanitizer, a report on the side with epochs is counted.

A printed address is compared as a placeholder: its value follows what the
run allocated.  Programs are given on the command line, or else every test
program is run.  The binary is $CETTA_BIN.  The check fails when a real difference or a
sanitizer report is found."""
import argparse
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
import tempfile

SANITIZER_REPORT = re.compile(
    r'(ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:|'
    r'SUMMARY: UndefinedBehaviorSanitizer|ERROR: ThreadSanitizer|'
    r'WARNING: ThreadSanitizer)')
SANITIZER_LINE = re.compile(r'^(==\d+==|\s+#\d+ |SUMMARY: )')
# A printed object's address (a space's, a state's) names it, but its value
# follows the allocator: the two runs differ in what they allocate, and even
# in their environment's size, so an address is compared as a placeholder.
ADDRESS = re.compile(r'0x[0-9a-fA-F]{6,}')
LIMIT = 400000


def programs(root):
    found = []
    for path in sorted(glob.glob(os.path.join(root, 'tests', 'petta', '*.metta'))):
        found.append((path, ['--lang', 'petta']))
    for path in sorted(glob.glob(os.path.join(root, 'tests', 'he', '*.metta'))):
        found.append((path, ['--lang', 'he']))
    for path in sorted(glob.glob(os.path.join(root, 'tests', '*.metta'))):
        found.append((path, []))
    return found


def language_args(path, root):
    rel = os.path.relpath(os.path.abspath(path), root)
    if rel.startswith(os.path.join('tests', 'petta') + os.sep):
        return ['--lang', 'petta']
    if rel.startswith(os.path.join('tests', 'he') + os.sep):
        return ['--lang', 'he']
    return []


def normal(text):
    """The text a comparison reads: sanitizer lines are counted apart, and
    printed addresses are placeholders."""
    lines = [ADDRESS.sub('0x_', line) for line in text.splitlines(True)
             if not SANITIZER_LINE.match(line)]
    return ''.join(lines)[:LIMIT]


def run(binary, path, lang, env_extra, timeout, root):
    env = dict(os.environ)
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    env.update(env_extra)
    try:
        done = subprocess.run(
            [binary] + lang + [path], cwd=root, env=env,
            stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    out = done.stdout.decode('utf-8', 'replace')
    err = done.stderr.decode('utf-8', 'replace')
    return done.returncode, out, err


def same(a, b):
    return (a[0] == b[0] and normal(a[1]) == normal(b[1]) and
            normal(a[2]) == normal(b[2]))


def check(binary, item, epochs, timeout, root):
    path, lang = item
    off = {'CETTA_DECODE_EPOCHS': '0'}
    first = run(binary, path, lang, off, timeout, root)
    second = run(binary, path, lang, epochs, timeout, root)
    if first is None or second is None:
        return path, 'timeout', first, second
    report = (SANITIZER_REPORT.search(first[2]) is not None or
              SANITIZER_REPORT.search(second[2]) is not None)
    if same(first, second):
        return path, 'sanitizer' if report else 'identical', first, second
    again = run(binary, path, lang, off, timeout, root)
    if again is None:
        return path, 'timeout', first, second
    if not same(first, again):
        return path, 'sanitizer' if report else 'nondeterministic', first, second
    return path, 'sanitizer' if report else 'different', first, second


def first_difference(a, b):
    for name, x, y in (('exit', str(a[0]), str(b[0])),
                       ('stdout', normal(a[1]), normal(b[1])),
                       ('stderr', normal(a[2]), normal(b[2]))):
        if x != y:
            xs, ys = x.splitlines(), y.splitlines()
            for index in range(max(len(xs), len(ys))):
                left = xs[index] if index < len(xs) else '<end>'
                right = ys[index] if index < len(ys) else '<end>'
                if left != right:
                    return (f'{name} line {index + 1}:\n  off: {left[:300]}\n'
                            f'  on:  {right[:300]}')
    return 'no difference'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--timeout', type=float, default=120.0)
    parser.add_argument('--poison', action='store_true')
    parser.add_argument('--epoch-kb', type=int, default=16)
    parser.add_argument('--work-dir')
    parser.add_argument('programs', nargs='*')
    options = parser.parse_args()
    binary = os.environ.get('CETTA_BIN')
    if not binary:
        sys.exit('CETTA_BIN names the binary to check')
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    items = ([(path, language_args(path, root)) for path in options.programs]
             if options.programs else programs(root))
    epochs = {'CETTA_DECODE_EPOCH_KB': str(options.epoch_kb)}
    if options.poison:
        epochs['CETTA_DECODE_EPOCH_POISON'] = '1'
    work = options.work_dir or tempfile.mkdtemp(prefix='decode-epochs-')
    os.makedirs(work, exist_ok=True)
    counts = {name: 0 for name in ('identical', 'nondeterministic', 'timeout',
                                   'different', 'sanitizer')}
    with open(os.path.join(work, 'results.tsv'), 'w') as results, \
            open(os.path.join(work, 'differences.log'), 'w') as differences, \
            concurrent.futures.ThreadPoolExecutor(options.jobs) as pool:
        futures = [pool.submit(check, binary, item, epochs, options.timeout,
                               root) for item in items]
        for future in concurrent.futures.as_completed(futures):
            path, status, first, second = future.result()
            counts[status] += 1
            exits = (f'{first[0] if first else "-"}\t'
                     f'{second[0] if second else "-"}')
            results.write(f'{os.path.relpath(path, root)}\t{status}\t{exits}\n')
            results.flush()
            if status in ('different', 'sanitizer'):
                differences.write(f'== {os.path.relpath(path, root)}: {status}\n')
                if status == 'sanitizer':
                    report = [line for line in second[2].splitlines()
                              if SANITIZER_REPORT.search(line) or
                              re.match(r'\s+#\d+ ', line)]
                    differences.write('\n'.join(report[:24]) + '\n')
                else:
                    differences.write(first_difference(first, second) + '\n')
                differences.flush()
    mode = ' poisoned' if options.poison else ''
    summary = (f"{len(items)} programs, epochs off against "
               f"{options.epoch_kb} KB{mode} epochs: "
               f"{counts['identical']} identical, "
               f"{counts['nondeterministic']} nondeterministic, "
               f"{counts['timeout']} timed out, "
               f"{counts['different']} real differences, "
               f"{counts['sanitizer']} sanitizer reports")
    print(f"{'PASS' if not counts['different'] and not counts['sanitizer'] else 'FAIL'}: "
          f"{summary} (details in {work})")
    return 1 if counts['different'] or counts['sanitizer'] else 0


if __name__ == '__main__':
    sys.exit(main())
