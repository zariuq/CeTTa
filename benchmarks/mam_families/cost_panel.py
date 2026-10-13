#!/usr/bin/env python3
"""Calibrated whole-program instruction panel: a candidate against a baseline.

    cost_panel.py prepare OUT
    cost_panel.py run OUT GROUP BASELINE CANDIDATE [--cpu N] [--shard K/M, K from 0]
    cost_panel.py run OUT apps BASELINE CANDIDATE --cpu N --cases A,B,...
    cost_panel.py run OUT GROUP BASELINE CANDIDATE --cpu N --recheck
                  [--relinks BASELINE_A,BASELINE_B:CANDIDATE_A,CANDIDATE_B]
    cost_panel.py summary OUT

Groups and their comparisons, 367 in all:

  apps         every applicable dialect of the app_panel.py applications (177)
  joins        the cyclic and acyclic joins of join_panel.py, HE and PeTTa (36)
  paths        conjunctive path counts and sums at depths 4, 8, 12, 16 (16)
  rings        tabled rings with seeds, default and extended profiles (16)
  chainers     the chainer rows of scripts/bench_chaining.py (26)
  one-pattern  exact lookup, lookup by a non-leading field, listing and count
               over a native space at 1,024 and 8,192 rows: whole programs (64)
               and the cost of each additional query (32)

Every comparison runs five alternating trials of the baseline, the candidate
and two identical-candidate controls under perf.  Answers are checked against
their oracles before any count is used.  cost_gate.instruction_gate decides
each comparison: the spread of the identical controls is the resolution, never
a blanket allowance.  A baseline that fails its oracle leaves its comparison
unavailable.  Executables must stay in the tree's runtime directory, where
they find their libraries.  Each is measured from a staged copy named by its
digest, so every command has the same length: the executable's path shifts the
alignment of the strings a run reads at startup.

`--cases` measures the named applications again under the same procedure,
and `--recheck` a whole group.  Their results go to OUT/rechecks, apart from
the panel that `summary` reads.  `--relinks` adds the follow-up for flagged
comparisons: the same objects of each side linked in other orders, decided by
`cost_gate.layout_gate`.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
import shutil
from pathlib import Path
import statistics
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ENGINES = ('baseline', 'candidate', 'control_a', 'control_b')
CALIBRATED = ENGINES
RELINKS = {'baseline': ('baseline_relink_a', 'baseline_relink_b'),
           'candidate': ('candidate_relink_a', 'candidate_relink_b')}
ENVIRONMENT = dict(os.environ, LC_ALL='C.UTF-8', CETTA_PETTA_SEARCH_MACHINE='1',
                   CETTA_PETTA_MACHINE_STATS='0')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner = load('paired_runner', HERE / 'run.py')
gate = load('matched_instruction_gate', HERE / 'cost_gate.py')


def sha(data):
    return hashlib.sha256(data if isinstance(data, bytes) else data.encode()).hexdigest()


def sha_file(path):
    return sha(Path(path).read_bytes())


def stage(path, digest):
    """The executable's copy in the runtime directory, named by its digest."""
    staged = ROOT / 'runtime' / f'cetta-panel-{digest[:12]}'
    if not staged.exists() or sha_file(staged) != digest:
        temporary = staged.with_name(f'{staged.name}.{os.getpid()}.partial')
        shutil.copy2(path, temporary)
        temporary.replace(staged)
    return staged


# ------------------------------------------------------------------ inputs --

def ring_program(size, seeds):
    lines = ['!(import! &self (library lib_tabling))', '!(tabled (node $key))']
    lines += [f'(= (node {i}) (empty))' for i in range(size)]
    lines += [f'(= (node {i}) (node {(i + 1) % size}))' for i in range(size)]
    lines += [f'(= (node {s}) (seed {s}))' for s in range(seeds)]
    lines += [f'!(let $value (node {q}) (support {q} $value))' for q in (0, 1)]
    return '\n'.join(lines) + '\n'


