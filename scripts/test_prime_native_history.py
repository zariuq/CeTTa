#!/usr/bin/env python3
"""Ordered native history, incomplete capture, and retained-scope controls."""
import argparse
import atexit
import datetime
import hashlib
import json
import os
import subprocess
from pathlib import Path
from test_prime_native_control import parse, observations

parser = argparse.ArgumentParser()
parser.add_argument('--runtime', required=True)
parser.add_argument('--receipt-dir', required=True)
parser.add_argument('--timeout', type=float, default=60)
args = parser.parse_args()
w = Path(__file__).resolve().parent.parent
b = Path(args.runtime).resolve()
d = Path(args.receipt_dir).resolve()
d.mkdir(parents=True, exist_ok=True)
native_binary = Path(os.environ.get('CETTA_WRAPPED_BIN', str(b))).resolve()
meta = {'executed_binary': str(native_binary),
        'sha256': hashlib.sha256(native_binary.read_bytes()).hexdigest(),
        'launcher': str(b), 'launcher_sha256': hashlib.sha256(b.read_bytes()).hexdigest()}
base = '(= (history-coin) 3)\n(= (history-coin) 3)\n(= (history-coin) 6)\n(= (history-pair $x) (Pair $x $x))\n'
body = '(history-pair (history-coin))'
history = '(history (selections 512) (nodes 32) (bytes 65536) (owners 10000))'

def policy(width, budget, h=True):
    return f"(search-policy (control FIFO (demand all) (retain True) (workers {width}) (budget {budget}) {(history if h else '')}))"
pattern = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t) (history $h))'
plain = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
runs = []
checks = []

def tagged(x, tag):
    if isinstance(x, list):
        if x and x[0] == tag:
            return x
        for v in x:
            result = tagged(v, tag)
            if result:
                return result
    return None

def normalize(history):
    """Rename allocated IDs jointly, preserving ordering and alias partitions."""
    ids = {}

    def key(role, value):
        if value == 0:
            return 0
        mapping = ids.setdefault(role, {})
        if value not in mapping:
            mapping[value] = len(mapping) + 1
        return mapping[value]

    def walk(x):
        if not isinstance(x, list):
            return x
        y = list(x)
        if y and y[0] == 'Selection':
            y[2] = key('occurrence', y[2])
        if y and y[0] == 'NeedGraph':
            y[1] = key('receipt-session', y[1])
            y[2] = key('node', y[2])
        if y and y[0] == 'NeedNode':
            for i in (1, 2, 3):
                y[i] = key('node', y[i])
        if y and y[0] == 'NeedEvent':
            for i, role in ((2, 'need-session'), (3, 'cell'), (4, 'source'), (6, 'row'), (7, 'production')):
                y[i] = key(role, y[i])
        if y and y[0] == 'Factor':
            y[1] = key('source', y[1])
            y[2] = key('source', y[2])
        return [walk(z) for z in y]
    return walk(history)

