"""Run a CeTTa program at several sizes on a runtime-statistics build and
report its resource ledger: high-water marks, cumulative counts and the gauges
left live at the end, each next to peak RSS.

usage: resource_ledger_probe.py BIN LANG TEMPLATE EXPECTED N [N ...]

TEMPLATE is a .metta file containing the token @N@ for the iteration count;
EXPECTED is the last line its run must print.  Every run must exit zero, print
EXPECTED and report every ledger counter, or the probe fails: a counter a
build does not report is not a measured zero.

Differences between consecutive sizes, divided by the difference in N, give
a slope per iteration.  A slope is a diagnostic, not an exact charge: garbage
collection, capacity steps and peaks reached at different moments can bend
it, so compare several sizes before reading one as sustained retention.  The
categories overlap (a cursor's arenas are also arenas), so they are not
added together into one heap census, and live payload, reserved arena bytes
and process RSS are separate quantities."""
import os
import re
import subprocess
import sys
import tempfile

PEAKS = [
    'open-equation-cursor-live-peak',
    'arena-ledger-cursor-live-peak',
    'arena-ledger-cursor-held-bytes-peak',
    'arena-ledger-eval-held-bytes-peak',
    'arena-ledger-scratch-held-bytes-peak',
    'arena-ledger-survivor-held-bytes-peak',
    'arena-ledger-persistent-held-bytes-peak',
    'arena-ledger-other-held-bytes-peak',
    'arena-ledger-space-held-bytes-peak',
    'arena-stock-bytes-peak',
    'petta-choice-depth-peak',
    'space-live-peak',
    'space-struct-bytes-peak',
    'frame-identity-live-peak',
]
CUMULATIVE = [
    'space-init-heap-bytes',
    'frame-identity-mint',
    'frame-identity-free',
    'fresh-var-base-mint',
    'symbol-table-entries',
]
FINAL = [
    'open-equation-cursor-live',
    'arena-ledger-cursor-live',
    'arena-ledger-cursor-held-bytes',
    'space-live',
    'frame-identity-live',
]


def run(binary, lang, template, expected, n):
    text = open(template).read().replace('@N@', str(n))
    with tempfile.NamedTemporaryFile('w', suffix='.metta', delete=False,
                                     dir=os.path.dirname(os.path.abspath(template))) as f:
        f.write(text)
        path = f.name
    try:
        p = subprocess.run(['/usr/bin/time', '-f', 'maxrss=%M', binary, '--lang', lang,
                            '--emit-runtime-stats', path],
                           capture_output=True, text=True, timeout=3600)
    finally:
        os.unlink(path)
    counters = {}
    for line in p.stderr.splitlines():
        m = re.match(r'runtime-counter (\S+) (-?\d+)$', line)
        if m:
            counters[m.group(1)] = int(m.group(2))
        m = re.match(r'maxrss=(\d+)$', line)
        if m:
            counters['maxrss-bytes'] = int(m.group(1)) * 1024
    output = [line for line in p.stdout.splitlines() if line.strip()]
    if p.returncode != 0:
        sys.exit(f'probe failed: exit {p.returncode} at N={n}\n{p.stderr[-800:]}')
    if not output or output[-1].strip() != expected:
        sys.exit(f'probe failed: at N={n} the program printed {output[-1:]!r}, '
                 f'not {expected!r}')
    if counters.get('runtime-statistics-recorded') != 1:
        sys.exit(f'probe failed: {binary} does not record runtime statistics '
                 '(build it with ENABLE_RUNTIME_STATS=1)')
    missing = [name for name in PEAKS + CUMULATIVE + FINAL + ['maxrss-bytes']
               if name not in counters]
    if missing:
        sys.exit(f'probe failed: at N={n} no value for {", ".join(missing)} '
                 '(a build without runtime statistics, or an older ledger)')
    return counters


def main():
    if len(sys.argv) < 7:
        sys.exit(__doc__)
    binary, lang, template, expected = sys.argv[1:5]
    sizes = sorted(int(arg) for arg in sys.argv[5:])
    runs = [run(binary, lang, template, expected, n) for n in sizes]
    header = f'{"counter":42}' + ''.join(f'{"N=" + str(n):>14}' for n in sizes)
    header += ''.join(f'{"slope " + str(a) + "-" + str(b):>16}'
                      for a, b in zip(sizes, sizes[1:]))
    for title, names in (('high-water marks', PEAKS),
                         ('cumulative', CUMULATIVE),
                         ('live at the end', FINAL),
                         ('process', ['maxrss-bytes'])):
        print(f'-- {title}')
        print(header)
        for name in names:
            values = [r[name] for r in runs]
            row = f'{name:42}' + ''.join(f'{v:>14,}' for v in values)
            row += ''.join(f'{(values[i + 1] - values[i]) / (sizes[i + 1] - sizes[i]):>16,.1f}'
                           for i in range(len(sizes) - 1))
            print(row)


if __name__ == '__main__':
    main()