RINGS = [(16, 0), (16, 1), (16, 16), (64, 0), (64, 1), (64, 64), (128, 1), (128, 128)]


def path_program(depth, language, kind):
    facts = ''.join(f'(path {i} 0 0 -3 0)\n(path {i} 0 0 5 1)\n' for i in range(depth))
    legs = ' '.join(f'(path {i} $k{i} $k{i + 1} $v{i} $o{i})' for i in range(depth))
    term = '1' if kind == 'count' else f'$v{depth // 2}'
    if language == 'petta':
        query = f'!(foldall + (match &self (, {legs}) {term}) 0)'
    else:
        query = f'!(fold (match &self (, {legs}) {term}) 0 $acc $item (eval (+ $acc $item)))'
    return facts + query + '\n'


def one_pattern_program(shape, size, rounds):
    rows = ''.join(f'!(add-atom &q (row k{i:05d} v{i:05d}))\n' for i in range(size))
    middle = size // 2
    query = {
        'exact': f'!(let $result (collapse (match &q (row k{middle:05d} v{middle:05d}) hit)) '
                 '(println! (probe $result)))',
        'nonleading': f'!(let $result (collapse (match &q (row $key v{middle:05d}) $key)) '
                      '(println! (probe $result)))',
        'list': '!(let $result (collapse (match &q (row $key $value) $value)) '
                '(println! (probe $result)))',
        'count': '!(let $rows (collapse (match &q (row $key $value) $value)) '
                 '(println! (probe (size-atom $rows))))',
    }[shape]
    probe = {
        'exact': '(probe (hit))',
        'nonleading': f'(probe (k{middle:05d}))',
        'list': '(probe (' + ' '.join(f'v{i:05d}' for i in range(size)) + '))',
        'count': f'(probe {size})',
    }[shape]
    return '!(bind! &q (new-space))\n' + rows + (query + '\n') * rounds, probe


CHAINERS = [('dtl_curried', 'he'), ('dtl_curried', 'petta'), ('dtl_nary', 'he'),
            ('dtl_nary', 'petta'), ('nil_ic_calendar', 'petta'), ('nil_metamath_th1', 'he'),
            ('nil_metamath_th1', 'petta'), ('nil_obc_jarr_s13', 'petta'),
            ('nil_obc_loowoz_s19', 'petta'), ('pln_selector', 'he'), ('pln_selector', 'petta'),
            ('roman_transitive_closure', 'he'), ('roman_transitive_closure', 'petta')]


def prepare(out):
    out = Path(out).resolve()
    inputs = out / 'inputs'
    apps = load('app_panel', HERE / 'app_panel.py')
    joins = load('join_panel', HERE / 'join_panel.py')
    for module, name in ((apps, 'apps'), (joins, 'joins')):
        argv = sys.argv
        sys.argv = [name, str(inputs / name), '--scale', 'measure']
        try:
            module.main()
        finally:
            sys.argv = argv
    families = {'rings': [], 'paths': [], 'one-pattern': []}
    for directory in families:
        (inputs / directory).mkdir(parents=True, exist_ok=True)
    for size, seeds in RINGS:
        path = inputs / 'rings' / f'ring-{size}-{seeds}.metta'
        path.write_text(ring_program(size, seeds))
        expected = ['true', 'true'] + [f'(support {q} (seed {s}))'
                                       for q in (0, 1) for s in range(seeds)]
        families['rings'].append(dict(case=f'ring-{size}-{seeds}', source=path.name,
                                      sha256=sha_file(path), expected=expected))
    for depth in (4, 8, 12, 16):
        for language in ('he', 'petta'):
            for kind in ('count', 'sum'):
                path = inputs / 'paths' / f'{depth}-{language}-{kind}.metta'
                path.write_text(path_program(depth, language, kind))
                families['paths'].append(dict(case=f'path-{depth}-{kind}', language=language,
                                              source=path.name, sha256=sha_file(path),
                                              expected=[str(1 << depth)]))
    for shape in ('exact', 'nonleading', 'list', 'count'):
        for size in (1024, 8192):
            for rounds in (4, 20):
                text, probe = one_pattern_program(shape, size, rounds)
                path = inputs / 'one-pattern' / f'{shape}-{size}-{rounds}.metta'
                path.write_text(text)
                families['one-pattern'].append(dict(shape=shape, size=size, rounds=rounds,
                                                    source=path.name, sha256=sha_file(path),
                                                    expected_probe=probe))
    for directory, rows in families.items():
        (inputs / directory / 'manifest.json').write_text(json.dumps(rows, indent=1) + '\n')
    library = {str(path.relative_to(ROOT)): sha_file(path)
               for path in sorted((ROOT / 'lib').rglob('*')) if path.is_file()}
    (out / 'library-identity.json').write_text(json.dumps(library, indent=1) + '\n')
    print('Prepared panel inputs in', inputs)


