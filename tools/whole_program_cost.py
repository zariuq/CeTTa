"""Whole-program cost gate: instructions of HE programs on a candidate executable
against the baseline executable, with the same answers.

usage: whole_program_cost.py <baseline-executable> <candidate-executable> [tree]

Each executable runs RUNS times (default 5), because instruction counts move by
up to about 0.2% from run to run. A row is flagged when the candidate's best
run needs more instructions than the baseline's worst run, or when its answers
differ; the baseline's own spread is printed and is the resolution of the
measurement. Exit status 1 if any row is flagged. Paths are relative to the
tree (default: the tree that holds this tool)."""
import os, re, signal, subprocess, sys
import json
from hashlib import sha256
from pathlib import Path

TREE = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else Path(__file__).resolve().parents[1]
BASE, CAND = (Path(a) if os.path.isabs(a) else TREE / a for a in sys.argv[1:3])
BASE_IDENTITY = sha256(BASE.read_bytes()).hexdigest()
CAND_IDENTITY = sha256(CAND.read_bytes()).hexdigest()
LIMIT = int(os.environ.get('COST_TIMEOUT', '600'))
RUNS = int(os.environ.get('COST_RUNS', '5'))
AVAILABLE_CPUS = os.sched_getaffinity(0)
CPU = int(os.environ.get('COST_CPU', str(min(AVAILABLE_CPUS))))
if CPU not in AVAILABLE_CPUS:
    raise SystemExit('COST_CPU must select a processor available to this process.')
WORK = Path(os.environ.get('COST_SCRATCH', str(TREE / 'runtime/he-module-cost')))
WORK.mkdir(parents=True, exist_ok=True)
PERF = WORK / f'whole-program-cost-{os.getpid()}.perf'
if RUNS < 2:
    raise SystemExit('At least two runs are required to measure baseline variability.')

PROGRAMS = [
    'bench_backchain_he', 'bc_depth10_spine_regression', 'bench_foldallmatch_he',
    'bench_foldallspacecount_he', 'bench_unique_he', 'bench_conjunction_he',
    'bench_matchjoin_he', 'bench_matchjoin3_he', 'bench_matchjoin8_he',
    'bench_duplicate_conjunction_he', 'bench_conjunction12_he', 'bench_nqueens8_he',
    'bench_fc_d3_count', 'bench_fc_d3_nodup_count',
]
ROWS = [(profile, f'benchmarks/{name}.metta') for profile in ('extended', 'he-compat') for name in PROGRAMS
        if not (profile == 'he-compat' and name.startswith('bench_fc_'))]
if os.environ.get('COST_STATS') == '1':
    ROWS = [('extended', 'benchmarks/bench_term_sharing_stress.metta')]
# Forward chaining needs the extended snapshot operation. Term-sharing is
# qualified separately with identified statistics builds on both sides.
# Module workloads have the same answer/effect comparison as whole programs.
MODULE_ROWS = [] if os.environ.get('COST_STATS') == '1' else [
    ('he', f'tests/he/module_cost_v1/{name}.metta') for name in
    ('startup', 'imported-open-call', 'imported-join', 'repeated-load', 'repeated-failure')]


def run(binary, profile, program):
    cmd = ['taskset', '-c', str(CPU), 'perf', 'stat', '-x,', '-e', 'instructions:u', '-o', str(PERF), '--',
           str(binary), '--lang', 'he', '--profile', profile, str(TREE / program)]
    p = subprocess.Popen(cmd, cwd=TREE, start_new_session=True, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True,
                         env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
    try:
        out, err = p.communicate(timeout=LIMIT)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)      # perf and the program it started
        except ProcessLookupError:
            pass
        p.communicate()
        return None, {'diagnostic': 'timeout; no language result inferred'}
    count = None
    for line in PERF.read_text().splitlines():
        field = line.split(',')
        if len(field) > 2 and field[2].startswith('instructions'):
            count = float(field[0])
    return count, (p.returncode, re.sub(r'0x[0-9a-f]+', '0xADDR', out), err)


flagged = 0
records = []
print(f'baseline  {BASE}\ncandidate {CAND}\n{RUNS} runs each; flag = candidate\'s best run above baseline\'s worst run, or different answers')
print(f'{"profile":10s} {"program":44s} {"baseline":>10s} {"spread":>8s} {"candidate":>10s} {"change":>8s}  answers')
selected = os.environ.get('COST_PROGRAMS')
for profile, program in ROWS + MODULE_ROWS:
    if selected and Path(program).stem not in selected.split(','):
        continue
    if not (TREE / program).exists():
        raise SystemExit(f'Missing workload: {program}')
    base, cand = [], []
    for iteration in range(RUNS):
        # Alternate the execution order so batch drift does not consistently
        # favor one side. Keep both observations from every pair.
        if iteration % 2:
            cand.append(run(CAND, profile, program))
            base.append(run(BASE, profile, program))
        else:
            base.append(run(BASE, profile, program))
            cand.append(run(CAND, profile, program))
    name = Path(program).stem
    if not all(c for c, _ in base + cand):
        print(f'{profile:10s} {name:44s} FLAG (a run did not finish)')
        flagged += 1
        records.append(dict(profile=profile, program=program, baseline=base, candidate=cand, flagged=True))
        continue
    b = sorted(c for c, _ in base); c = sorted(c for c, _ in cand)
    same = all(observation == base[0][1] for _, observation in base + cand)
    valid = all(observation[0] == 0 for _, observation in base + cand)
    flag = c[0] > b[-1] or not same or not valid
    records.append(dict(profile=profile, program=program, baseline=base, candidate=cand, flagged=flag,
                        baseline_spread=(b[-1]-b[0])/b[0], same_observations=same, valid_exit=valid))
    flagged += flag
    print(f'{profile:10s} {name:44s} {b[RUNS//2]/1e6:9.1f}M {100*(b[-1]-b[0])/b[0]:7.3f}% {c[RUNS//2]/1e6:9.1f}M '
          f'{100*(c[RUNS//2]/b[RUNS//2]-1):+7.2f}%  {"same" if same else "differ"}{"   FLAG" if flag else ""}', flush=True)
receipt_path = Path(os.environ.get('COST_RECEIPT', str(WORK / f'cost-{os.getpid()}.json')))
identity_changed = dict(
    baseline=sha256(BASE.read_bytes()).hexdigest() != BASE_IDENTITY,
    candidate=sha256(CAND.read_bytes()).hexdigest() != CAND_IDENTITY)
if any(identity_changed.values()):
    print('FLAG: an executable changed during measurement; the panel is invalid.')
    flagged += 1
with receipt_path.open('x') as stream:
    json.dump(dict(baseline_sha256=BASE_IDENTITY,
                   candidate_sha256=CAND_IDENTITY,
                   executable_identity_changed=identity_changed,
                   driver_sha256=sha256(Path(__file__).read_bytes()).hexdigest(),
                   runs=RUNS, cpu=CPU, sampling='interleaved alternating pairs',
                   selected_programs=selected, records=records, flagged=flagged), stream, indent=2)
print(f'Receipt: {receipt_path}')
print(f'{flagged} row(s) flagged')
sys.exit(1 if flagged else 0)