def run(name, query, definitions=base, language='prime', source_path=None):
    p = Path(source_path) if source_path else d / (name + '.metta')
    if not source_path:
        p.write_text(definitions + query + '\n')
    cmd = [str(b), '--lang', language] + (['--profile', 'extended'] if language != 'prime' else []) + [str(p)]
    try:
        r = subprocess.run(cmd, cwd=w, capture_output=True, text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired as e:
        def decoded(value):
            return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
        (d / (name + '.stdout')).write_text(decoded(e.stdout))
        (d / (name + '.stderr')).write_text(decoded(e.stderr))
        runs.append(dict(name=name, command=cmd, timeout=True,
                         source_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
                         native_exit=None, observations=[]))
        return []
    (d / (name + '.stdout')).write_text(r.stdout)
    (d / (name + '.stderr')).write_text(r.stderr)
    try:
        o = list(observations(parse(r.stdout)))
    except (IndexError, ValueError, TypeError) as e:
        o = []
        (d / (name + '.parse-error')).write_text(repr(e) + '\n')
    runs.append(dict(name=name, command=cmd, source_sha256=hashlib.sha256(p.read_bytes()).hexdigest(), native_exit=r.returncode, observations=o))
    return o


finished = False


def save_receipt():
    result = dict(at=datetime.datetime.now().astimezone().isoformat(),
                  binary=meta, runs=runs, checks=checks, finished=finished,
                  passed=finished and all(x['passed'] for x in checks) and
                  all(x.get('native_exit') == 0 for x in runs))
    (d / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')


atexit.register(save_receipt)

for width in (1, 2):
    whole = run(f'whole-w{width}', f'!(select 0 {policy(width, 2000)} {body})')
    if not whole:
        checks.append(dict(case=f'whole-w{width}', passed=False))
        continue
    expected = whole[-1]
    for cut in (0, 1, 2, 7, 19, 31, 60, 91, 109, 110):
        obs = run(f'split-w{width}-c{cut}', f'!(let {pattern} (select 0 {policy(width, cut)} {body}) (Split $v (select 0 {policy(width, 2000 - cut)} $r)))')
        trees = parse((d / f'split-w{width}-c{cut}.stdout').read_text())
        pair = tagged(trees, 'Split')
        semantic = bool(obs and pair) and pair[1] + obs[-1]['values'] == expected['values'] and all((obs[-1][k] == expected[k] for k in ('work', 'status', 'closed', 'satisfied')))
        same_history = bool(obs) and normalize(obs[-1]['history']) == normalize(expected['history'])
        checks.append(dict(case=f'split-w{width}-c{cut}', passed=semantic and same_history, semantic=semantic, whole_record=same_history, work=obs[-1]['work'] if obs else None))
    for cut in (0, 19, 60):
        obs = run(f'zero-w{width}-c{cut}', f'!(let {pattern} (select 0 {policy(width, cut)} {body}) (Before $w $h (select 0 {policy(width, 0)} $r)))')
        trees = parse((d / f'zero-w{width}-c{cut}.stdout').read_text())

        def before(x):
            if isinstance(x, list):
                if x and x[0] == 'Before':
                    return x
                for v in x:
                    a = before(v)
                    if a:
                        return a
        a = before(trees)
        checks.append(dict(case=f'zero-w{width}-c{cut}', passed=bool(a and obs) and a[1] == obs[-1]['work'] and (a[2] == obs[-1]['history'])))
    for cut in (19, 31, 60, 91):
        obs = run(f'late-w{width}-c{cut}', f'!(let {plain} (select 0 {policy(width, cut, False)} {body}) (Late $w $v (select 0 {policy(width, 2000 - cut)} $r)))')
        pair = tagged(parse((d / f'late-w{width}-c{cut}.stdout').read_text()), 'Late')
        checks.append(dict(case=f'late-w{width}-c{cut}', passed=bool(obs and pair) and pair[2] + obs[-1]['values'] == expected['values'] and (obs[-1]['work'] == expected['work']) and (['start-work', pair[1]] in obs[-1]['history']), requested=cut, prefix_work=pair[1] if pair else None))

def walk(x):
    if isinstance(x, list):
        if x and isinstance(x[0], str):
            yield x
        for y in x:
            yield from walk(y)

def fields(history):
    return {x[0]: x[1] for x in history[1:] if isinstance(x, list) and len(x) == 2}
def history_nodes(record, seen=None):
    """Yield each immutable receipt node once under its session identity."""
    if seen is None:
        seen = {}
    for graph in (item for item in walk(record) if item[0] == 'NeedGraph'):
        for node in graph[4]:
            identity = graph[1], node[1]
            if identity in seen:
                if seen[identity] != node:
                    raise ValueError('conflicting history node identity')
                continue
            seen[identity] = node
            yield identity, node


def production_delivery_cut(record):
    """Propose a cut from the full trace, before inspecting late capture.

    Selection-local charges exclude separately paid worker preparation. The
    proposed cut is therefore checked against an actual recorded prefix below.
    """
    summary = fields(record)
    if summary.get('status') != 'Complete':
        return None
    paid = summary['start-work']
    seen = {}
    productions = {}
    deliveries = []
    for selection in summary['selections']:
        paid += selection[4]
        for _, node in history_nodes(selection, seen):
            event = node[4]
            if not isinstance(event, list) or event[0] != 'NeedEvent':
                continue
            producer = event[2], event[3], event[7]
            if event[8] == 'True':
                productions[producer] = paid
            elif event[1] == 0 and event[7]:
                deliveries.append((paid, producer))
    if not productions:
        return None
    cut = max(productions.values())
    later = [(stamp, producer) for stamp, producer in deliveries
             if stamp > cut and producer in productions]
    if not later:
        return None
    return dict(cut=cut, selected_spent=paid,
                production_stamps=sorted(productions.values()),
                later_delivery_stamps=[stamp for stamp, _ in later])


def production_phase_prefix(prefix, productions, deliveries, cut):
    """Check producer completion independently of the later recording test."""
    if not prefix or prefix['work'] != cut or prefix['closed'] != 'False':
        return False
    if fields(prefix['history']).get('status') != 'Complete':
        return False
    events = [node[4] for _, node in history_nodes(prefix['history'])
              if isinstance(node[4], list) and node[4][0] == 'NeedEvent']
    produced = [event for event in events if event[8] == 'True']
    delivered = [event for event in events if event[1] == 0 and event[8] == 'False']
    return len(delivered) < len(deliveries) and normalize([produced, delivered]) == \
        normalize([productions, deliveries[:len(delivered)]])


def same_observation(left, right):
    return all(left[key] == right[key] for key in
               ('values', 'work', 'closed', 'satisfied', 'status'))


def admission_events(record):
    nodes = dict(history_nodes(record))
    return [node[4] for node in nodes.values() if isinstance(node[4], list)
            and node[4][0] == 'NeedEvent' and node[4][1] in (6, 7, 8)]


def history_phase_controls():
    produced = ['NeedEvent', 1, 11, 7, 0, 0, 0, 19, 'True']
    delivered = ['NeedEvent', 0, 11, 7, 0, 0, 0, 19, 'False']
    def graph(session, event):
        return ['NeedGraph', session, 1, 'Complete',
                [['NeedNode', 1, 0, 0, event]]]
    first, later = graph(3, produced), graph(4, delivered)
    nodes = list(history_nodes([first, first, later]))
    checks.append(dict(case='history-node-session-identity', passed=
        len(nodes) == 2 and nodes[0][0] == (3, 1) and nodes[1][0] == (4, 1)))
    conflict = graph(3, delivered)
    refused = False
    try:
        list(history_nodes([first, conflict]))
    except ValueError:
        refused = True
    checks.append(dict(case='history-node-conflict-refused', passed=refused))
    # Selection indices deliberately differ from cumulative paid work.
    def record(second=later, status='Complete'):
        return ['History', ['status', status], ['start-work', 10],
                ['selections', [['Selection', 99, 1, 'test', 2, first],
                                ['Selection', 100, 2, 'test', 3, second]]]]
    witness = production_delivery_cut(record())
    checks.append(dict(case='history-phase-uses-selection-charge-candidate', passed=
        witness == dict(cut=12, selected_spent=15, production_stamps=[12],
                        later_delivery_stamps=[15])))
    wrong = list(delivered)
    wrong[3] = 8
    checks.append(dict(case='history-phase-needs-later-same-producer', passed=
        production_delivery_cut(record(first)) is None and
        production_delivery_cut(record(graph(4, wrong))) is None and
        production_delivery_cut(record(status='Incomplete')) is None))
    prefix = dict(work=12, closed='False', history=['History',
        ['status', 'Complete'], ['selections', [first]]])
    checks.append(dict(case='history-phase-validates-actual-prefix', passed=
        production_phase_prefix(prefix, [produced], [delivered], 12) and
        not production_phase_prefix(dict(prefix, work=11), [produced], [delivered], 12) and
        not production_phase_prefix(dict(prefix, closed='True'), [produced], [delivered], 12) and
        not production_phase_prefix(prefix, [produced, produced], [delivered], 12) and
        not production_phase_prefix(prefix, [produced], [], 12)))


history_phase_controls()


admission_source = ('(= (history-verdict $w) Maybe)\n'
                    '(= (history-verdict $w) True)\n'
                    '(= (history-verdict $w) False)\n'
                    '(= (history-verdict $w) True)\n'
                    '(=w (history-guarded) 7 2 (admit history-verdict))\n')
admission_body = '(weight:read (algebra 0 1 + *) (history-guarded))'
for language in ('prime', 'he', 'petta'):
    for width in (1, 2):
        stem = f'admission-{language}-w{width}'
        off = run(stem + '-off', f'!(select 0 {policy(width, 1500, False)} {admission_body})',
                  admission_source, language)
        on = run(stem + '-on', f'!(select 0 {policy(width, 1500)} {admission_body})',
                 admission_source, language)
        events = admission_events(on[-1]['history']) if on else []
        counts = {kind: sum(event[1] == kind for event in events) for kind in (6, 7, 8)}
        checks.append(dict(case=stem, passed=bool(off and on) and
            same_observation(off[-1], on[-1]) and counts == {6: 2, 7: 1, 8: 1} and
            on[-1]['closed'] == 'False' and on[-1]['status'] == 'suspended' and
            len(on[-1]['values']) == 2 and all(value[1:3] == [7, 2] for value in on[-1]['values']) and
            all(event[10] == ['quote', 2] and event[7] == 0 and event[8] == 'False' for event in events),
            verdict_counts=counts))
        # HE's normal interpretation of an unknown constructor evaluates its
        # operands. Quote the saved display so that comparison checks the
        # retained history, not a second interpretation of its quoted fields.
        saved_history = '(quote $h)' if language == 'he' else '$h'
        zero = run(stem + '-zero',
            f'!(let {pattern} (select 0 {policy(width, 1500)} {admission_body}) '
            f'(Before $w {saved_history} (select 0 {policy(width, 0)} $r)))',
            admission_source, language)
        before = tagged(parse((d / (stem + '-zero.stdout')).read_text()), 'Before')
        saved = before[2] if before else None
        if language == 'he' and isinstance(saved, list) and saved[0] == 'quote':
            saved = saved[1]
        checks.append(dict(case=stem + '-zero', passed=bool(before and zero) and
            before[1] == zero[-1]['work'] and saved == zero[-1]['history']))
        for cut in (0, 1, 12, 31, 60, 81, 150, 228, 229):
            name = stem + f'-cut{cut}'
            resumed = run(name,
                f'!(let {pattern} (select 0 {policy(width, cut)} {admission_body}) '
                f'(Split $v (select 0 {policy(width, 1500 - cut)} $r)))',
                admission_source, language)
            split = tagged(parse((d / (name + '.stdout')).read_text()), 'Split')
            same = bool(on and resumed and split)
            if same:
                values = split[1] + resumed[-1]['values']
                same = normalize([values, resumed[-1]['history']]) == normalize(
                    [on[-1]['values'], on[-1]['history']]) and all(
                    resumed[-1][key] == on[-1][key] for key in
                    ('work', 'closed', 'satisfied', 'status'))
            checks.append(dict(case=name, passed=same))

for verdict, kind, attempts in (('True', 6, 1), ('False', 7, 1), ('Maybe', 8, 1)):
    definition = (f'(= (history-verdict $w) {verdict})\n'
        '(=w (history-guarded) (println! "HISTORY-GUARDED-BODY") 2 (admit history-verdict))\n')
    name = 'admission-effect-' + verdict
    off = run(name + '-off', f'!(select 0 {policy(1, 1000, False)} (history-guarded))', definition)
    on = run(name, f'!(select 0 {policy(1, 1000)} (history-guarded))', definition)
    events = admission_events(on[-1]['history']) if on else []
    lines = (d / (name + '.stdout')).read_text().splitlines()
    printed = sum(line.strip() in ('HISTORY-GUARDED-BODY', '"HISTORY-GUARDED-BODY"') for line in lines)
    # A closed source is examined once, including a completed rejection.
    # History records the actual admission attempt; an empty frontier does
    # not cause a second call or a synthetic admission event.
    checks.append(dict(case=name, passed=bool(off and on) and same_observation(off[-1], on[-1]) and
        len(events) == attempts and all(event[1] == kind for event in events) and
        printed == (1 if verdict == 'True' else 0) and
        on[-1]['closed'] == ('False' if verdict == 'Maybe' else 'True')))

# Equal authored rules are separate occurrences even when both reject.
for verdict, kind in (('True', 6), ('False', 7), ('Maybe', 8)):
    rule = '(=w (history-guarded) (println! "HISTORY-GUARDED-BODY") 2 (admit history-verdict))\n'
    definition = f'(= (history-verdict $w) {verdict})\n' + rule + rule
    name = 'admission-duplicate-' + verdict
    off = run(name + '-off', f'!(select 0 {policy(1, 1000, False)} (history-guarded))', definition)
    on = run(name, f'!(select 0 {policy(1, 1000)} (history-guarded))', definition)
    events = admission_events(on[-1]['history']) if on else []
    lines = (d / (name + '.stdout')).read_text().splitlines()
    printed = sum(line.strip() in ('HISTORY-GUARDED-BODY', '"HISTORY-GUARDED-BODY"') for line in lines)
    checks.append(dict(case=name, passed=bool(off and on) and same_observation(off[-1], on[-1]) and
        len(events) == 2 and len({event[4] for event in events}) == 2 and
        all(event[1] == kind and event[7] == 0 and event[8] == 'False' and
            event[10:] == [['quote', 2], ['quote', verdict]] for event in events) and
        printed == (2 if verdict == 'True' else 0) and
        on[-1]['values'] == ([[], []] if verdict == 'True' else []) and
        on[-1]['closed'] == ('False' if verdict == 'Maybe' else 'True') and
        on[-1]['status'] == ('suspended' if verdict == 'Maybe' else 'closed')))

unexamined = run('admission-unexamined', f'!(select 0 {policy(1, 0)} {admission_body})', admission_source)
checks.append(dict(case='admission-unexamined', passed=bool(unexamined) and
    not admission_events(unexamined[-1]['history']) and unexamined[-1]['work'] == 0 and
    unexamined[-1]['closed'] == 'False'))

for language in ('prime', 'he', 'petta'):
    for width in (1, 2):
        off = run(f'off-{language}-w{width}', f'!(select 0 {policy(width, 2000, False)} {body})', language=language)
        on = run(f'on-{language}-w{width}', f'!(select 0 {policy(width, 2000)} {body})', language=language)
        zero_policy = policy(width, 2000).replace('(selections 512)', '(selections 0)')
        zero = run(f'empty-capacity-{language}-w{width}', f'!(select 0 {zero_policy} {body})', language=language)
        semantic = bool(off and on and zero) and all((o[-1]['values'] == [['Pair', 3, 3], ['Pair', 3, 3], ['Pair', 6, 6]] and all((o[-1][k] == off[-1][k] for k in ('work', 'status', 'closed'))) for o in (on, zero)))
        z = fields(zero[-1]['history']) if zero else {}
        checks.append(dict(case=f'erasure-{language}-w{width}', passed=semantic and 'history' not in off[-1] and (z.get('status') == 'Incomplete') and (z.get('selections') == []) and (z.get('omitted-selections', 0) > 0) and (z.get('nodes') == 0)))
for width in (1, 2):
    whole = next((r['observations'][-1] for r in runs if r['name'] == f'whole-w{width}'))
    nodes = dict(history_nodes(whole['history']))
    events = [x[-1] for x in nodes.values() if isinstance(x[-1], list) and x[-1][0] == 'NeedEvent']
    productions = [x for x in events if x[8] == 'True']
    deliveries = [x for x in events if x[1] == 0 and x[8] == 'False']
    producer_ids = {(event[2], event[3], event[7]) for event in productions}
    checks.append(dict(case=f'production-delivery-w{width}', passed=len(productions) == 3 and len({x[7] for x in productions}) == 3 and len(producer_ids) == 3 and (len(deliveries) == 3) and all((event[2], event[3], event[7]) in producer_ids for event in deliveries)))
    witness = production_delivery_cut(whole['history'])
    cut = witness['cut'] if witness else 0
    prefix_rows = run(f'phase-prefix-w{width}',
        f'!(select 0 {policy(width, cut)} {body})')
    prefix = prefix_rows[-1] if prefix_rows else None
    phase_verified = production_phase_prefix(prefix, productions, deliveries, cut)
    name = f'late-after-production-w{width}'
    observed = run(name, f'!(let {plain} (select 0 {policy(width, cut, False)} {body}) (Late $w $v (select 0 {policy(width, 2000 - cut)} $r)))')
    pair = tagged(parse((d / (name + '.stdout')).read_text()), 'Late')
    late = observed[-1] if observed else None
    nodes = dict(history_nodes(late['history'])) if late else {}
    events = [x[-1] for x in nodes.values() if isinstance(x[-1], list) and x[-1][0] == 'NeedEvent']
    checks.append(dict(case=f'late-external-production-w{width}', passed=
        bool(witness and pair and late) and phase_verified and
        pair[1] == cut and ['start-work', pair[1]] in late['history'] and
        pair[2] + late['values'] == whole['values'] and
        all(late[key] == whole[key] for key in ('work', 'closed', 'satisfied', 'status')) and
        any(['producer', 'External'] in event for event in events) and
        not any(event[8] == 'True' for event in events), phase_witness=witness,
        phase_verified=phase_verified, prefix_work=prefix['work'] if prefix else None))
    tiny = policy(width, 2000).replace('(selections 512)', '(selections 2)').replace('(bytes 65536)', '(bytes 1)').replace('(owners 10000)', '(owners 0)')
    o = run(f'tiny-w{width}', f'!(select 0 {tiny} {body})')
    h = fields(o[-1]['history']) if o else {}
    checks.append(dict(case=f'bounded-omission-w{width}', passed=bool(o) and o[-1]['values'] == whole['values'] and (o[-1]['work'] == whole['work']) and (h.get('status') == 'Incomplete') and (h.get('nodes') == 0) and (len(h.get('selections', [])) == 2)))
    o = run(f'stale-zero-w{width}', f'!(let {pattern} (select 0 {policy(width, 19)} {body}) (let $_ (add-atom &self (= (new-row) 8)) (Before $w $h (select 0 {policy(width, 0)} $r))))')
    tree = tagged(parse((d / f'stale-zero-w{width}.stdout').read_text()), 'Before')
    checks.append(dict(case=f'stale-zero-w{width}', passed=bool(o and tree) and o[-1]['status'] == 'invalidated' and (o[-1]['work'] == tree[1]) and (o[-1]['history'] == tree[2]) and (o[-1]['closed'] == 'False')))
weighted = base.replace('(= (history-coin) 3)', '(=w (history-coin) 3 2)').replace('(= (history-coin) 6)', '(=w (history-coin) 6 5)')
for width in (1, 2):
    o = run(f'weighted-w{width}', f'!(select 0 {policy(width, 2000)} (weight:read (algebra 0 1 + *) {body}))', weighted)
    values = [x[1:3] for obs in o for x in walk(obs['values']) if x[0] == 'Weighted']
    checks.append(dict(case=f'weighted-sharing-w{width}', passed=values == [[['Pair', 3, 3], 2], [['Pair', 3, 3], 2], [['Pair', 6, 6], 5]]))
effect = '(= (history-effect) (println! "HISTORY-ONCE"))\n'
o = run('stored-graph-inert', f'!(let {pattern} (select 0 {policy(1, 2000)} (history-effect)) (Data $h (eval $h)))', effect)
lines = (d / 'stored-graph-inert.stdout').read_text().splitlines()
checks.append(dict(case='stored-graph-inert', passed=sum((line.strip() in ('HISTORY-ONCE', '"HISTORY-ONCE"') for line in lines)) == 1 and any(('NeedGraph' in line for line in lines))))


def graph_topology(record):
    """An independent structural check, deliberately not source authentication."""
    known = {}
    for graph in (x for x in walk(record) if x[0] == 'NeedGraph'):
        session, root, _, nodes = graph[1:]
        seen = set()
        for node in nodes:
            _, identity, left, right, _ = node
            if identity == 0 or identity in seen:
                return False
            if any(parent and parent not in seen for parent in (left, right)):
                return False
            key = session, identity
            if key in known and known[key] != node:
                return False
            known[key] = node
            seen.add(identity)
        if root not in seen:
            return False
    return bool(known)


for width in (1, 2):
    whole = next(r['observations'][-1] for r in runs
                 if r['name'] == f'whole-w{width}')
    checks.append(dict(case=f'ordered-graph-identity-w{width}',
                       passed=graph_topology(whole['history']) and
                       fields(whole['history'])['source-correspondence'] == 'Unchecked'))

    complete = run(f'complete-rows-w{width}',
                   f'!(select 0 {policy(width, 2000)} (history-coin))')
    captured = fields(complete[-1]['history']) if complete else {}
    checks.append(dict(case=f'complete-recorded-duplicates-w{width}', passed=
        bool(complete) and complete[-1]['values'] == [3, 3, 6] and
        complete[-1]['closed'] == 'True' and captured.get('status') == 'Complete' and
        captured.get('omitted-nodes') == captured.get('omitted-selections') == 0 and
        captured.get('source-correspondence') == 'Unchecked' and
        graph_topology(complete[-1]['history'])))

    # An outer-complete selection prefix cannot conceal a refused inner graph.
    no_nodes = policy(width, 2000).replace('(nodes 32)', '(nodes 0)')
    obs = run(f'inner-refused-w{width}', f'!(select 0 {no_nodes} {body})')
    h = fields(obs[-1]['history']) if obs else {}
    checks.append(dict(case=f'inner-refused-retains-selection-w{width}', passed=
        bool(obs) and same_observation(obs[-1], whole) and
        h.get('omitted-selections') == 0 and h.get('omitted-nodes', 0) > 0 and
        h.get('status') == 'Incomplete' and h.get('nodes') == 0 and
        len(h.get('selections', [])) == len(fields(whole['history'])['selections'])))

    # Incomparable limits are an uninterpreted operation, not permission to
    # restart, resize, discard or advance the previously owned recording.
    changed = policy(width, 0).replace('(nodes 32)', '(nodes 31)')
    obs = run(f'changed-limits-w{width}',
        f'!(let {pattern} (select 0 {policy(width, 19)} {body}) '
        f'(let $_ (select 0 {changed} $r) '
        f'(Before $w $h (select 0 {policy(width, 0, False)} $r))))')
    tree = tagged(parse((d / f'changed-limits-w{width}.stdout').read_text()), 'Before')
    checks.append(dict(case=f'changed-limits-preserve-owner-w{width}', passed=
        bool(obs and tree) and obs[-1]['work'] == tree[1] and
        obs[-1]['history'] == tree[2] and obs[-1]['closed'] == 'False'))

    # A nested physical worker must not inherit its parent's writable slice.
    # Missing detail is allowed only as explicit observational incompleteness.
    inner = ('(select 3 (search-policy (control FIFO (workers 2) '
             '(budget 2000))) (history-coin))')
    off = run(f'nested-off-w{width}',
              f'!(select 0 {policy(width, 4000, False)} {inner})')
    on = run(f'nested-on-w{width}',
             f'!(select 0 {policy(width, 4000)} {inner})')
    nested = list(observations(on[-1]['values'])) if on else []
    checks.append(dict(case=f'nested-physical-erasure-w{width}', passed=
        bool(off and on) and same_observation(off[-1], on[-1]) and
        len(nested) == 1 and nested[0]['values'] == [3, 3, 6] and
        nested[0]['satisfied'] == 'True' and nested[0]['closed'] == 'False'))

    # Withdraw a still-running consumer, then reuse the worker threads in an
    # unrelated episode. Both cancellation and library retirement own roots.
    cancel_source = base + '(= (history-loop) (history-loop))\n'
    initial = policy(width, 100).replace('(demand all)', '(goal $xs (history-loop))')
    final = policy(width, 1000).replace('(demand all)', '(goal $xs False)')
    obs = run(f'cancel-w{width}',
        f'!(let {pattern} (select 1 {initial} (history-coin)) '
        f'(Before $w $h (select 1 {final} $r)))\n'
        f'!(select 0 {policy(width, 2000)} {body})', cancel_source)
    tree = tagged(parse((d / f'cancel-w{width}.stdout').read_text()), 'Before')
    prefix = fields(tree[2])['selections'] if tree else []
    resumed = fields(obs[0]['history'])['selections'] if obs else []
    checks.append(dict(case=f'cancel-then-independent-owner-w{width}', passed=
        len(obs) == 2 and bool(tree) and obs[0]['values'] == [] and
        obs[0]['closed'] == 'True' and obs[0]['satisfied'] == 'False' and
        resumed[:len(prefix)] == prefix and same_observation(obs[-1], whole)))

    parked_source = ('(= (history-verdict $x) Maybe)\n'
                     '(= (history-verdict $x) True)\n'
                     '(=w (history-guarded) 7 2 (admit history-verdict))\n')
    parked_body = '(weight:read (algebra 0 1 + *) (history-guarded))'
    off = run(f'parked-off-w{width}',
              f'!(select 0 {policy(width, 2000, False)} {parked_body})', parked_source)
    on = run(f'parked-on-w{width}',
             f'!(select 0 {policy(width, 2000)} {parked_body})', parked_source)
    checks.append(dict(case=f'parked-grade-retained-w{width}', passed=
        bool(off and on) and same_observation(off[-1], on[-1]) and
        on[-1]['closed'] == 'False' and on[-1]['status'] == 'suspended' and
        len(on[-1]['values']) == 1 and on[-1]['values'][0][1:3] == [7, 2]))

for language in ('prime', 'he', 'petta'):
    malformed = {
        'unknown-field': history.replace('(nodes 32)', '(unknown-nodes 32)'),
        'field-order': history.replace('(selections 512) (nodes 32)',
                                       '(nodes 32) (selections 512)'),
        'negative-limit': history.replace('(bytes 65536)', '(bytes -1)'),
        'field-arity': history.replace('(owners 10000)', '(owners 10000 0)'),
    }
    for name, descriptor in malformed.items():
        case = f'malformed-{name}-{language}'
        control = policy(1, 100).replace(history, descriptor)
        query = f'(select 0 {control} {body})'
        obs = run(case, '!' + query, language=language)
        result = parse((d / (case + '.stdout')).read_text())
        control_atom = parse(control)[0]
        # Malformed native descriptors follow each host's existing fallback.
        # Prime diagnoses the malformed search policy; the eager hosts retain
        # the call with its three evaluated operand occurrences.
        if language == 'prime':
            expected = [['Error', control_atom, 'SearchPolicyLaneNameSymbolIsExpected']]
        else:
            expected = [['select', 0, control_atom, value] for value in
                        (['Pair', 3, 3], ['Pair', 3, 3], ['Pair', 6, 6])]
        actual = result if language == 'petta' else result[0]

        def canonical_bools(value):
            if isinstance(value, list):
                return [canonical_bools(item) for item in value]
            return {'true': 'True', 'false': 'False'}.get(value, value)

        checks.append(dict(case=case, passed=not obs and
            canonical_bools(actual) == canonical_bools(expected)))

# The debugger is an ordinary imported MeTTa program, using the same retained
# observations. It must not execute a code-shaped verdict while inspecting it.
debug_client = w / 'tests/prime/need_admission_history.metta'
debug_library = w / 'lib/native_history.metta'
for variant in ('ordinary', 'inert', 'omitted'):
    source = debug_client.read_text()
    if variant == 'inert':
        source = source.replace('(= (debug:unknown $coefficient) Maybe)',
            '(= (debug:unknown $coefficient) '
            '(quote (println! "VERDICT-MUST-STAY-DATA")))')
    if variant == 'omitted':
        source = source.replace('(nodes 32) (bytes 65536)', '(nodes 0) (bytes 0)')
    source = source.replace('../../lib/native_history.metta', str(debug_library))
    for language in ('prime', 'he', 'petta'):
        name = f'debug-{variant}-{language}'
        run(name, source, definitions='', language=language,
            source_path=debug_client if variant == 'ordinary' else None)
        stdout = (d / (name + '.stdout')).read_text()
        nodes = list(walk(parse(stdout)))
        reports = [node for node in nodes if node[0] == 'Explained']
        roles = [[node[3] for node in walk(report) if node[0] == 'Verdict']
                 for report in reports]
        effects = [line.strip('"') for line in stdout.splitlines()
                   if line.strip('"') in ('ACCEPTED-BODY', 'REJECTED-BODY',
                                           'PENDING-BODY', 'VERDICT-MUST-STAY-DATA')]
        ok = len(reports) == 4 and effects == ['ACCEPTED-BODY']
        if ok:
            views = [{item[0]: item[1:] for item in report[1:]
                      if isinstance(item, list) and item} for report in reports]
            ok = all(views[i]['closed'] == ['True'] and
                     views[i]['status'] == ['closed'] for i in (0, 1)) and \
                 all(views[i]['closed'] == ['False'] and
                     views[i]['status'] == ['suspended'] for i in (2, 3)) and \
                 views[3]['work'] == [0] and views[3]['activity'] == ['Unexamined']
            ok = ok and not any(node[0].startswith('history:') for node in nodes)
            if variant == 'omitted':
                ok = ok and roles == [[], [], [], []] and all(
                    views[i]['recording'][0] == 'Incomplete' for i in (0, 1, 2))
            else:
                ok = ok and roles == [['Accepted'],
                    ['Rejected'], ['Pending'], []]
        checks.append(dict(case=name, passed=ok, roles=roles, effects=effects,
                           reports=len(reports),
                           library_sha256=hashlib.sha256(debug_library.read_bytes()).hexdigest()))

# Project a real nonempty retained result, preserving order and multiplicity.
for language in ('prime', 'he', 'petta'):
    name = 'debug-values-' + language
    run(name, f'!(import! &self {debug_library}) '
        f'!(let $observation (select 0 {policy(1, 2000)} {body}) '
        '(history:explain $observation))', language=language)
    stdout = (d / (name + '.stdout')).read_text()
    report = tagged(parse(stdout), 'Explained')
    expected = ['values', ['quote', [['Pair', 3, 3], ['Pair', 3, 3], ['Pair', 6, 6]]]]
    checks.append(dict(case=name, passed=bool(report) and report[1] == expected,
                       projection=report[1] if report else None))

    # A supplied report is data, not replay authority or executable syntax.
    name = 'debug-code-data-' + language
    supplied = ('(Observation ((println! "EXPLAIN-MUST-STAY-DATA")) '
        '(satisfied True) (closed True) (residual None) (work 0) (status closed) '
        '(history (History (status Incomplete) (start-work 0) (earlier Unrecorded) '
        '(source-correspondence Unchecked) (omitted-selections 1) (omitted-nodes 0) '
        '(overflow False) (RetainedCharges (payload 0)) (limits (nodes 0)) '
        '(nodes 0) (reservations 0) (selections ()))))')
    run(name, f'!(import! &self {debug_library}) !(history:explain {supplied})',
        definitions='', language=language)
    stdout = (d / (name + '.stdout')).read_text()
    report = tagged(parse(stdout), 'Explained')
    effects = [line for line in stdout.splitlines()
               if line.strip('"') == 'EXPLAIN-MUST-STAY-DATA']
    expected = ['values', ['quote', [['println!', 'EXPLAIN-MUST-STAY-DATA']]]]
    checks.append(dict(case=name, passed=bool(report) and report[1] == expected and not effects,
                       projection=report[1] if report else None, effects=effects))

finished = True
save_receipt()
for check in checks:
    print(f'{"PASS" if check["passed"] else "FAIL"}: {check["case"]}')
passed = sum(check['passed'] for check in checks)
print(f'(NativeHistorySummary {passed} {len(checks)} {len(checks) - passed})')
raise SystemExit(passed != len(checks) or
                 any(run.get('native_exit') != 0 for run in runs))