# ------------------------------------------------------------- measurement --

class Panel:
    def __init__(self, out, group, baseline, candidate, cpu, shard, cases=None,
                 recheck=False, relinks=None):
        self.out = Path(out).resolve()
        self.inputs = self.out / 'inputs'
        self.group = group
        self.cpu = cpu
        self.shard = shard
        self.cases = cases
        suffix = f'-{shard[0]}of{shard[1]}' if shard else ''
        if cases or recheck:
            stamp = time.strftime('%Y%m%d-%H%M%S')
            self.results = self.out / 'rechecks' / f'{group}-cpu{cpu}-{stamp}'
        else:
            self.results = self.out / 'results' / f'{group}{suffix}'
        self.results.mkdir(parents=True, exist_ok=False)
        self.binaries = {'baseline': Path(baseline).resolve(), 'candidate': Path(candidate).resolve()}
        self.binaries['control_a'] = self.binaries['control_b'] = self.binaries['candidate']
        self.relinks = bool(relinks)
        for side, paths in (relinks or {}).items():
            for label, path in zip(RELINKS[side], paths):
                self.binaries[label] = Path(path).resolve()
        self.identity = {label: sha_file(path) for label, path in self.binaries.items()}
        self.binaries = {label: stage(path, self.identity[label])
                         for label, path in self.binaries.items()}
        library = json.loads((self.out / 'library-identity.json').read_text())
        changed = [name for name, value in library.items() if sha_file(ROOT / name) != value]
        assert not changed, ('libraries changed since prepare', changed)
        self.cells = []
        os.sched_setaffinity(0, {cpu})

    def publish(self):
        (self.results / 'results.json').write_text(json.dumps(dict(
            group=self.group, shard=self.shard, cases=self.cases, cpu=self.cpu, trials=5,
            binaries={label: dict(path=str(path), sha256=self.identity[label])
                      for label, path in self.binaries.items()},
            cells=self.cells), indent=1) + '\n')

    def cell(self, key, command, cwd, check, timeout=60):
        """Five alternating trials per engine; `check` maps stdout to
        (correct, observation)."""
        samples = {label: {} for label in ENGINES}
        refused = False
        stem = self.results / 'runs' / '-'.join(str(part) for part in key)
        for trial in range(5):
            for label in ENGINES if trial % 2 == 0 else ENGINES[::-1]:
                if label == 'baseline' and refused:
                    continue
                record = runner.paired_measure(command(self.binaries[label]), cwd, ENVIRONMENT,
                                               stem / f'{label}-{trial}', timeout, self.cpu, True)
                try:
                    correct, observation = check(Path(record['stdout_file']).read_text())
                except (ValueError, UnicodeError, OSError):
                    correct, observation = False, None
                correct = correct and record['rc'] == 0 and not record['timed_out']
                record.update(correct=correct, trial=trial,
                              observation_sha256=sha(json.dumps(observation, ensure_ascii=False)))
                samples[label][trial] = record
                if label != 'baseline':
                    assert correct and record['instructions'], (key, label, record)
                elif not correct:
                    refused = True
        return samples, refused

    def decide(self, key, samples, refused, **extra):
        medians = {label: statistics.median(r['instructions'] for r in samples[label].values())
                   for label in ENGINES if samples[label] and not (label == 'baseline' and refused)}
        cell = dict(key=list(key), medians=medians, **extra)
        if refused:
            cell.update(ratio=None, calibrated=dict(status='unavailable', accepted=False,
                        reason='The baseline fails the oracle.'))
        else:
            cell.update(ratio=medians['candidate'] / medians['baseline'],
                        calibrated=gate.instruction_gate(*[samples[label] for label in CALIBRATED]))
            if self.relinks:
                cell.update(layout=gate.layout_gate(
                    [samples[label] for label in ('baseline', *RELINKS['baseline'])],
                    [samples[label] for label in ('candidate', *RELINKS['candidate'])],
                    samples['control_a'], samples['control_b']))
        self.cells.append(cell)
        self.publish()
        status = cell['calibrated']['status']
        layout = cell.get('layout')
        print(*key, status, '' if cell['ratio'] is None else format(cell['ratio'], '.6f'),
              *(() if not layout else
                ('layout:', layout['status'], format(layout['median_percent'], '+.4f') + '%')),
              flush=True)
        return cell


