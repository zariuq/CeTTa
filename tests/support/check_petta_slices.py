#!/usr/bin/env python3
"""Check that a PeTTa document read a few forms at a time answers as one read
whole, and that reading it in slices keeps the load's memory flat.

usage: check_petta_slices.py FORMS PROGRAM...

Each PROGRAM (with PROGRAM's .expected beside it) runs with its slices
forced to 3 forms, and its answers must agree with the expected ones as
bags.  Then a generated document of FORMS facts that its space keeps as
bits, and one of four times as many, run read whole and in slices of a
quarter of FORMS forms, so that both are read in several full slices: from
one size to the other, the peak memory read in slices may grow by at most a
third of what it grows read whole, which must be 8 MiB or more, or the test
measures nothing.  The binary is $CETTA_BIN."""
import os
import resource
import subprocess
import sys
import tempfile

FLOOR_KIB = 8 * 1024
HERE = os.path.dirname(os.path.abspath(__file__))
COMPARE = os.path.join(HERE, 'compare_answers.py')


def cetta(args, env_extra, timeout=600):
    env = dict(os.environ, **env_extra)
    return subprocess.run([os.environ['CETTA_BIN'], '--profile', 'extended',
                           '--lang', 'petta', *args], env=env,
                          capture_output=True, text=True, timeout=timeout)


def peak_kib(program, env_extra):
    child = subprocess.run(
        [sys.executable, __file__, '--one', program, *(
            f'{k}={v}' for k, v in env_extra.items())],
        capture_output=True, text=True, timeout=1200)
    rc, _, peak = child.stdout.strip().partition(' ')
    if child.returncode != 0 or rc != '0':
        raise SystemExit(f"FAIL: {program} {env_extra}: rc {rc}\n"
                         f"{child.stdout}{child.stderr}")
    return int(peak)


def document(path, facts):
    side = 1
    while side * (side - 1) // 2 < facts:
        side += 1
    with open(path, 'w') as out:
        made = 0
        for i in range(side):
            for j in range(i + 1, side):
                if made == facts:
                    break
                out.write(f'(lt {i} {j})\n')
                made += 1
        out.write('!(size-atom (collapse (match &self (lt $i $j) t)))\n')


def main():
    forms, programs = int(sys.argv[1]), sys.argv[2:]
    sliced = {'CETTA_PETTA_SLICE_BYTES': '1', 'CETTA_PETTA_SLICE_FORMS': '3'}
    for program in programs:
        run = cetta([program], sliced)
        with tempfile.NamedTemporaryFile('w', suffix='.out', delete=False) as f:
            f.write(run.stdout + run.stderr)
            actual = f.name
        try:
            agree = subprocess.run(
                [sys.executable, COMPARE, '--lang', 'petta', '--program',
                 program, '--expected', program[:-len('.metta')] + '.expected',
                 '--actual', actual, '--cetta', os.environ['CETTA_BIN'],
                 '--profile', 'extended', '--lang', 'petta'],
                capture_output=True, text=True, timeout=600)
        finally:
            os.unlink(actual)
        if run.returncode != 0 or agree.returncode != 0:
            print(f"FAIL: {program} read in 3-form slices answers otherwise\n"
                  f"{agree.stdout}{agree.stderr}")
            return 1
    growth = {}
    with tempfile.TemporaryDirectory() as directory:
        small = os.path.join(directory, 'facts_small.metta')
        large = os.path.join(directory, 'facts_large.metta')
        document(small, forms)
        document(large, 4 * forms)
        for mode, env in (('whole', {'CETTA_PETTA_SLICE_BYTES':
                                     str(1 << 62)}),
                          ('slices', {'CETTA_PETTA_SLICE_BYTES': '1',
                                      'CETTA_PETTA_SLICE_FORMS':
                                      str(max(1, forms // 4))})):
            growth[mode] = peak_kib(large, env) - peak_kib(small, env)
    if growth['whole'] < FLOOR_KIB:
        print(f"FAIL: read whole the peak grew only {growth['whole']} KiB: "
              f"the test measures nothing")
        return 1
    if 3 * growth['slices'] > growth['whole']:
        print(f"FAIL: read in slices the peak grew {growth['slices']} KiB "
              f"from {forms} to {4 * forms} facts, against "
              f"{growth['whole']} KiB read whole")
        return 1
    print(f"PASS: documents read in slices answer as read whole "
          f"({len(programs)} programs), and their load stays flat: "
          f"{growth['slices']:+d} KiB in slices, {growth['whole']:+d} KiB whole, "
          f"from {forms} to {4 * forms} facts")
    return 0


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == '--one':
        extra = dict(item.split('=', 1) for item in sys.argv[3:])
        done = cetta([sys.argv[2]], extra)
        peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        print(f"{done.returncode} {peak}")
        sys.exit(0)
    sys.exit(main())
