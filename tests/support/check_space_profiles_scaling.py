#!/usr/bin/env python3
"""Check that a strict stack keeps no structure beyond its order.

usage: check_space_profiles_scaling.py ITEMS

A stack takes ITEMS items, and in a second run four times as many; some
runs then make general matches against it, each with at most one answer.
From one size to the other, general matches may grow a strict stack's peak
memory by at most 24 bytes an added item more than it grows with no match
at all: they build no index, and the bound leaves room for garbage a
collection has not yet reclaimed.  Under the same matches a full stack's
must grow by at least 64 bytes an added item more than the strict one's,
its indexes, or the test measures nothing.  The binary is $CETTA_BIN.

A sanitized binary runs with its quarantine off: the freed blocks it holds
back for checking would count as growth (general matches' transient
allocations grew a strict stack by a further 38 bytes an item that way)."""
import os
import resource
import subprocess
import sys
import tempfile

STRICT_BYTES_PER_ITEM = 24
INDEX_BYTES_PER_ITEM = 64


def program(path, profile, items, matches):
    with open(path, 'w') as out:
        out.write(f'!(bind! &stack (new-space stack {profile}))\n')
        out.write('(= (fill $k $n) (if (>= $k $n) () '
                  '(let $t (% $k 7) (let $_ (space-push &stack (item $k (tag $t))) '
                  '(fill (+ $k 1) $n)))))\n')
        out.write(f'!(fill 0 {items})\n')
        for k in range(matches):
            out.write(f'!(collapse (match &stack (item {k * 37} $t) $t))\n')
            out.write(f'!(collapse (match &stack (item $k (tag {7 + k})) found))\n')
        out.write('!(space-len &stack)\n')


def peak_kib(path):
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = ':'.join(
        part for part in (env.get('ASAN_OPTIONS', ''), 'quarantine_size_mb=0') if part)
    child = subprocess.run(
        [sys.executable, __file__, '--one', path],
        capture_output=True, text=True, timeout=1200, env=env)
    rc, _, rest = child.stdout.strip().partition(' ')
    peak, _, last = rest.partition(' ')
    if child.returncode != 0 or rc != '0':
        raise SystemExit(f"FAIL: {path}: rc {rc}\n{child.stdout}{child.stderr}")
    return int(peak), last


def main():
    items = int(sys.argv[1])
    growth = {}
    with tempfile.TemporaryDirectory() as directory:
        for profile, matches in (('strict', 0), ('strict', 3), ('full', 3)):
            peaks = []
            for size in (items, 4 * items):
                path = os.path.join(directory, f'{profile}_{matches}_{size}.metta')
                program(path, profile, size, matches)
                peak, last = peak_kib(path)
                if last != str(size):
                    print(f"FAIL: {profile} stack of {size} items read "
                          f"{last!r} as its length")
                    return 1
                peaks.append(peak)
            growth[profile, matches] = peaks[1] - peaks[0]
    plain = growth['strict', 0]
    strict = growth['strict', 3]
    full = growth['full', 3]
    added = 3 * items
    if full - strict < added * INDEX_BYTES_PER_ITEM // 1024:
        print(f"FAIL: a full stack grew only {full - strict} KiB more than a "
              f"strict one over {added} added items: the test measures nothing")
        return 1
    if strict > plain + added * STRICT_BYTES_PER_ITEM // 1024:
        print(f"FAIL: general matches grew a strict stack by {strict} KiB "
              f"against {plain} KiB without them, from {items} to "
              f"{4 * items} items")
        return 1
    print(f"PASS: general matches build no index in a strict stack: "
          f"+{strict} KiB with them, +{plain} KiB without, +{full} KiB full, "
          f"from {items} to {4 * items} items")
    return 0


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--one':
        done = subprocess.run(
            [os.environ['CETTA_BIN'], '--profile', 'extended', '--lang', 'petta',
             sys.argv[2]], capture_output=True, text=True, timeout=1200)
        peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        lines = done.stdout.strip().splitlines()
        print(f"{done.returncode} {peak} {lines[-1] if lines else ''}")
        sys.exit(0)
    sys.exit(main())