def ordered_or_bag(expected, preamble, contract, he):
    def check(text):
        answers = runner.answer_atoms(text, he)
        correct = answers[:len(preamble)] == preamble
        answers = answers[len(preamble):]
        correct &= not any(atom.startswith('(Error ') for atom in answers)
        if contract == 'occurrence-bag':
            correct &= Counter(answers) == Counter(expected)
            return correct, sorted(answers)
        correct &= answers == expected
        return correct, answers
    return check


def run_apps(panel):
    corpus = panel.inputs / 'apps'
    cases = []
    for case in json.loads((corpus / 'manifest.json').read_text()):
        for language in ('petta', 'he'):
            if language in case['dialects']:
                cases.append((case, language))
    if panel.shard:
        cases = cases[panel.shard[0]::panel.shard[1]]
    if panel.cases:
        unknown = set(panel.cases) - {case['case'] for case, _ in cases}
        assert not unknown, ('unknown applications', sorted(unknown))
        cases = [(case, language) for case, language in cases if case['case'] in panel.cases]
    for case, language in cases:
        variant = case['dialects'][language]
        source = corpus / variant['source']
        expected_path = corpus / variant.get('expected', case['expected'])
        assert sha_file(source) == variant['sha256']
        expected = runner.answer_atoms(expected_path.read_text(), False)
        preamble = [runner.alpha_canonicalize_atom_text(x) for x in variant.get('preamble', [])]
        contract = variant.get('contract', case['contract'])
        profile = variant.get('profile')
        command = lambda binary: [str(binary), '--lang', language, *(['--profile', profile] if profile else []),
                                  str(source)]
        key = (case['case'], language)
        # A hang guard only: the heaviest HE cells run about half a minute
        # unloaded, and the panel shares the machine with its own gates.
        timeout = 300
        samples, refused = panel.cell(key, command, corpus,
                                      ordered_or_bag(expected, preamble, contract, language == 'he'), timeout)
        panel.decide(key, samples, refused, profile=profile or 'default')


def run_joins(panel):
    corpus = panel.inputs / 'joins'
    for case in json.loads((corpus / 'manifest.json').read_text()):
        for language in ('he', 'petta'):
            variant = case['dialects'][language]
            source = corpus / variant['source']
            assert sha_file(source) == variant['sha256']
            expected = runner.answer_atoms(
                (corpus / variant.get('expected', case['expected'])).read_text(), False)
            profile = variant['profile']
            command = lambda binary: [str(binary), '--lang', language,
                                      *(['--profile', profile] if profile != 'default' else []),
                                      str(source)]
            key = (case['case'], language)
            samples, refused = panel.cell(key, command, ROOT,
                                          ordered_or_bag(expected, [], 'occurrence-bag', language == 'he'))
            panel.decide(key, samples, refused, profile=profile)


