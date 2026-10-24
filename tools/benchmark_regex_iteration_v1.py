#!/usr/bin/env python3
"""Compare complete ordered capture observations before timing repeated scans."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import time

from test_composable_syntax_v1 import text
from gslt2parse_schema_v1 import Symbol
from test_lbnf_values_v1 import headed, llist, output_values

ROOT = Path(__file__).resolve().parents[1]


def fixtures(sizes):
    for size in sizes:
        sample = ''.join('("t%d" "\n" ((shell "cat src/file%d.py")) "\n")\n%s\n'
                         % (i, i, 'x' * 170) for i in range(size))
        yield ('headers-' + str(size),
               r'(?:\A|[\x{A}-\x{D}\x{1C}-\x{1E}\x{85}\x{2028}-\x{2029}])\("',
               r'(?:\A|[\n-\r\x1c-\x1e\x85\u2028-\u2029])\("', sample)
    yield ('code-fences', r'(?s)```(?:lean4?|)\s*\n(.*?)```',
           r'(?s)```(?:lean4?|)\s*\n(.*?)```',
           ('Comment λ\n```lean4\nby\n  exact True.intro\n```\n' * 120) + '```unfinished')
    quoted = r"'(?:\\.|[^'\\])*'|\"(?:\\.|[^\"\\])*\"|%[^\n]*(?:\n|$)|/\*.*?\*/"
    space = r'(?:[ \t\r\n]|%[^\n]*(?:\n|$)|/\*.*?\*/)*'
    word = r"(?:'(?:\\.|[^'\\])*'|[a-z][A-Za-z0-9_]*|[0-9]+)"
    pattern = '(?s)' + quoted + '|file' + space + r'\(' + space + '(' + word + ')' + space + ',' + space + '(' + word + ')' + space + r'\)|[A-Za-z_$][A-Za-z0-9_$]*'
    yield ('vampire-references', pattern, pattern,
           ''.join("thf(s%d,axiom,p,file('input',premise_%d)). %% λ\n" % (i, i) for i in range(120)))


def captures(value):
    assert headed(value, 're:match', 3), value
    result = []
    for group in llist(value[3]):
        if headed(group, 're:capture-span', 3):
            result.append([group[2], group[3]])
        else:
            assert headed(group, 're:unmatched', 1), group
            result.append(None)
    assert result[0] == list(value[1:3])
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cetta', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--sizes', type=int, nargs='+', default=[120, 1200, 12000])
    p.add_argument('--repeat', type=int, default=7)
    a = p.parse_args()
    if min(a.sizes) < 1 or a.repeat < 1:
        p.error('sizes and repeat must be positive')
    a.output.mkdir(parents=True, exist_ok=True)
    records = []
    for name, native, python, sample in fixtures(a.sizes):
        fixture = (a.output / (name + '.txt')).resolve()
        fixture.write_text(sample)
        re.purge()
        start = time.perf_counter_ns()
        compiled = re.compile(python)
        compile_python = time.perf_counter_ns() - start
        offsets = [0]
        for char in sample:
            offsets.append(offsets[-1] + len(char.encode()))
        expected = [[None if m.start(g) < 0 else [offsets[m.start(g)], offsets[m.end(g)]]
                     for g in range(compiled.groups + 1)] for m in compiled.finditer(sample)]
        driver = '\n'.join([
            '!(import! &self fs)', '!(import! &self re)', '!(import! &self system)',
            '(= (bench:emit LNil) ())',
            '(= (bench:emit (LCons $head $tail)) (let $unit (println! $head) (bench:emit $tail)))',
            '!(bind! &begin (new-state (system:monotonic-ns)))',
            f'!(bind! &pattern (new-state (re:compile {text(native)})))',
            '!(println! (CompileNs (- (system:monotonic-ns) (get-state &begin))))',
            '!(bind! &reference (new-state (re:restore (re:store (get-state &pattern)))))',
            f'!(bind! &input (new-state (fs:read-text {text(str(fixture))})))',
            '!(let (re:matches $matches) (re:find-all (get-state &pattern) (get-state &input)) (bench:emit $matches))',
            '!(println! (RestoredEquivalent (== (re:find-all (get-state &pattern) (get-state &input)) (re:find-all (get-state &reference) (get-state &input)))))',
        ]) + '\n'
        # Restored CRP2 graphs retain the exact instructions but do not rebuild
        # unstored accelerators. Alternate their order in the same process.
        for repeat in range(a.repeat + 1):
            order = [('pattern', 'MatchNs'), ('reference', 'UnfilteredNs')]
            for owner, label in order[::1 if repeat % 2 == 0 else -1]:
                driver += ('!(let* (($begin (system:monotonic-ns))'
                           f' ((re:matches $matches) (re:find-all (get-state &{owner}) (get-state &input)))'
                           f' ($end (system:monotonic-ns))) (println! ({label} (- $end $begin))))\n')
        path = a.output / (name + '.metta')
        path.write_text(driver)
        # Matching-only profile: exclude observation printing and comparison.
        path.with_suffix('.profile.metta').write_text('\n'.join(
            line for line in driver.splitlines()
            if not line.startswith('!(let (re:matches') and 'RestoredEquivalent' not in line) + '\n')
        run = subprocess.run([str(a.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                             cwd=ROOT, text=True, capture_output=True, timeout=180)
        (a.output / (name + '.stdout')).write_text(run.stdout)
        (a.output / (name + '.stderr')).write_text(run.stderr)
        if run.returncode:
            raise RuntimeError((name, run.returncode, run.stderr))
        values = output_values(run.stdout)
        actual = [captures(v) for v in values if headed(v, 're:match', 3)]
        assert actual == expected, (name, 'ordered byte-span/capture disagreement', len(actual), len(expected))
        equivalent = [v[1] for v in values if headed(v, 'RestoredEquivalent', 1)]
        assert len(equivalent) == 1 and equivalent[0] in (Symbol('true'), Symbol('True')), (name, 'restored graph differs')
        native_times = [v[1] for v in values if headed(v, 'MatchNs', 1)]
        reference_times = [v[1] for v in values if headed(v, 'UnfilteredNs', 1)]
        compile_times = [v[1] for v in values if headed(v, 'CompileNs', 1)]
        assert len(native_times) == len(reference_times) == a.repeat + 1 and len(compile_times) == 1, (name, 'missing timings')
        python_times = []
        for _ in range(a.repeat + 1):
            start = time.perf_counter_ns()
            matches = list(compiled.finditer(sample))
            python_times.append(time.perf_counter_ns() - start)
            assert len(matches) == len(expected)
        record = dict(case=name, bytes=len(sample.encode()), matches=len(expected),
                      observation_sha256=hashlib.sha256(json.dumps(actual).encode()).hexdigest(),
                      native_compile_ns=compile_times[0], python_compile_ns=compile_python,
                      native_ns=native_times[1:], python_ns=python_times[1:],
                      unfiltered_ns=reference_times[1:],
                      unfiltered_median_ms=statistics.median(reference_times[1:]) / 1e6,
                      native_median_ms=statistics.median(native_times[1:]) / 1e6,
                      python_median_ms=statistics.median(python_times[1:]) / 1e6)
        records.append(record)
        print(json.dumps(record), flush=True)
    result = dict(binary_sha256=hashlib.file_digest(a.cetta.open('rb'), 'sha256').hexdigest(),
                  cases=records)
    (a.output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
