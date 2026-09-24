#!/usr/bin/env python3
"""Run frozen MAM family cases on a CeTTa binary and on SWI-PeTTa.

Usage: run.py CASEDIR OUTDIR CETTA_BINARY [--engines cetta,swi] [--baseline BINARY]
              [--trials N] [case ...]

Engines: `cetta` (the binary as given), `canonical` (the same binary with every
compiled route off: a transition budget, however large, sends every call through
canonical search), `baseline` (another CeTTa binary) and `swi` (SWI-PeTTa).

Full process measurement (perf stat instructions:u, cycles:u; /usr/bin/time wall,
user, peak RSS).  An engine's answers are its stdout lines after the case's header
lines; they must equal <case>.expected exactly (order and multiplicity).
"""
import hashlib, json, os, subprocess, sys
from pathlib import Path

PETTA = Path('/home/aimama/aihub/hyperon/PeTTa')
ROOT = Path('/home/aimama/aihub/hyperon/cetta-shared-opt-audit-20260807')

def measure(label, cmd, cwd, out, timeout, env=None):
    perf, tm, so = out/f'{label}.perf', out/f'{label}.time', out/f'{label}.out'
    full = ['perf', 'stat', '-x', ',', '-e', 'instructions:u,cycles:u', '-o', str(perf), '--',
            '/usr/bin/time', '-f', '%e %U %S %M', '-o', str(tm), *cmd]
    try:
        with so.open('wb') as o, (out/f'{label}.err').open('wb') as e:
            rc = subprocess.run(full, cwd=cwd, stdout=o, stderr=e, timeout=timeout,
                                env=env).returncode
    except subprocess.TimeoutExpired:
        return dict(rc='timeout')
    wall, user, _sys, rss = tm.read_text().split()[-4:]
    counts = {}
    for l in perf.read_text().splitlines():
        f = l.split(',')
        if len(f) > 2 and f[0].isdigit(): counts[f[2]] = int(f[0])
    return dict(rc=rc, wall=float(wall), user=float(user), rss_kib=int(rss),
                instructions=counts.get('instructions:u'), cycles=counts.get('cycles:u'),
                stdout=so.read_text(errors='replace'))

def main():
    args = sys.argv[1:]
    def opt(name, default):
        if name in args:
            i = args.index(name); v = args[i+1]; del args[i:i+2]; return v
        return default
    engines = opt('--engines', 'cetta,swi').split(',')
    trials = int(opt('--trials', '1'))
    baseline = opt('--baseline', None)
    timeout = int(opt('--timeout', '600'))
    cases_dir, out, cetta = Path(args[0]), Path(args[1]), Path(args[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = {r['case']: r for r in json.loads((cases_dir/'manifest.json').read_text())}
    names = args[3:] or list(manifest)
    rows = []
    for case in names:
        m = manifest[case]; src = (cases_dir/f'{case}.metta').resolve()
        expected = (cases_dir/f'{case}.expected').read_text().splitlines()
        for t in range(trials):
            for eng in (engines if t % 2 == 0 else list(reversed(engines))):
                env = None
                if eng == 'swi':
                    cmd, cwd = ['./run.sh', str(src), '--silent'], PETTA
                else:
                    binary = Path(baseline).resolve() if eng == 'baseline' else cetta
                    cmd, cwd = [str(binary), '--lang', 'petta', str(src)], ROOT
                    if eng == 'canonical':
                        env = dict(os.environ,
                                   CETTA_PETTA_MACHINE_TRANSITION_LIMIT='18446744073709551615')
                r = measure(f'{case}-{eng}-{t}', cmd, cwd, out, timeout, env)
                lines = r.pop('stdout', '').splitlines()
                got = lines[m['header_lines']:]
                r.update(case=case, engine=eng, trial=t, correct=(r['rc'] == 0 and got == expected),
                         answers=len(got))
                rows.append(r)
                print(f"{case:40s} {eng:9s} t{t} ok={r['correct']!s:5s} "
                      f"{(r.get('instructions') or 0)/1e9:9.3f}G {r.get('wall', 0):7.2f}s "
                      f"{r.get('rss_kib', 0)/1024:8.1f}MB rc={r['rc']}", flush=True)
    (out/'results.json').write_text(json.dumps(dict(
        cetta=str(cetta), cetta_sha256=hashlib.sha256(cetta.read_bytes()).hexdigest(),
        baseline=baseline,
        rows=rows), indent=1) + '\n')

if __name__ == '__main__':
    main()