def run_listed(panel, directory, language_of, profiles):
    corpus = panel.inputs / directory
    for row in json.loads((corpus / 'manifest.json').read_text()):
        source = corpus / row['source']
        assert sha_file(source) == row['sha256']
        language = language_of(row)
        for profile in profiles:
            command = lambda binary: [str(binary), '--lang', language,
                                      *(['--profile', profile] if profile != 'default' else []),
                                      str(source)]
            key = (row['case'], language, profile)
            samples, refused = panel.cell(key, command, ROOT,
                                          ordered_or_bag(row['expected'], [], 'occurrence-bag', language == 'he'))
            panel.decide(key, samples, refused, profile=profile)


def run_chainers(panel):
    sys.path.insert(0, str(ROOT / 'scripts'))
    import bench_chaining as bench
    manifest = bench.load_manifest()
    scratch = panel.results / 'programs'
    scratch.mkdir()
    for case, language in CHAINERS:
        row = manifest[case]
        source = bench.compose_program(row, scratch)
        for profile in ('default', 'extended'):
            samples = {label: {} for label in ENGINES}
            results = []
            for trial in range(5):
                for label in ENGINES if trial % 2 == 0 else ENGINES[::-1]:
                    name = '-'.join((case, language, profile, label))
                    result = bench.run_engine(name, language, row, source, trial, scratch,
                                              panel.binaries[label], ROOT.parent / 'PeTTa',
                                              Path('/usr/lib'), measure_instructions=True,
                                              profile=profile)
                    result.update(variant=label, trial=trial)
                    results.append(result)
            bench.qualify(row, results)
            for result in results:
                result.update(correct=True, rc=0, timed_out=False,
                              observation_sha256=result['sha256'])
                samples[result['variant']][result['trial']] = {
                    key: value for key, value in result.items() if key != 'normalized'}
            panel.decide((case, language, profile), samples, False, profile=profile)


def run_one_pattern(panel):
    corpus = panel.inputs / 'one-pattern'
    programs = json.loads((corpus / 'manifest.json').read_text())
    for language in ('petta', 'he'):
        for profile in ('default', 'extended'):
            for size in (1024, 8192):
                for shape in ('exact', 'nonleading', 'list', 'count'):
                    by_rounds = {}
                    for rounds in (4, 20):
                        program = next(p for p in programs if (p['shape'], p['size'], p['rounds']) ==
                                       (shape, size, rounds))
                        source = corpus / program['source']
                        assert sha_file(source) == program['sha256']

                        def check(text, rounds=rounds, oracle=program['expected_probe']):
                            lines = text.splitlines()
                            probes = [line.strip() for line in lines if line.strip().startswith('(probe ')]
                            rest = '\n'.join(line for line in lines if not line.strip().startswith('(probe '))
                            return probes == [oracle] * rounds, [probes, runner.answer_atoms(rest, language == 'he')]
                        command = lambda binary: [str(binary), '--lang', language,
                                                  *(['--profile', profile] if profile != 'default' else []),
                                                  str(source)]
                        key = (language, profile, size, shape, rounds)
                        samples, refused = panel.cell(key, command, ROOT, check)
                        panel.decide(key, samples, refused, profile=profile)
                        by_rounds[rounds] = (samples, refused)
                    if by_rounds[4][1] or by_rounds[20][1]:
                        panel.decide((language, profile, size, shape, 'per-query'), {}, True)
                        continue
                    deltas = {}
                    for label in ENGINES:
                        deltas[label] = {}
                        for trial in range(5):
                            short = by_rounds[4][0][label][trial]
                            long = by_rounds[20][0][label][trial]
                            delta = dict(long, instructions=long['instructions'] - short['instructions'])
                            delta['observation_sha256'] = sha(json.dumps(
                                [short['observation_sha256'], long['observation_sha256']]))
                            deltas[label][trial] = delta
                    panel.decide((language, profile, size, shape, 'per-query'), deltas, False,
                                 profile=profile)


