#!/usr/bin/env python3
"""Deterministic Python calls must not retain one search choice per iteration."""
import os
from pathlib import Path
import re
import sys

from check_list_building_linear import measure

ROOT = Path(__file__).resolve().parents[2]
RUNGS = (25000, 50000, 200000)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    os.environ['PYTHONPATH'] = str(ROOT / 'tests/support') + os.pathsep + os.environ.get('PYTHONPATH', '')
    setup = '!(import! &self (library py))\n'
    programs = {
        'bound': setup + '''
!(py:bind! &self probe:sqrt (py:get (py:import "math") "sqrt"))
(= (loop $n) (if (== $n 0) done (let $unused (probe:sqrt 4.0) (loop (- $n 1)))))
''',
        'lookup': setup + '''
!(bind! &math (py:import "math"))
(= (loop $n) (if (== $n 0) done
  (let $unused (py:call (py:get &math "sqrt") 4.0) (loop (- $n 1)))))
''',
        'objects': setup + '''
!(bind! &probe (py:import "python_retention_fixture"))
!(py:bind! &self probe:new (py:get &probe "Item"))
!(py:bind! &self probe:counts (py:get &probe "counts"))
(= (loop $n) (if (== $n 0) (py:to-tuple (probe:counts))
  (let $unused (probe:new) (loop (- $n 1)))))
''',
    }
    for case, program in programs.items():
        peaks, live_peaks = [], []
        for count in RUNGS:
            output, rss = measure(binary, program + f'!(loop {count})\n')
            answer = output.splitlines()[-1]
            if case == 'objects':
                match = re.fullmatch(r'\((\d+) (\d+) (\d+)\)', answer)
                assert match, answer
                made, live, peak = map(int, match.groups())
                assert made == count and live < count // 4 and peak <= count, (count, answer)
                live_peaks.append(peak)
            else:
                assert answer == 'done', (case, count, output)
            peaks.append(rss)
            print(f'{case} n={count}: peak_rss_kib={rss}, answer={answer}', flush=True)
        # Eight times the calls must not double retained memory. Compare
        # within a process configuration, without a workstation memory cap.
        assert max(peaks[1:]) <= 2 * peaks[0], (case, peaks)
        if live_peaks:
            assert max(live_peaks[1:]) <= 2 * max(live_peaks[0], 1), live_peaks
    print('PASS: Python calls have bounded retention and release transient objects during loops')


if __name__ == '__main__': main()