GROUPS = {
    'apps': run_apps,
    'joins': run_joins,
    'paths': lambda panel: run_listed(panel, 'paths', lambda row: row['language'], ['extended']),
    'rings': lambda panel: run_listed(panel, 'rings', lambda row: 'petta', ['default', 'extended']),
    'chainers': run_chainers,
    'one-pattern': run_one_pattern,
}
EXPECTED_CELLS = {'apps': 177, 'joins': 36, 'paths': 16, 'rings': 16, 'chainers': 26, 'one-pattern': 96}


def summary(out):
    cells = []
    for path in sorted((Path(out) / 'results').glob('*/results.json')):
        data = json.loads(path.read_text())
        cells += [dict(cell, group=data['group']) for cell in data['cells']]
    count = Counter(cell['calibrated']['status'] for cell in cells)
    by_group = Counter(cell['group'] for cell in cells)
    print('comparisons:', len(cells), dict(by_group))
    print('decisions:', dict(count))
    for cell in cells:
        if not cell['calibrated'].get('accepted'):
            print(' ', cell['group'], *cell['key'], cell['calibrated']['status'],
                  cell['calibrated'].get('reason', ''), cell.get('ratio'))
    missing = {group: expected - by_group.get(group, 0)
               for group, expected in EXPECTED_CELLS.items() if by_group.get(group, 0) != expected}
    if missing:
        print('incomplete groups:', missing)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('prepare').add_argument('out')
    run = commands.add_parser('run')
    run.add_argument('out')
    run.add_argument('group', choices=sorted(GROUPS))
    run.add_argument('baseline')
    run.add_argument('candidate')
    run.add_argument('--cpu', type=int, required=True)
    run.add_argument('--shard', help='K/M: application shard K of M, counting from 0')
    run.add_argument('--cases', help='A,B,...: measure these applications again')
    run.add_argument('--recheck', action='store_true', help='measure the group again')
    run.add_argument('--relinks', help='BASELINE_A,BASELINE_B:CANDIDATE_A,CANDIDATE_B')
    commands.add_parser('summary').add_argument('out')
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.out)
    elif args.command == 'summary':
        summary(args.out)
    else:
        shard = tuple(int(x) for x in args.shard.split('/')) if args.shard else None
        if shard and not (len(shard) == 2 and 0 <= shard[0] < shard[1]):
            parser.error('--shard K/M needs 0 <= K < M')
        cases = args.cases.split(',') if args.cases else None
        if cases and args.group != 'apps':
            parser.error('--cases selects applications of the apps group')
        relinks = None
        if args.relinks:
            if not (cases or args.recheck):
                parser.error('--relinks follows up a recheck')
            baselines, candidates = (part.split(',') for part in args.relinks.split(':'))
            if len(baselines) != 2 or len(candidates) != 2:
                parser.error('--relinks names two builds per side')
            relinks = dict(baseline=baselines, candidate=candidates)
            global ENGINES
            ENGINES = CALIBRATED + RELINKS['baseline'] + RELINKS['candidate']
        panel = Panel(args.out, args.group, args.baseline, args.candidate, args.cpu, shard, cases,
                      args.recheck, relinks)
        GROUPS[args.group](panel)
        if not shard and not cases:
            assert len(panel.cells) == EXPECTED_CELLS[args.group], (len(panel.cells), args.group)
        print('measurement complete:', len(panel.cells), 'comparisons;',
              Counter(cell['calibrated']['status'] for cell in panel.cells))


if __name__ == '__main__':
    main()
