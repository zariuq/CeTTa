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
from test_prime_native_control import parse, observations, bag
from test_prime_native_cost import receipt

parser = argparse.ArgumentParser()
parser.add_argument('--runtime', required=True)
parser.add_argument('--receipt-dir', required=True)
parser.add_argument('--timeout', type=float, default=60)
parser.add_argument('--population', choices=('all', 'replay', 'forget', 'diagnostics', 'origins', 'resampling', 'debug', 'breakpoint', 'resource-breakpoint'), default='all')
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

def policy(width, budget, h=True, stop=False):
    observer = '(history False)' if stop else history if h else ''
    return f"(search-policy (control FIFO (demand all) (retain True) (workers {width}) (budget {budget}) {observer}))"
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


def walk(x):
    if isinstance(x, list):
        if x and isinstance(x[0], str):
            yield x
        for y in x:
            yield from walk(y)


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

def same_observation(left, right):
    return all(left[key] == right[key] for key in
               ('values', 'work', 'closed', 'satisfied', 'status'))

def run(name, query, definitions=base, language='prime', source_path=None, environment=None):
    p = Path(source_path) if source_path else d / (name + '.metta')
    if not source_path:
        p.write_text(definitions + query + '\n')
    cmd = [str(b), '--lang', language] + (['--profile', 'extended'] if language != 'prime' else []) + [str(p)]
    try:
        r = subprocess.run(cmd, cwd=w, capture_output=True, text=True, timeout=args.timeout,
                           env=environment)
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
    runs.append(dict(name=name, command=cmd, source_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
                     need_diagnostics=environment.get('CETTA_PRIME_NEED_TRACE') if environment is not None else None,
                     native_exit=r.returncode, observations=o))
    return o


finished = False


def save_receipt():
    result = dict(at=datetime.datetime.now().astimezone().isoformat(),
                  binary=meta, runs=runs, checks=checks, finished=finished,
                  passed=finished and all(x['passed'] for x in checks) and
                  all(x.get('native_exit') == 0 for x in runs))
    (d / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')


atexit.register(save_receipt)


def finish():
    global finished
    finished = True
    save_receipt()
    for check in checks:
        print(f'{"PASS" if check["passed"] else "FAIL"}: {check["case"]}')
    passed = sum(check['passed'] for check in checks)
    print(f'(NativeHistorySummary {passed} {len(checks)} {len(checks) - passed})')
    raise SystemExit(passed != len(checks) or
                     any(run.get('native_exit') != 0 for run in runs))


def replay_fields(value):
    """Read one actual report; duplicate field names never overwrite evidence."""
    if not isinstance(value, list) or not value:
        raise ValueError(('not a report', value))
    result = {}
    for row in value[1:]:
        if not isinstance(row, list) or len(row) != 2 or row[0] in result:
            raise ValueError(('invalid report field', row))
        result[row[0]] = row[1]
    return result


def history_reports(name, tag):
    result = []
    for row in parse((d / (name + '.stdout')).read_text()):
        for value in row:
            if isinstance(value, list) and value and value[0] == tag:
                result.append(replay_fields(value))
    return result


def replay_reports(name):
    return history_reports(name, 'ReplayCheckpoint')


def replay_observation(report):
    request = replay_fields(report['last-request'])
    quoted = request['observation']
    if not isinstance(quoted, list) or len(quoted) != 2 or quoted[0] != 'quote':
        raise ValueError(('unquoted observation', quoted))
    value = quoted[1]
    return {'values': value[1], **replay_fields(['Fields', *value[2:]])}


def replay_policy(budget=0, transaction=False):
    resource = '(resources Transaction)' if transaction else ''
    return (f'(search-policy (control FIFO (budget {budget}) (demand all) (retain True) {resource} '
            '(costs native-events-v1) (receipts True) '
            '(history (selections 512) (nodes 64) (bytes 32768) (owners 512))))')


def replay_controls():
    # This is the public capsule protocol, not a second replay interpreter.
    # Answers and store changes below follow directly from the authored sources.
    library = w / 'lib/native_history.metta'
    projection = f'!(import! &self {library})\n' + '''
!(let $record (parse "(Report (payload (println! \\\"FIELD-MUST-STAY-DATA\\\")))") (history:field $record payload))
!(let $record (parse "(Report (payload A) (payload A))") (history:field $record payload))
!(let $record (parse "(Report (payload A))") (history:field $record absent))
!(let $record (parse "(Observation (A A B) (closed True))") (history:field $record values))
!(let $record (parse "(Observation (A A B) (values Other))") (history:field $record values))
'''
    projected = [
        ['HistoryField', 'Found', 'payload', ['quote', ['println!', 'FIELD-MUST-STAY-DATA']]],
        ['HistoryField', 'Duplicate', 'payload', []],
        ['HistoryField', 'Missing', 'absent', []],
        ['HistoryField', 'Found', 'values', ['quote', ['A', 'A', 'B']]],
        ['HistoryField', 'Duplicate', 'values', []],
    ]
    for language in ('prime', 'he', 'petta'):
        name = 'replay-field-' + language
        run(name, projection, definitions='', language=language)
        text = (d / (name + '.stdout')).read_text()
        actual = [node for node in walk(parse(text)) if node[0] == 'HistoryField']
        effects = [line for line in text.splitlines()
                   if line.strip('"') == 'FIELD-MUST-STAY-DATA']
        checks.append(dict(case=name, passed=actual == projected and not effects,
                           actual=actual, effects=effects))

    expression = '(superpose (A B B))'
    whole = run('replay-independent-whole',
                f'!(cost:run {replay_policy(500)} {expression})', definitions='')
    expected = whole[-1] if whole else None
    for cut in (0, 1, 7, 19):
        name = f'replay-pure-split-{cut}'
        query = (f'!(bind! &capsule (history:capture (quote (cost:run '
                 f'{replay_policy()} {expression})) (ReplayLimits 8 8192 134217728)))\n'
                 '!(history:checkpoint &capsule)\n'
                 f'!(history:record &capsule {cut})\n'
                 f'!(history:record &capsule {500 - cut})\n'
                 '!(history:replay &capsule 2000000000)\n'
                 '!(history:replay &capsule 2000000000)\n'
                 '!(history:checkpoint &capsule)\n'
                 '!(history:dispose &capsule)')
        run(name, query, definitions='')
        reports = replay_reports(name)
        ok = len(reports) == 6 and expected is not None
        if ok:
            first, last = map(replay_observation, reports[1:3])
            cost = receipt(last['cost'])
            reference = receipt(expected['cost'])
            ok = (first['values'] + last['values'] == ['A', 'B', 'B'] and
                  last['closed'] == 'True' and last['status'] == 'closed' and
                  last['work'] == expected['work'] and
                  cost['counts'] == {kind: count + (2 if kind == 'cost-read' else 0)
                                     for kind, count in reference['counts'].items()} and
                  [r['accepted-requests'] for r in reports] == [0, 0, 0, 1, 2, 2] and
                  [r['replay-authenticated'] for r in reports] ==
                  ['False'] * 4 + ['True', 'True'] and
                  all(r['refusal'] == 'None' and r['source-correspondence'] == 'Unchecked'
                      and r['storage-within-limit'] == 'True' for r in reports) and
                  reports[2]['last-request'] == reports[-1]['last-request'])
        checks.append(dict(case=name, passed=ok, requests=len(reports)))

    # An accepted residual has its own owning handle. Closing its capsule must
    # not retire that separately returned consumer or replay its earlier answers.
    name = 'replay-owned-resume-after-disposal'
    query = (f'!(import! &self {library})\n'
             f'!(bind! &capsule (history:capture (quote (cost:run {replay_policy()} '
             f'{expression})) (ReplayLimits 8 8192 134217728)))\n'
             '!(history:record &capsule 7)\n'
             '!(history:replay &capsule 2000000000)\n'
             '!(bind! &checkpoint (history:checkpoint &capsule))\n'
             '!(bind! &accepted (let $report &checkpoint '
             '(let (HistoryField Found accepted-control $token) '
             '(history:field $report accepted-control) '
             '(let $tail (cdr-atom $token) (car-atom $tail)))))\n'
             '!(history:dispose &capsule)\n'
             f'!(select 0 {replay_policy(500)} &accepted)')
    resumed = run(name, query, definitions='')
    reports = replay_reports(name)
    ok = len(reports) == 2 and bool(resumed) and expected is not None
    if ok:
        prefix = replay_observation(reports[0])
        suffix = resumed[-1]
        ok = (prefix['values'] + suffix['values'] == ['A', 'B', 'B'] and
              suffix['closed'] == 'True' and suffix['work'] == expected['work'] and
              reports[1]['replay-authenticated'] == 'True')
    checks.append(dict(case=name, passed=ok))

    # Refusal happens before effects; a textual handle is only written data.
    for label, source, body, limits, reason in (
        ('effect', '', '(println! "REPLAY-MUST-NOT-EXECUTE")',
         '(ReplayLimits 8 8192 134217728)', 'ProducerOperation'),
        ('capacity', '', expression, '(ReplayLimits 0 8192 134217728)', 'CapsuleCapacity'),
        ('graded', '(= (replay-graded) A)\n'
         '(= (replay-graded) B (where (println! "REPLAY-MUST-NOT-EXECUTE")))\n',
         '(replay-graded)', '(ReplayLimits 8 8192 134217728)', 'UngradedNamedRows'),
    ):
        name = 'replay-refused-' + label
        run(name, f'!(history:capture (quote (cost:run {replay_policy()} {body})) {limits})',
            definitions=source)
        text = (d / (name + '.stdout')).read_text()
        reports = replay_reports(name)
        ok = (len(reports) == 1 and reports[0]['refusal'] == reason and
              reports[0]['replay-authenticated'] == 'False' and
              reports[0]['recorded-requests'] == 0 and
              not any(line.strip('"') == 'REPLAY-MUST-NOT-EXECUTE'
                      for line in text.splitlines()))
        checks.append(dict(case=name, passed=ok))

    name = 'replay-forged-written-handle'
    query = '!(history:replay (NativeHandle "prime-replay" 1) 2000000000)'
    run(name, query, definitions='')
    trees = parse((d / (name + '.stdout')).read_text())
    checks.append(dict(case=name, passed=trees ==
        [[['history:replay', ['NativeHandle', 'prime-replay', 1], 2000000000]]]))

    for mutate in (False, True):
        name = 'replay-source-changed' if mutate else 'replay-inspection-exhausted'
        query = (f'!(bind! &capsule (history:capture (quote (cost:run {replay_policy()} '
                 f'{expression})) (ReplayLimits 8 8192 134217728)))\n'
                 '!(history:record &capsule 500)\n')
        if mutate:
            query += '!(add-atom &self (replay-marker changed))\n'
        query += (f'!(history:replay &capsule {2000000000 if mutate else 0})\n'
                  '!(history:checkpoint &capsule)\n!(history:dispose &capsule)')
        run(name, query, definitions='')
        reports = replay_reports(name)
        ok = (len(reports) == 3 and reports[1]['accepted-requests'] == 0 and
              reports[1]['unconsumed-requests'] == 1 and
              reports[1]['replay-authenticated'] == 'False' and
              reports[1]['last-request'] == reports[0]['last-request'] ==
              reports[2]['last-request'])
        checks.append(dict(case=name, passed=ok))

    # Locate a committed transaction boundary rather than baking a particular
    # copying algorithm's tick count into the replay contract.
    transaction_setup = ('!(bind! &jobs (new-space))\n'
                         '!(add-atom &jobs (exec 0 (I) (O (+ A))))\n'
                         '!(add-atom &jobs (exec 1 (I) (O (+ B))))\n')
    boundary_name = 'replay-transaction-commit-boundary'
    boundary_policy = replay_policy(10000, transaction=True).replace('(demand all)', '')
    run(boundary_name, transaction_setup + f'!(select 1 {boundary_policy} &jobs)', definitions='')
    boundary = list(observations(parse((d / (boundary_name + '.stdout')).read_text())))[-1]
    boundary_agenda = replay_fields(boundary['agenda'])
    transaction_grant = boundary['work']
    checks.append(dict(case=boundary_name, passed=(len(boundary['values']) == 1 and
        boundary_agenda['phase'] == 'Select' and boundary_agenda['commits'] == 1 and
        isinstance(transaction_grant, int) and transaction_grant > 1),
        committed_boundary_grant=transaction_grant))

    # The live target receives one commit in each recording request. Replay
    # uses independent target images and cannot repeat live publications.
    for changed in (False, True):
        name = 'replay-transaction-changed-target' if changed else 'replay-transaction'
        query = (transaction_setup +
                 f'!(bind! &capsule (history:capture (quote (cost:run '
                 f'{replay_policy(transaction=True)} &jobs)) '
                 '(ReplayLimits 8 8192 536870912)))\n'
                 f'!(history:record &capsule {transaction_grant})\n'
                 f'!(history:record &capsule {transaction_grant})\n'
                 '!(history:replay &capsule 2000000000)\n')
        if changed:
            query += '!(add-atom &jobs Extra)\n!(remove-atom &jobs Extra)\n'
        query += ('!(history:replay &capsule 2000000000)\n'
                  '!(collapse (get-atoms &jobs))\n!(history:checkpoint &capsule)\n'
                  '!(history:dispose &capsule)')
        run(name, query, definitions='')
        reports = replay_reports(name)
        trees = parse((d / (name + '.stdout')).read_text())
        ok = len(reports) == 5 and [['A', 'B']] in trees
        if ok:
            final = reports[-1]
            recorded = replay_fields(reports[1]['last-request'])
            retained = replay_fields(final['last-request'])
            recorded_word = replay_fields(recorded['transaction'])
            retained_word = replay_fields(retained['transaction'])
            ok = (reports[2]['accepted-requests'] == 1 and
                  final['recorded-requests'] == 2 and
                  final['accepted-requests'] == (1 if changed else 2) and
                  final['replay-authenticated'] == ('False' if changed else 'True') and
                  final['refusal'] == ('ChangedExternalTarget' if changed else 'None') and
                  all(r['profile'] == 'TransactionBoundaryRequestsV1' and
                      r['source-correspondence'] == 'Unchecked' for r in reports) and
                  {k: v for k, v in recorded.items() if k != 'transaction'} ==
                  {k: v for k, v in retained.items() if k != 'transaction'} and
                  {k: v for k, v in recorded_word.items() if k != 'retained-word-bytes'} ==
                  {k: v for k, v in retained_word.items() if k != 'retained-word-bytes'})
        checks.append(dict(case=name, passed=ok))

    name = 'replay-transaction-unrestorable-cut'
    query = (transaction_setup +
             f'!(bind! &capsule (history:capture (quote (cost:run '
             f'{replay_policy(transaction=True)} &jobs)) '
             '(ReplayLimits 8 8192 536870912)))\n'
             f'!(history:record &capsule {transaction_grant - 1})\n'
             '!(history:replay &capsule 2000000000)\n'
             '!(collapse (get-atoms &jobs))\n!(history:dispose &capsule)')
    run(name, query, definitions='')
    reports = replay_reports(name)
    trees = parse((d / (name + '.stdout')).read_text())
    stores = [form[0] for form in trees if isinstance(form, list) and len(form) == 1 and
              isinstance(form[0], list) and len(form[0]) == 2 and
              all(isinstance(row, list) and row and row[0] == 'exec' for row in form[0])]
    checks.append(dict(case=name, passed=(len(reports) == 2 and
        reports[0]['refusal'] == 'UnrestorableCut' and
        all(report['accepted-requests'] == 0 and report['replay-authenticated'] == 'False'
            for report in reports) and len(stores) == 1)))

    for stem in ('retained_replay', 'retained_transaction_replay'):
        name = 'replay-client-' + stem
        source = w / 'examples/prime/native_control' / (stem + '.metta')
        run(name, '', definitions='', source_path=source)
        actual = (d / (name + '.stdout')).read_text()
        expected = source.with_suffix('.expected').read_text()
        checks.append(dict(case=name, passed=parse(actual) == parse(expected)))


def forget_controls():
    library = str(w / 'lib/native_history.metta')
    imported = f'!(import! &self {library})\n'
    capture = (f'!(bind! &before (cost:run {policy(1, 19)} {body}))\n'
               '!(let $report &before $report)\n'
               '!(bind! &old (let $report &before '
               '(let (HistoryField Found residual $token) (history:field $report residual) '
               '(let $tail (cdr-atom $token) (car-atom $tail)))))\n')
    resume = f'!(select 0 {policy(1, 2000, False)} &old)'
    reference = run('forget-reference', f'!(cost:run {policy(1, 2000, False)} {body})')
    expected = reference[-1] if reference else None
    for cut in (0, 19, 49):
        name = f'forget-live-alias-{cut}'
        query = imported + capture.replace('(budget 19)', f'(budget {cut})')
        query += ('!(history:forget &before 0)\n'
                  '!(history:forget &before 131072)\n' + resume)
        parts = run(name, query)
        reports = history_reports(name, 'HistoryPolicy')
        ok = (expected is not None and len(parts) == 2 and len(reports) == 2 and
              parts[0]['values'] + parts[-1]['values'] == expected['values'] and
              parts[-1]['closed'] == 'True' and parts[-1]['work'] == expected['work'] and
              'history' not in parts[-1] and
              [r['status'] for r in reports] == ['Refused', 'Unrecorded'] and
              reports[0]['reason'] == 'InspectionLimit' and reports[1]['reason'] == 'None' and
              all(r['residual'] == parts[0]['residual'] and r['work'] == cut for r in reports) and
              [r['earlier'] for r in reports] == ['Unchanged', 'Unrecorded'] and
              parts[-1]['residual'] == parts[0]['residual'])
        checks.append(dict(case=name, passed=ok))

    weighted = base.replace('(= (history-coin) 3)', '(=w (history-coin) 3 2)')
    weighted = weighted.replace('(= (history-coin) 6)', '(=w (history-coin) 6 5)')
    weighted_body = f'(weight:read (algebra 0 1 + *) {body})'
    whole = run('forget-weighted-reference',
                f'!(cost:run {policy(1, 2000, False)} {weighted_body})', weighted)
    weighted_expected = whole[-1] if whole else None
    for cut in (19, 49):
        name = f'forget-weighted-alias-{cut}'
        query = imported + capture.replace(body, weighted_body).replace('(budget 19)', f'(budget {cut})')
        parts = run(name, query + '!(history:forget &before 131072)\n' + resume, weighted)
        reports = history_reports(name, 'HistoryPolicy')
        ok = (weighted_expected is not None and len(parts) == 2 and len(reports) == 1 and
              reports[0]['status'] == 'Unrecorded' and
              # Factors created after the cut use the ambient fresh allocator.
              # Compare its identity partition, order and coefficients jointly.
              normalize(parts[0]['values'] + parts[-1]['values']) == normalize(weighted_expected['values']) and
              (cut != 49 or parts[0]['values'] + parts[-1]['values'] == weighted_expected['values']) and
              len(weighted_expected['values']) == 3 and
              parts[-1]['work'] == weighted_expected['work'] and parts[-1]['closed'] == 'True' and
              'history' not in parts[-1])
        checks.append(dict(case=name, passed=ok))

    name = 'forget-late-recording-suffix'
    query = (imported + capture + '!(history:forget &before 131072)\n'
             f'!(select 0 {policy(1, 1, False)} &old)\n'
             f'!(select 0 {policy(1, 2000)} &old)')
    parts = run(name, query)
    late = parts[-1].get('history') if parts else None
    fields = {row[0]: row[1] for row in late[1:]
              if isinstance(row, list) and len(row) == 2} if isinstance(late, list) else {}
    ok = (expected is not None and len(parts) == 3 and
          sum((part['values'] for part in parts), []) == expected['values'] and
          'history' not in parts[1] and parts[1]['work'] == 20 and
          fields.get('start-work') == 20 and fields.get('earlier') == 'Unrecorded' and
          parts[-1]['closed'] == 'True' and parts[-1]['work'] == expected['work'])
    checks.append(dict(case=name, passed=ok))

    name = 'forget-truncated-recording'
    limited = capture.replace('(selections 512)', '(selections 1)').replace('(nodes 32)', '(nodes 0)')
    parts = run(name, imported + limited + '!(history:forget &before 131072)\n' + resume)
    reports = history_reports(name, 'HistoryPolicy')
    ok = (expected is not None and len(parts) == 2 and reports and reports[0]['status'] == 'Unrecorded' and
          tagged(parts[0].get('history'), 'status') == ['status', 'Incomplete'] and
          parts[0]['values'] + parts[-1]['values'] == expected['values'] and
          parts[-1]['closed'] == 'True' and 'history' not in parts[-1])
    checks.append(dict(case=name, passed=bool(ok)))

    name = 'forget-textual-and-duplicate-holder-refusal'
    query = (imported + capture +
             '!(bind! &textual (parse "(NativeHandle \\"prime-search\\" 1)"))\n'
             '!(history:forget &textual 131072)\n'
             '!(bind! &presented (let $token &old '
             '(Observation () (residual $token) (residual $token))))\n'
             '!(history:forget &presented 131072)\n' + resume)
    parts = run(name, query)
    reports = history_reports(name, 'HistoryPolicy')
    ok = (expected is not None and len(reports) == 2 and
          all(r['status'] == 'Refused' and r['reason'] == 'SourceOwner' and
              r['residual'] == 'None' for r in reports) and
          len(parts) == 2 and parts[0]['values'] + parts[-1]['values'] == expected['values'])
    checks.append(dict(case=name, passed=ok))

    name = 'forget-positional-values-are-not-fields'
    query = (imported + capture +
             '!(bind! &presented (let $token &old '
             '(Observation (residual $token) (residual $token))))\n'
             '!(history:forget &presented 131072)\n' + resume)
    parts = run(name, query)
    reports = history_reports(name, 'HistoryPolicy')
    ok = (expected is not None and len(reports) == 1 and reports[0]['status'] == 'Unrecorded' and
          len(parts) == 2 and parts[0]['values'] + parts[-1]['values'] == expected['values'] and
          'history' not in parts[-1])
    checks.append(dict(case=name, passed=ok))

    name = 'forget-report-is-completed-data'
    marker = 'FORGET-REPORT-MUST-STAY-DATA'
    definitions = base + f'(= (HistoryPolicy $a $b $c $d $e $f) (println! "{marker}"))\n'
    parts = run(name, imported + capture + '!(history:forget &before 131072)\n' + resume, definitions)
    reports = history_reports(name, 'HistoryPolicy')
    text = (d / (name + '.stdout')).read_text()
    ok = (expected is not None and len(reports) == 1 and reports[0]['status'] == 'Unrecorded' and
          not any(line.strip('"') == marker for line in text.splitlines()) and
          len(parts) == 2 and parts[0]['values'] + parts[-1]['values'] == expected['values'])
    checks.append(dict(case=name, passed=ok))

    name = 'forget-completed-effect-is-not-replayed'
    marker = 'FORGET-EFFECT-ONCE'
    effect = f'(= (forget-effect) (let $_ (println! "{marker}") (superpose (A B B))))\n'
    query = imported + capture.replace(body, '(forget-effect)')
    parts = run(name, query + '!(history:forget &before 131072)\n' + resume, effect)
    reports = history_reports(name, 'HistoryPolicy')
    stdout = (d / (name + '.stdout')).read_text()
    effects = [line.strip('"') for line in stdout.splitlines()
               if line.strip('"') == marker]
    ok = (len(parts) == 2 and len(reports) == 1 and
          reports[0]['status'] == 'Unrecorded' and effects == [marker] and
          stdout.find(marker) < stdout.find('[(Observation') and
          parts[0]['closed'] == 'False' and parts[-1]['closed'] == 'True' and
          parts[0]['values'] + parts[-1]['values'] == ['A', 'B', 'B'] and
          parts[-1]['residual'] == parts[0]['residual'] and 'history' not in parts[-1])
    checks.append(dict(case=name, passed=ok, effects=effects))


def fields(history):
    return {x[0]: x[1] for x in history[1:] if isinstance(x, list) and len(x) == 2}


def recording_policy_controls():
    """Stop a real retained observer, then optionally record a fresh suffix."""
    imported = f'!(import! &self {w / "lib/native_history.metta"})\n'
    marker = 'HISTORY-POLICY-ONCE'
    definitions = (
        f'(= (policy-coin) (let $_ (println! "{marker}") (superpose (2 2 3))))\n'
        '(= (policy-step $x) (+ $x 1))\n'
        '(= (policy-name 3) Three)\n(= (policy-name 4) Four)\n'
        '(= (policy-pair $x) (Pair (policy-name (policy-step $x)) (policy-step $x)))\n'
        '(= (policy-jobs $x) (policy-pair $x))\n(= (policy-jobs $x) Ready)\n')
    expected = sorted(map(repr, ['Ready', ['Pair', 'Three', 3],
                                ['Pair', 'Three', 3], ['Pair', 'Four', 4]]))
    for width in (1, 2):
        for cut in (7, 20):
            for restart in (False, True):
                name = f'history-policy-w{width}-c{cut}-restart-{int(restart)}'
                stopped = policy(width, 3, False, stop=True)
                final = policy(width, 5000, restart)
                if restart:
                    final = final.replace('(selections 512)', '(selections 4)')
                query = (imported +
                    f'!(bind! &before (cost:run {policy(width, cut)} (policy-jobs (policy-coin))))\n'
                    '!(let $report &before $report)\n'
                    '!(bind! &old (let $report &before '
                    '(let (HistoryField Found residual $token) (history:field $report residual) '
                    '(let $tail (cdr-atom $token) (car-atom $tail)))))\n'
                    f'!(select 0 {stopped} &old)\n'
                    f'!(select 0 {final} &old)\n'
                    '!(let $report &before $report)\n')
                parts = run(name, query, definitions)
                stdout = (d / (name + '.stdout')).read_text()
                effects = sum(line.strip('"') == marker for line in stdout.splitlines())
                ok = len(parts) == 4 and effects == 1
                if ok:
                    before, gap, result, retained = parts
                    h0, h1, h2 = [fields(part['history']) for part in (before, gap, result)]
                    ok = (before == retained and
                          result['closed'] == 'True' and
                          result['residual'] == before['residual'] and
                          sorted(map(repr, before['values'] + gap['values'] + result['values'])) == expected and
                          h0['recording'] == 'True' and h1['recording'] == 'False' and
                          h1['end-work'] == before['work'] and
                          all(h1[key] == h0[key] for key in
                              ('selections', 'nodes', 'omitted-nodes', 'reservations')))
                    if restart:
                        ok = ok and (h2['recording'] == 'True' and
                            h2['start-work'] == gap['work'] and
                            h2['end-work'] == result['work'] and
                            len(h2['selections']) == 4 and
                            h2['omitted-selections'] > 0 and h2['status'] == 'Incomplete')
                    else:
                        ok = ok and (h2['recording'] == 'False' and
                            h2['end-work'] == before['work'] and
                            all(h2[key] == h0[key] for key in
                                ('selections', 'nodes', 'omitted-nodes', 'reservations')))
                checks.append(dict(case=name, passed=ok, effects=effects))


def diagnostic_controls(recording=True):
    ordinary_env = {key: value for key, value in os.environ.items()
                    if key != 'CETTA_PRIME_NEED_TRACE'}
    traced_env = {**ordinary_env, 'CETTA_PRIME_NEED_TRACE': '1'}
    for width in (1, 2):
        for empty in (False, True):
            definitions = (
                '(= diagnostic-source (empty))\n(= diagnostic-source (empty))\n'
                if empty else
                '(= diagnostic-source 1)\n'
                '(= diagnostic-source (let $_ (println! "DIAGNOSTIC-ONCE") (+ 1 2)))\n')
            definitions += '(= (diagnostic-pair $x) (Pair $x $x))\n'
            for recorded in ((False, True) if recording else (False,)):
                name = f'diagnostics-w{width}-empty{int(empty)}-history{int(recorded)}'
                query = f'!(select 0 {policy(width, 2000, recorded)} (diagnostic-pair diagnostic-source))'
                off = run(name + '-off', query, definitions, environment=ordinary_env)
                on = run(name + '-on', query, definitions, environment=traced_env)
                off_err = (d / (name + '-off.stderr')).read_text()
                on_err = (d / (name + '-on.stderr')).read_text()
                expected = [] if empty else [['Pair', 1, 1], ['Pair', 3, 3]]
                # A named source can use the synchronous force or the retained
                # force cursor. Diagnostics must observe the route actually
                # taken without changing its values, work or history.
                force_traced = '[factive ' in on_err or '[sforce ' in on_err
                ok = len(off) == len(on) == 1 and not off_err and force_traced
                for label, observation in (('off', off), ('on', on)):
                    ok = ok and bool(observation)
                    if observation:
                        result = observation[0]
                        ok = ok and (sorted(map(repr, result['values'])) == sorted(map(repr, expected)) and
                            result['closed'] == 'True' and result['status'] == 'closed' and
                            ('history' in result) == recorded)
                    effects = sum(line.strip('"') == 'DIAGNOSTIC-ONCE'
                                  for line in (d / (name + '-' + label + '.stdout')).read_text().splitlines())
                    ok = ok and effects == (0 if empty else 1)
                if ok and width == 1:
                    ok = off[0]['work'] == on[0]['work']
                    if recorded:
                        ok = ok and normalize(off[0]['history']) == normalize(on[0]['history'])
                checks.append(dict(case=name, passed=ok))


    marker = 'DIAGNOSTIC-SCHEDULE-ONCE'
    definitions = f'(= (diagnostic-work) (let $_ (println! "{marker}") (+ 4 5)))\n'
    body = '(let $p (delay (diagnostic-work)) (Pair (force $p) (force $p)))'
    for width in (1, 2):
        for recorded in ((False, True) if recording else (False,)):
            name = f'diagnostics-owned-force-w{width}-history{int(recorded)}'
            query = f'!(select 0 {policy(width, 2000, recorded)} {body})'
            off = run(name + '-off', query, definitions, environment=ordinary_env)
            on = run(name + '-on', query, definitions, environment=traced_env)
            ok = len(off) == len(on) == 1
            if ok:
                ok = same_observation(off[0], on[0]) and off[0]['values'] == [['Pair', 9, 9]]
                ok = ok and off[0]['closed'] == 'True' and off[0]['status'] == 'closed'
                if recorded and width == 1:
                    ok = ok and normalize(off[0]['history']) == normalize(on[0]['history'])
            for label in ('off', 'on'):
                output = (d / (name + '-' + label + '.stdout')).read_text()
                ok = ok and sum(line.strip('"') == marker for line in output.splitlines()) == 1
            ok = ok and not (d / (name + '-off.stderr')).read_text()
            ok = ok and '[sforce ' in (d / (name + '-on.stderr')).read_text()
            checks.append(dict(case=name, passed=ok))

    ordinary_cases = (
        ('strict', '(= (diagnostic-inc $x) (+ $x 1))\n',
         '!(diagnostic-inc (superpose (2 2)))', [[3, 3]], '[strict]'),
        ('fold', '', '!(foldl-atom (1 2 3) 0 $acc $x (+ $acc $x))', [[6]], None),
    )
    for label, definitions, query, expected, trace in ordinary_cases:
        name = 'diagnostics-ordinary-' + label
        for suffix, environment in (('off', ordinary_env), ('on', traced_env)):
            run(name + '-' + suffix, query, definitions, environment=environment)
        plain = (d / (name + '-off.stdout')).read_text()
        traced = (d / (name + '-on.stdout')).read_text()
        off_err = (d / (name + '-off.stderr')).read_text()
        on_err = (d / (name + '-on.stderr')).read_text()
        checks.append(dict(case=name, passed=parse(plain) == parse(traced) == expected and
                           not off_err and (trace is None or trace in on_err)))

    # Native sharing diagnostics select the force operation at entry. They
    # must neither disappear when requested nor duplicate the shared effect.
    name = 'diagnostics-native-force-policy'
    marker = 'FORCE-POLICY-ONCE'
    definitions = f'(= (force-policy-work) (chain (println! "{marker}") $ignored (+ 4 5)))\n'
    body = '(let $p (delay (force-policy-work)) (Pair (force $p) (force $p)))'
    quiet = {key: value for key, value in ordinary_env.items()
             if key != 'CETTA_PRIME_NATIVE_CONTROL_TRACE'}
    loud = {**quiet, 'CETTA_PRIME_NATIVE_CONTROL_TRACE': '1'}
    samples = []
    for suffix, environment in (('off', quiet), ('on', loud)):
        samples.append(run(name + '-' + suffix,
                           f'!(select 0 {policy(1, 2000, False)} {body})',
                           definitions, environment=environment))
    checks.append(dict(case=name, passed=
        len(samples[0]) == len(samples[1]) == 1 and
        same_observation(samples[0][0], samples[1][0]) and
        samples[0][0]['values'] == [['Pair', 9, 9]] and
        all((d / (name + '-' + suffix + '.stdout')).read_text().count(marker) == 1
            for suffix in ('off', 'on')) and
        not (d / (name + '-off.stderr')).read_text() and
        '[need-share]' in (d / (name + '-on.stderr')).read_text()))

    name = 'diagnostics-native-publication-and-rational'
    body = ('(chain (add-atom &self (DiagnosticPublication 7)) $ignored '
            '(Pair (weight:exact-rational? 7) (weight:exact-rational? 1.5)))')
    samples = []
    for suffix, environment in (('off', quiet), ('on', loud)):
        samples.append(run(name + '-' + suffix,
                           f'!(select 0 {policy(1, 2000, False)} {body})',
                           definitions='', environment=environment))
    notes = (d / (name + '-on.stderr')).read_text()
    checks.append(dict(case=name, passed=
        len(samples[0]) == len(samples[1]) == 1 and
        same_observation(samples[0][0], samples[1][0]) and
        samples[0][0]['values'] == [['Pair', 'True', 'False']] and
        samples[0][0]['closed'] == 'True' and
        not (d / (name + '-off.stderr')).read_text() and
        'own-write phase=before' in notes and 'own-write phase=after' in notes and
        'exact rational value=7 result=1' in notes and
        'exact rational value=1.5 result=0' in notes))


def origin_controls(recording=True):
    marker = 'ORIGIN-MUST-NOT-FORCE'
    definitions = f'(= (origin-effect) (println! {marker}))\n'
    cases = [
        ('unforced', '(observe-origin (delay (origin-effect)))',
         [['suspension:view', ['origin-effect']]], True),
        ('denied', '(observe-origin (suspension:restrict (delay 7) (suspension:rights force)))',
         [['Error', ['observe-origin', 'suspension:opaque'], 'suspension:inspect-denied']], False),
        ('ordinary-data', '(observe-origin 17)', [['observe-origin', 17]], False),
        ('captured', '(let $x 7 (let $p (delay (Pair $x (+ 1 2))) (observe-origin $p)))',
         [['suspension:view', ['Pair', 7, ['+', 1, 2]]]], True),
    ]
    def inspections(observation):
        return [node for node in walk(observation.get('history', []))
                if node[0] == 'NeedEvent' and node[1] == 1]
    for width in (1, 2):
        for label, expression, expected, inspected in cases:
            stem = f'origin-w{width}-{label}'
            off = run(stem + '-off', f'!(select 0 {policy(width, 2000, False)} {expression})', definitions)
            ok = len(off) == 1 and off[0]['values'] == expected and off[0]['closed'] == 'True'
            ok = ok and not inspections(off[0]) and 'history' not in off[0]
            if recording:
                on = run(stem + '-on', f'!(select 0 {policy(width, 2000)} {expression})', definitions)
                ok = ok and len(on) == 1 and all(off[0][key] == on[0][key]
                    for key in ('values', 'work', 'closed', 'satisfied', 'status'))
                ok = ok and bool(inspections(on[0])) == inspected
            effects = sum(line.strip('"') == marker for row in runs[-(2 if recording else 1):]
                          for line in (d / (row['name'] + '.stdout')).read_text().splitlines())
            checks.append(dict(case=stem, passed=ok and effects == 0, effects=effects))
        if recording:
            expression = '(observe-origin (delay (origin-effect)))'
            for restart in (False, True):
                stem = f'origin-policy-w{width}-restart{int(restart)}'
                if restart:
                    query = (f'!(bind! &origin-saved (select 0 {policy(width, 0)} {expression})) '
                        f'!(bind! &origin-tail (let {pattern} &origin-saved $r)) '
                        f'!(select 0 {policy(width, 0, stop=True)} &origin-tail) '
                        f'!(select 0 {policy(width, 2000)} &origin-tail)')
                else:
                    query = (f'!(let {pattern} (select 0 {policy(width, 0)} {expression}) '
                        f'(select 0 {policy(width, 2000, stop=True)} $r))')
                parts = run(stem, query, definitions)
                ok = bool(parts) and parts[-1]['values'] == [['suspension:view', ['origin-effect']]]
                ok = ok and parts[-1]['closed'] == 'True' and bool(inspections(parts[-1])) == restart
                ok = ok and not any(line.strip('"') == marker for line in (d / (stem + '.stdout')).read_text().splitlines())
                checks.append(dict(case=stem, passed=ok))


def resampling_controls(recording=True):
    marker = 'RESAMPLING-EFFECT'
    definitions = (f'(= resampling-coin 1) (= resampling-coin 2) '
                   f'(= resampling-effect (chain (println! {marker}) $ignored 7)) ')
    cases = [
        ('shared-choice', '(let $p (delay resampling-coin) (Pair (force $p) (force $p)))',
         [['Pair', 1, 1], ['Pair', 2, 2]], 0, False),
        ('fresh-choice', '(let $p (resample resampling-coin) (Pair (force $p) (force $p)))',
         [['Pair', 1, 1], ['Pair', 1, 2], ['Pair', 2, 1], ['Pair', 2, 2]], 0, True),
        ('shared-effect', '(let $p (delay resampling-effect) (Pair (force $p) (force $p)))',
         [['Pair', 7, 7]], 1, False),
        ('fresh-effect', '(let $p (resample resampling-effect) (Pair (force $p) (force $p)))',
         [['Pair', 7, 7]], 2, True),
        ('empty', '(let $p (resample (empty)) (force $p))', [], 0, True),
    ]
    for width in (1, 2):
        for label, expression, expected, expected_effects, resampled in cases:
            stem = f'resampling-w{width}-{label}'
            off = run(stem + '-off', f'!(select 0 {policy(width, 4000, False)} {expression})', definitions)
            ok = len(off) == 1 and off[0]['closed'] == 'True' and 'history' not in off[0]
            ok = ok and sorted(map(repr, off[0]['values'])) == sorted(map(repr, expected))
            if recording:
                on = run(stem + '-on', f'!(select 0 {policy(width, 4000)} {expression})', definitions)
                ok = ok and len(on) == 1 and on[0]['closed'] == 'True'
                ok = ok and sorted(map(repr, on[0]['values'])) == sorted(map(repr, expected))
                events = [node for node in walk(on[0]['history']) if node[0] == 'NeedEvent' and node[1] == 5]
                ok = ok and bool(events) == resampled
            effects = []
            for row in runs[-(2 if recording else 1):]:
                effects.append(sum(line.strip('"') == marker
                    for line in (d / (row['name'] + '.stdout')).read_text().splitlines()))
            checks.append(dict(case=stem, passed=ok and all(n == expected_effects for n in effects), effects=effects))


def breakpoint_controls(recording=True):
    """Inspect owned entries without executing them or replaying the prefix."""
    imported = f'!(import! &self {w / "lib/native_history.metta"})\n'
    capture = ('!(let $report &before $report)\n'
               '!(bind! &resume (let $report &before '
               '(let (HistoryField Found residual $token) (history:field $report residual) '
               '(let $tail (cdr-atom $token) (car-atom $tail)))))\n')

    def stop_policy(option, recording=''):
        return ('(search-policy (control FIFO (demand all) (retain True) '
                f'(budget 10000) {option} {recording}))')

    for binder in ('let', 'chain'):
        for recorded in ((False, True) if recording else (False,)):
            name = f'breakpoint-{binder}-recorded-{int(recorded)}'
            before = ('(let $_ (println! "BREAK-BEFORE") (break:stop 7))'
                      if binder == 'let' else
                      '(chain (println! "BREAK-BEFORE") $_ (break:stop 7))')
            definitions = ('(= (break:stop $x) '
                           f'({binder} ' +
                           ('$_ (println! "BREAK-AFTER")' if binder == 'let' else
                            '(println! "BREAK-AFTER") $_') +
                           ' (superpose ($x $x 11))))\n')
            active = stop_policy('(breakpoint (break:stop $any))', history if recorded else '')
            query = (imported + f'!(bind! &before (select 0 {active} {before}))\n' + capture +
                     f'!(select 0 {stop_policy("")} &resume)\n' +
                     f'!(select 0 {stop_policy("(breakpoint False)", "(history False)")} &resume)\n')
            parts = run(name, query, definitions)
            effects = [line.strip('"') for line in (d / (name + '.stdout')).read_text().splitlines()
                       if line.strip('"') in ('BREAK-BEFORE', 'BREAK-AFTER')]
            ok = len(parts) == 3 and effects == ['BREAK-BEFORE', 'BREAK-AFTER']
            if ok:
                held, repeated, done = parts
                notice = replay_fields(held['breakpoint'])
                again = replay_fields(repeated['breakpoint'])
                ok = (held['values'] == repeated['values'] == [] and
                      held['status'] == repeated['status'] == 'breakpoint' and
                      held['work'] == repeated['work'] and
                      held['residual'] == repeated['residual'] == done['residual'] and
                      notice['state'] == again['state'] == 'Hit' and
                      notice['occurrence'] == again['occurrence'] != 0 and
                      again['inspections'] == notice['inspections'] + 1 and
                      notice['operation'] == 'Call' and
                      done['closed'] == 'True' and done['values'] == [7, 7, 11])
                if recorded:
                    recorded_fields = fields(held['history'])
                    stopped_fields = fields(done['history'])
                    ok = ok and stopped_fields['recording'] == 'False' and all(
                        stopped_fields[key] == recorded_fields[key] for key in
                        ('selections', 'nodes', 'omitted-nodes', 'reservations', 'end-work'))
            checks.append(dict(case=name, passed=ok, effects=effects))

    for variant, option, budget in (
            ('incomplete', '(breakpoint (break:stop $any) 0)', 10000),
            ('fuel-before-inspection', '(breakpoint (break:stop $any) 0)', 0),
            ('miss', '(breakpoint (never:entered))', 10000)):
        name = 'breakpoint-' + variant
        policy_text = stop_policy(option).replace('(budget 10000)', f'(budget {budget})')
        parts = run(name, f'!(select 0 {policy_text} (break:stop 7))',
                    '(= (break:stop $x) $x)\n')
        ok = len(parts) == 1
        if ok:
            part = parts[0]
            notice = replay_fields(part['breakpoint'])
            if variant == 'incomplete':
                ok = (part['status'] == 'inspection-incomplete' and part['work'] == 0 and
                      part['values'] == [] and notice['state'] == 'InspectionIncomplete' and
                      notice['operation'] == 'Normalize')
            elif variant == 'fuel-before-inspection':
                ok = (part['status'] == 'suspended' and part['work'] == 0 and
                      part['values'] == [] and notice['inspections'] == 0)
            else:
                ok = (part['closed'] == 'True' and part['values'] == [7] and
                      notice['state'] == 'Armed' and notice['inspections'] > 0)
        checks.append(dict(case=name, passed=ok))


    name = 'breakpoint-choice-entry'
    active = stop_policy('(breakpoint (break:stop $any))')
    query = (imported + f'!(bind! &before (select 0 {active} '
             '(superpose ((break:stop 7) (break:stop 8)))))\n' + capture +
             f'!(select 0 {stop_policy("(breakpoint False)")} &resume)')
    parts = run(name, query, '(= (break:stop $x) $x)\n')
    checks.append(dict(case=name, passed=len(parts) == 2 and
                       parts[0]['status'] == 'breakpoint' and not parts[0]['values'] and
                       replay_fields(parts[0]['breakpoint'])['operation'] == 'Call' and
                       parts[1]['closed'] == 'True' and parts[1]['values'] == [7, 8]))

    name = 'breakpoint-chain-order'
    definitions = ('(= (break:source) (chain (println! "BREAK-SOURCE-1") $_ 1))\n'
                   '(= (break:source) (chain (println! "BREAK-SOURCE-2") $_ 2))\n')
    query = (f'!(select 0 {stop_policy("(breakpoint (never:entered))")} '
             '(chain (break:source) $x (chain (println! "BREAK-BODY") $_ $x)))')
    parts = run(name, query, definitions)
    effects = [line.strip('"') for line in (d / (name + '.stdout')).read_text().splitlines()
               if line.strip('"').startswith('BREAK-')]
    checks.append(dict(case=name, passed=len(parts) == 1 and parts[0]['values'] == [1, 2] and
                       effects == ['BREAK-SOURCE-1', 'BREAK-SOURCE-2', 'BREAK-BODY', 'BREAK-BODY'],
                       effects=effects))

    name = 'breakpoint-invalidated'
    active = stop_policy('(breakpoint (break:stop $any))')
    query = (imported + f'!(bind! &before (select 0 {active} (break:stop 7)))\n' + capture +
             '!(add-atom &self (= (break:stop $x) Extra))\n' +
             f'!(select 0 {stop_policy("(breakpoint False)")} &resume)')
    parts = run(name, query, '(= (break:stop $x) $x)\n')
    checks.append(dict(case=name, passed=len(parts) == 2 and
                       parts[0]['status'] == 'breakpoint' and
                       parts[1]['status'] == 'invalidated' and
                       parts[0]['work'] == parts[1]['work'] and not parts[1]['values']))

    if recording:
        name = 'breakpoint-checkpoint-replay'
        replay = replay_policy().replace('(demand all)',
                                         '(demand all) (breakpoint (break:stop $any))')
        query = (f'!(bind! &capsule (history:capture (quote (cost:run {replay} (break:stop 7))) '
                 '(ReplayLimits 8 8192 536870912)))\n'
                 '!(history:record &capsule 1000)\n!(history:record &capsule 1000)\n'
                 '!(history:replay &capsule 2000000000)\n'
                 '!(history:replay &capsule 2000000000)\n!(history:dispose &capsule)')
        run(name, query, '(= (break:stop $x) $x)\n')
        reports = replay_reports(name)
        ok = len(reports) == 4
        if ok:
            held = [replay_observation(report) for report in reports[:2]]
            ok = (all(part['status'] == 'breakpoint' and part['work'] == 0 for part in held) and
                  reports[-1]['accepted-requests'] == 2 and
                  reports[-1]['unconsumed-requests'] == 0 and
                  reports[-1]['replay-authenticated'] == 'True' and
                  all(report['refusal'] == 'None' for report in reports))
        checks.append(dict(case=name, passed=ok))


def resource_breakpoint_controls(recording=True):
    """Pause live publication without consuming inputs or lending authority."""
    imported = f'!(import! &self {w / "lib/native_history.metta"})\n'
    rows = ('!(bind! &jobs (new-space))\n!(add-atom &jobs Permit)\n'
            '!(add-atom &jobs (exec 10 (I (take Permit) (absent Blocked)) '
            '(O (+ (Winner Alpha)))))\n')
    capture = ('!(let $report &before $report)\n'
               '!(bind! &resume (let $report &before '
               '(let (HistoryField Found residual $token) (history:field $report residual) '
               '(let $tail (cdr-atom $token) (car-atom $tail)))))\n')
    world = ('!(let $winners (collapse (match &jobs (Winner $who) $who)) '
             '(Winners $winners))\n'
             '!(let $permits (collapse (match &jobs Permit Present)) '
             '(Permits $permits))\n')

    def control(option, budget=10000, observer='', profile='Transaction', width=1):
        return (f'(search-policy (control FIFO (resources {profile}) '
                f'(workers {width}) (retain True) (budget {budget}) {option} {observer}))')

    active = control('(breakpoint (exec 10 $inputs $outputs))')
    resumed = control('(breakpoint False)')
    prefix = imported + rows + f'!(bind! &before (select 1 {active} &jobs))\n' + capture

    for variant, change, winner in (
            ('unchanged', '', 'Alpha'),
            ('unrelated', '!(add-atom &jobs Unrelated)\n', 'Alpha'),
            ('stale-absence', '!(add-atom &jobs Blocked)\n', None),
            ('stale-take',
             '!(add-atom &jobs (exec 0 (I (take Permit)) (O (+ (Winner Beta)))))\n'
             '!(select 1 (search-policy (control WO (resources Transaction) '
             '(key (exec $p $i $o) $p) (budget 10000))) &jobs)\n', 'Beta')):
        name = 'resource-breakpoint-' + variant
        query = prefix + change + f'!(select 1 {resumed} &resume)\n' + world
        parts = run(name, query, definitions='')
        trees = list(walk(parse((d / (name + '.stdout')).read_text())))
        ok = (len(parts) == (3 if variant == 'stale-take' else 2) and
              parts[0]['status'] == 'breakpoint' and not parts[0]['values'] and
              replay_fields(parts[0]['breakpoint'])['operation'] == 'ResourceCommit' and
              replay_fields(parts[0]['breakpoint'])['coverage'] == 'OwnedPrimeResourceCommits' and
              ['Winners', [winner] if winner else []] in trees and
              ['Permits', ['Present'] if variant == 'stale-absence' else []] in trees)
        if ok:
            ok = (parts[-1]['residual'] == parts[0]['residual'] and
                  len(parts[-1]['values']) == (1 if winner == 'Alpha' else 0))
        checks.append(dict(case=name, passed=ok))

    for recorded in ((False, True) if recording else (False,)):
        name = f'resource-breakpoint-repeat-recorded-{int(recorded)}'
        selected = control('(breakpoint (exec 10 $inputs $outputs))',
                           observer=history if recorded else '')
        query = (imported + rows + f'!(bind! &before (select 1 {selected} &jobs))\n' + capture +
                 f'!(select 1 {control("")} &resume)\n' + world +
                 ('!(history:forget &before 131072)\n' if recorded else '') +
                 f'!(select 1 {resumed} &resume)\n' + world)
        parts = run(name, query, definitions='')
        trees = list(walk(parse((d / (name + '.stdout')).read_text())))
        ok = len(parts) == 3
        if ok:
            held, again, done = parts
            first = replay_fields(held['breakpoint'])
            second = replay_fields(again['breakpoint'])
            ok = (held['status'] == again['status'] == 'breakpoint' and
                  held['work'] == again['work'] and first['occurrence'] != 0 and
                  first['occurrence'] == second['occurrence'] and
                  second['inspections'] == first['inspections'] + 1 and
                  len(done['values']) == 1 and held['residual'] == done['residual'] and
                  ['Permits', ['Present']] in trees and ['Winners', ['Alpha']] in trees)
            if recorded:
                reports = history_reports(name, 'HistoryPolicy')
                ok = ok and len(reports) == 1 and reports[0]['status'] == 'Unrecorded'
        checks.append(dict(case=name, passed=ok))

    for variant, option, budget in (
            ('incomplete', '(breakpoint (exec 10 $inputs $outputs) 0)', 10000),
            ('zero-budget', '(breakpoint (exec 10 $inputs $outputs))', 0),
            ('miss', '(breakpoint (exec 99 $inputs $outputs))', 10000)):
        name = 'resource-breakpoint-' + variant
        parts = run(name, imported + rows +
                    f'!(select 1 {control(option, budget)} &jobs)\n' + world,
                    definitions='')
        trees = list(walk(parse((d / (name + '.stdout')).read_text())))
        ok = len(parts) == 1
        if ok:
            part = parts[0]
            notice = replay_fields(part['breakpoint'])
            ok = (['Winners', ['Alpha'] if variant == 'miss' else []] in trees and
                  ['Permits', [] if variant == 'miss' else ['Present']] in trees)
            if variant == 'incomplete':
                ok = ok and part['status'] == 'inspection-incomplete' and not part['values']
            elif variant == 'zero-budget':
                ok = ok and part['work'] == notice['inspections'] == 0 and not part['values']
            else:
                ok = ok and len(part['values']) == 1 and notice['state'] == 'Armed'
        checks.append(dict(case=name, passed=ok))

    name = 'resource-breakpoint-pure-caller'
    query = (prefix + '!(select 1 (search-policy (control WO (budget 10000) '
             f'(key $candidate (select 1 {resumed} &resume)))) (superpose (1 2)))\n' +
             world + f'!(select 1 {resumed} &resume)\n' + world)
    parts = run(name, query, definitions='')
    trees = list(walk(parse((d / (name + '.stdout')).read_text())))
    checks.append(dict(case=name, passed=len(parts) >= 2 and
                       parts[0]['status'] == 'breakpoint' and
                       ['Permits', ['Present']] in trees and
                       len(parts[-1]['values']) == 1 and ['Winners', ['Alpha']] in trees))

    if recording:
        name = 'resource-breakpoint-truncated-history'
        selected = control('(breakpoint (exec 10 $inputs $outputs))', observer=
                           '(history (selections 0) (nodes 0) (bytes 0) (owners 0))')
        parts = run(name, imported + rows +
                    f'!(bind! &before (select 1 {selected} &jobs))\n' + capture +
                    f'!(select 1 {resumed} &resume)\n' + world, definitions='')
        trees = list(walk(parse((d / (name + '.stdout')).read_text())))
        checks.append(dict(case=name, passed=len(parts) == 2 and
                           parts[0]['status'] == 'breakpoint' and
                           fields(parts[0]['history'])['status'] == 'Incomplete' and
                           len(parts[-1]['values']) == 1 and
                           ['Winners', ['Alpha']] in trees and ['Permits', []] in trees))


    raw_exec = '(exec 0 (, Permit) (O (+ (Winner Alpha)) (- Permit)))'
    raw_rows = ('!(bind! &jobs (new-space))\n!(add-atom &jobs Permit)\n'
                f'!(add-atom &jobs {raw_exec})\n')
    raw_world = world + ('!(let $keys (collapse (match &jobs (exec $k $i $o) $k)) '
                         '(Directives $keys))\n')
    def raw_control(option='', budget=10000, observer=''):
        return (f'(search-policy (control FIFO (resources MM2) (workers 1) '
                f'(budget {budget}) {option} {observer}))')
    armed = '(breakpoint (exec 0 $inputs $outputs))'
    raw_resumed = raw_control('(breakpoint False)')
    for recorded in ((False, True) if recording else (False,)):
        name = f'mm2-activation-repeat-recorded-{int(recorded)}'
        query = (imported + raw_rows +
                 f'!(bind! &before (select 1 {raw_control(armed, observer=history if recorded else "")} &jobs))\n' +
                 capture + raw_world + f'!(select 1 {raw_control()} &resume)\n' + raw_world +
                 ('!(history:forget &before 131072)\n' if recorded else '') +
                 f'!(select 1 {raw_resumed} &resume)\n' + raw_world +
                 f'!(select 2 {raw_resumed} &resume)\n' + raw_world)
        parts = run(name, query, definitions='')
        trees = list(walk(parse((d / (name + '.stdout')).read_text())))
        states = [node[1] for node in trees if node[0] == 'Directives']
        ok = len(parts) == 4 and states == [[0], [0], [], []]
        if ok:
            held, again, done, exhausted = parts
            first, second = (replay_fields(part['breakpoint']) for part in (held, again))
            ok = (held['status'] == again['status'] == 'breakpoint' and
                  held['work'] == again['work'] and
                  first['operation'] == second['operation'] == 'ResourceActivation' and
                  first['coverage'] == 'OwnedPrimeResourceActivations' and
                  first['occurrence'] == second['occurrence'] != 0 and
                  second['inspections'] == first['inspections'] + 1 and
                  len(done['values']) == 1 and not exhausted['values'] and
                  held['residual'] == done['residual'] and
                  ['Winners', ['Alpha']] in trees and ['Permits', ['Present']] in trees)
            if recorded:
                policies = history_reports(name, 'HistoryPolicy')
                ok = ok and len(policies) == 1 and policies[0]['status'] == 'Unrecorded'
        checks.append(dict(case=name, passed=ok))

    for variant, change in (
            ('append', '!(add-atom &jobs Unrelated)\n'),
            ('replace-equal', f'!(remove-atom &jobs {raw_exec})\n!(add-atom &jobs {raw_exec})\n')):
        name = 'mm2-activation-stale-' + variant
        query = (imported + raw_rows + f'!(bind! &before (select 1 {raw_control(armed)} &jobs))\n' +
                 capture + change + f'!(select 1 {raw_resumed} &resume)\n' + raw_world)
        parts = run(name, query, definitions='')
        trees = list(walk(parse((d / (name + '.stdout')).read_text())))
        checks.append(dict(case=name, passed=len(parts) == 2 and
            parts[0]['status'] == 'breakpoint' and not parts[-1]['values'] and
            'StaleActivationSource' in str(parts[-1]) and
            ['Directives', [0]] in trees and ['Permits', ['Present']] in trees and
            ['Winners', []] in trees))

    for variant, option, budget in (
            ('incomplete', '(breakpoint (exec 0 $inputs $outputs) 0)', 10000),
            ('zero-budget', armed, 0),
            ('miss', '(breakpoint (exec 99 $inputs $outputs))', 10000)):
        name = 'mm2-activation-' + variant
        parts = run(name, imported + raw_rows +
                    f'!(select 1 {raw_control(option, budget)} &jobs)\n' + raw_world, definitions='')
        trees = list(walk(parse((d / (name + '.stdout')).read_text())))
        ok = len(parts) == 1
        if ok:
            part = parts[0]
            if variant == 'miss':
                ok = len(part['values']) == 1 and ['Directives', []] in trees
            else:
                ok = (not part['values'] and ['Directives', [0]] in trees and
                      ['Permits', ['Present']] in trees and ['Winners', []] in trees)
                if variant == 'incomplete':
                    ok = ok and part['status'] == 'inspection-incomplete'
                else:
                    ok = ok and part['work'] == replay_fields(part['breakpoint'])['inspections'] == 0
        checks.append(dict(case=name, passed=ok))

    name = 'mm2-activation-pure-admission-cannot-resume'
    query = (imported + raw_rows + f'!(bind! &before (select 1 {raw_control(armed)} &jobs))\n' +
             capture + f'(= (resume-verdict $weight) (chain (select 1 {raw_resumed} &resume) $ignored True))\n'
             '(=w (resume-guarded) GuardedBody 2 (admit resume-verdict))\n'
             '!(weight:read (algebra 0 1 + *) (resume-guarded))\n' + raw_world +
             f'!(select 1 {raw_resumed} &resume)\n' + raw_world)
    parts = run(name, query, definitions='')
    trees = list(walk(parse((d / (name + '.stdout')).read_text())))
    checks.append(dict(case=name, passed=len(parts) == 3 and
        parts[0]['status'] == 'breakpoint' and parts[1]['status'] == 'fault' and
        not parts[1]['values'] and len(parts[-1]['values']) == 1 and
        [node[1] for node in trees if node[0] == 'Directives'] == [[0], []]))


    from test_prime_native_resources import world_observations
    snapshot_exec = '(exec Take (I (take (Token $x))) (O (+ (Taken $x))))'
    snapshot_rows = ('!(bind! &jobs (new-space))\n!(add-atom &jobs (Token A))\n'
                     '!(add-atom &jobs (Token B))\n' + f'!(add-atom &jobs {snapshot_exec})\n')
    snapshot_live = '!(let $rows (collapse (get-atoms &jobs)) (LiveStore $rows))\n'
    def snapshot_control(option='', budget=10000, observer=''):
        return control(option, budget, observer, '(Transaction worlds)')
    snapshot_armed = '(breakpoint (exec Take $inputs $outputs))'
    snapshot_resumed = snapshot_control('(breakpoint False)')
    reference = run('snapshot-breakpoint-reference', imported + snapshot_rows +
                    f'!(select 100 {snapshot_control()} &jobs)', definitions='')
    expected = world_observations(reference[-1])
    for recorded in ((False, True) if recording else (False,)):
        name = f'snapshot-breakpoint-repeat-recorded-{int(recorded)}'
        query = (imported + snapshot_rows +
                 f'!(bind! &before (select 100 {snapshot_control(snapshot_armed, observer=history if recorded else "")} &jobs))\n' +
                 capture + snapshot_live + f'!(select 100 {snapshot_control()} &resume)\n' + snapshot_live +
                 ('!(history:forget &before 131072)\n' if recorded else '') +
                 f'!(select 1 {snapshot_resumed} &resume)\n' +
                 f'!(select 100 {snapshot_resumed} &resume)\n' + snapshot_live)
        parts = run(name, query, definitions='')
        trees = list(walk(parse((d / (name + '.stdout')).read_text())))
        live = [node[1] for node in trees if node[0] == 'LiveStore']
        ok = len(parts) == 4 and len(live) == 3 and live == [live[0]] * 3
        if ok:
            before, again, first, last = parts
            initial, repeated = (replay_fields(p['breakpoint']) for p in (before, again))
            joined = {**last, 'values': first['values'] + last['values']}
            ok = (before['status'] == again['status'] == 'breakpoint' and
                  not before['values'] and not again['values'] and before['work'] == again['work'] and
                  initial['operation'] == 'ResourceCommit' and
                  initial['coverage'] == 'OwnedPrimeSnapshotCommits' and
                  initial['occurrence'] == repeated['occurrence'] != 0 and
                  repeated['inspections'] == initial['inspections'] + 1 and
                  len(first['values']) == len(last['values']) == 1 and
                  last['closed'] == 'True' and last['work'] == reference[-1]['work'] and
                  world_observations(joined) == expected)
            if recorded:
                policies = history_reports(name, 'HistoryPolicy')
                ok = ok and len(policies) == 1 and policies[0]['status'] == 'Unrecorded'
        checks.append(dict(case=name, passed=ok))

    name = 'snapshot-breakpoint-equal-occurrences-remain-distinct'
    duplicates = snapshot_rows.replace('(Token B)', '(Token A)')
    parts = run(name, imported + duplicates +
                f'!(bind! &before (select 100 {snapshot_control(snapshot_armed)} &jobs))\n' +
                capture + f'!(select 100 {snapshot_resumed} &resume)', definitions='')
    observations = world_observations(parts[-1]) if parts else []
    checks.append(dict(case=name, passed=len(parts) == 2 and
        parts[0]['status'] == 'breakpoint' and len(observations) == 2 and
        observations[0][0] == observations[1][0] and observations[0][1] != observations[1][1]))

    for variant, change in (
            ('append', '!(add-atom &jobs Phantom)\n'),
            ('replace-equal', f'!(remove-atom &jobs {snapshot_exec})\n!(add-atom &jobs {snapshot_exec})\n')):
        name = 'snapshot-breakpoint-stale-' + variant
        parts = run(name, imported + snapshot_rows +
                    f'!(bind! &before (select 100 {snapshot_control(snapshot_armed)} &jobs))\n' +
                    capture + change + f'!(select 100 {snapshot_resumed} &resume)\n' + snapshot_live,
                    definitions='')
        trees = list(walk(parse((d / (name + '.stdout')).read_text())))
        live = [node[1] for node in trees if node[0] == 'LiveStore']
        checks.append(dict(case=name, passed=len(parts) == 2 and
            parts[0]['status'] == 'breakpoint' and parts[-1]['status'] == 'invalidated' and
            not parts[-1]['values'] and len(live) == 1 and
            ['Token', 'A'] in live[0] and ['Token', 'B'] in live[0] and
            not any(isinstance(row, list) and row[:1] == ['Taken'] for row in live[0])))

    for variant, option, budget in (
            ('incomplete', '(breakpoint (exec Take $inputs $outputs) 0)', 10000),
            ('zero-budget', snapshot_armed, 0),
            ('miss', '(breakpoint (exec Never $inputs $outputs))', 10000)):
        name = 'snapshot-breakpoint-' + variant
        parts = run(name, imported + snapshot_rows +
                    f'!(select 100 {snapshot_control(option, budget)} &jobs)\n' + snapshot_live,
                    definitions='')
        ok = len(parts) == 1
        if ok and variant == 'miss':
            ok = world_observations(parts[0]) == expected and parts[0]['work'] == reference[-1]['work']
        elif ok:
            ok = not parts[0]['values']
            if variant == 'incomplete':
                ok = ok and parts[0]['status'] == 'inspection-incomplete'
            else:
                ok = ok and parts[0]['work'] == replay_fields(parts[0]['breakpoint'])['inspections'] == 0
        checks.append(dict(case=name, passed=ok))

    name = 'snapshot-breakpoint-pure-admission-cannot-resume'
    query = (imported + snapshot_rows +
             f'!(bind! &before (select 100 {snapshot_control(snapshot_armed)} &jobs))\n' + capture +
             f'(= (snapshot-verdict $weight) (chain (select 100 {snapshot_resumed} &resume) $ignored True))\n'
             '(=w (snapshot-guarded) GuardedBody 2 (admit snapshot-verdict))\n'
             '!(weight:read (algebra 0 1 + *) (snapshot-guarded))\n' +
             f'!(select 100 {snapshot_resumed} &resume)\n')
    parts = run(name, query, definitions='')
    checks.append(dict(case=name, passed=len(parts) == 3 and
        parts[0]['status'] == 'breakpoint' and parts[1]['status'] == 'fault' and
        not parts[1]['values'] and world_observations(parts[-1]) == expected))
    if recording:
        name = 'snapshot-breakpoint-truncated-history'
        observer = '(history (selections 0) (nodes 0) (bytes 0) (owners 0))'
        parts = run(name, imported + snapshot_rows +
                    f'!(bind! &before (select 100 {snapshot_control(snapshot_armed, observer=observer)} &jobs))\n' +
                    capture + f'!(select 100 {snapshot_resumed} &resume)', definitions='')
        checks.append(dict(case=name, passed=len(parts) == 2 and
            parts[0]['status'] == 'breakpoint' and fields(parts[0]['history'])['status'] == 'Incomplete' and
            world_observations(parts[-1]) == expected))


    concurrent_worlds = ('!(bind! &jobs (new-space))\n!(add-atom &jobs Token)\n'
        '!(add-atom &jobs (exec Left (I (read Token)) (O (+ Left))))\n'
        '!(add-atom &jobs (exec Right (I (read Token)) (O (+ Right))))\n')
    evidence = '(receipts True) (costs native-events-v1)'
    for width in (2, 3):
        for profile, initial in (('Transaction', rows), ('MM2', raw_rows),
                                  ('(Transaction worlds)', concurrent_worlds)):
            name = f'resource-parallel-breakpoint-w{width}-{profile}'
            plain = control('', observer=evidence, profile=profile, width=width)
            baseline = run(name + '-reference', imported + initial +
                           f'!(select 100 {plain} &jobs)\n' + snapshot_live, definitions='')
            for recorded in ((False, True) if recording else (False,)):
                label = name + f'-recorded-{int(recorded)}'
                observed = evidence + (' ' + history if recorded else '')
                armed = control('(breakpoint (exec $key $inputs $outputs))',
                                observer=observed, profile=profile, width=width)
                resumed = control('(breakpoint False)', observer=evidence, profile=profile, width=width)
                query = (imported + initial + f'!(bind! &before (select 100 {armed} &jobs))\n' +
                         capture + snapshot_live + f'!(select 100 {plain} &resume)\n' +
                         ('!(history:forget &before 131072)\n' if recorded else '') +
                         f'!(select 100 {resumed} &resume)\n' + snapshot_live)
                parts = run(label, query, definitions='')
                trees = list(walk(parse((d / (label + '.stdout')).read_text())))
                live = [node[1] for node in trees if node[0] == 'LiveStore']
                ok = len(parts) == 3 and len(live) == 2
                if ok:
                    before, again, completed = parts
                    first, second = (replay_fields(p['breakpoint']) for p in (before, again))
                    expected_operation = 'ResourceActivation' if profile == 'MM2' else 'ResourceCommit'
                    ok = (before['status'] == again['status'] == 'breakpoint' and
                          not before['values'] and not again['values'] and before['work'] == again['work'] and
                          first['operation'] == expected_operation and
                          first['occurrence'] == second['occurrence'] != 0 and
                          second['inspections'] == first['inspections'] + 1 and
                          completed['closed'] == 'True')
                    if profile == '(Transaction worlds)':
                        # Several private worlds supply independent matcher
                        # packets. A lone live provider stays on the coordinator.
                        ok = (ok and bool(completed.get('parallel-waves')) and
                              live[0] == live[1] and
                              world_observations(completed) == world_observations(baseline[-1]))
                    else:
                        ok = (ok and completed['values'] == baseline[-1]['values'] and
                              bool(completed.get('parallel-waves')) == bool(baseline[-1].get('parallel-waves')))
                    ok = ok and completed['work'] == baseline[-1]['work'] + (1 if profile == 'MM2' else 0)
                    if recorded:
                        policies = history_reports(label, 'HistoryPolicy')
                        ok = ok and len(policies) == 1 and policies[0]['status'] == 'Unrecorded'
                checks.append(dict(case=label, passed=ok))


def interaction_history_controls(profile='Transaction'):
    page = ('(InteractionChoices (choices $choices) (status $status) '
            '(pending $pending) (quanta $quanta) (residual $residual))')
    held = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t) (agenda $a))'
    raw = profile == 'MM2'
    inputs = ',' if raw else 'I'
    option = 'MM2 ' if raw else ''
    tag = 'interaction-history-mm2' if raw else 'interaction-history'
    prefix = ('!(bind! &jobs (new-space))\n'
              f'!(add-atom &jobs (exec A ({inputs}) (O (+ A))))\n'
              f'!(add-atom &jobs (exec B ({inputs}) (O (+ B))))\n'
              f'!(bind! &menu (interaction:choices &jobs {option}1000))\n'
              f'!(bind! &discovery (let {page} &menu $residual))\n'
              '!(bind! &selected (interaction:perform &discovery 2 0))\n'
              f'!(bind! &held (let {held} &selected $r))\n'
              '!(interaction:dispose &discovery)\n')
    def resource(limit, observer=''):
        return (f'!(select {limit} (search-policy (control FIFO (resources {profile}) '
                f'(workers 1) (budget 1000) {observer})) &held)\n')
    ordinary = run(tag+'-ordinary', prefix + resource(1) + resource(2), definitions='')
    recorded = run(tag+'-optional', prefix + resource(1, history) +
                   resource(2, '(history False)'), definitions='')
    checks.append(dict(case=tag+'-erases-to-normal-owned-execution', passed=
        len(ordinary) == len(recorded) == 2 and
        all(same_observation(left, right) for left, right in zip(ordinary, recorded)) and
        all('history' not in report for report in ordinary)))
    before = fields(recorded[0]['history']) if recorded else {}
    stopped = fields(recorded[1]['history']) if len(recorded) == 2 else {}
    checks.append(dict(case=tag+'-stop-preserves-the-selected-prefix', passed=
        bool(before.get('selections')) and before.get('recording') == 'True' and
        stopped.get('recording') == 'False' and stopped.get('selections') == before.get('selections')))
    name = tag+'-forget-and-continue'
    forgotten = run(name, prefix + resource(1, history) + '!(history:forget &held 100000)\n' +
                    resource(2) + '!(collapse (get-atoms &jobs))', definitions='')
    policies = [node for node in walk(parse((d / (name + '.stdout')).read_text()))
                if node[0] == 'HistoryPolicy']
    checks.append(dict(case=name, passed=len(forgotten) == 2 and bool(policies) and
        fields(policies[0]).get('status') == 'Unrecorded' and
        same_observation(forgotten[-1], ordinary[-1]) and
        'history' not in forgotten[-1]))


def debug_controls():
    client = w / 'examples/prime/native_control/controlled_publication.metta'
    name = 'debug-controlled-publication-unrecorded'
    run(name, '', source_path=client)
    checks.append(dict(case=name, passed=(d / (name + '.stdout')).read_text() ==
                       client.with_suffix('.expected').read_text() and
                       not (d / (name + '.stderr')).read_text()))
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
            ok = len(reports) == 6 and effects == ['ACCEPTED-BODY']
            if ok:
                views = [{item[0]: item[1:] for item in report[1:]
                          if isinstance(item, list) and item} for report in reports]
                ok = all(views[i]['closed'] == ['True'] and
                         views[i]['status'] == ['closed'] for i in (0, 1, 4, 5)) and \
                     all(views[i]['closed'] == ['False'] and
                         views[i]['status'] == ['suspended'] for i in (2, 3)) and \
                     views[3]['work'] == [0] and views[3]['activity'] == ['Unexamined'] and \
                     views[4]['values'] == [['quote', []]] and \
                     views[5]['values'] == [['quote', []]] and \
                     all(views[i]['residual'] != [[]] for i in (2, 3))
                ok = ok and not any(node[0].startswith('history:') for node in nodes)
                if variant == 'omitted':
                    ok = ok and roles == [[], [], [], [], [], []] and all(
                        views[i]['recording'][0] == 'Incomplete' for i in (0, 1, 2))
                else:
                    ok = ok and roles == [['Accepted'],
                        ['Rejected'], ['Pending'], [], [], ['Accepted']]
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
            '(source-correspondence Unchecked) (recording False) (end-work 0) '
            '(omitted-selections 1) (omitted-nodes 0) '
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


    # Graph lookup uses the same unique-field reader, retaining the original node
    # as quoted data. A missing node in a partial graph remains partial evidence.
    for language in ('prime', 'he', 'petta'):
        name = 'debug-node-fields-' + language
        graph = '(NeedGraph 9 2 Complete ((NeedNode 1 0 0 Join) (NeedNode 2 1 1 (quote (println! "NODE-MUST-STAY-DATA")))))'
        duplicate = '(NeedGraph 9 2 Complete ((NeedNode 2 1 1 Join) (NeedNode 2 1 1 Join)))'
        run(name, f'!(import! &self {debug_library}) '
            f'!(history:node {graph} 2) !(history:node {graph} 3) '
            '!(history:node (NeedGraph 9 2 Incomplete ((NeedNode 1 0 0 Join))) 2) '
            f'!(history:node {duplicate} 2)', definitions='', language=language)
        stdout = (d / (name + '.stdout')).read_text()
        nodes = [node for node in walk(parse(stdout)) if node[0] == 'GraphNode']
        effects = [line for line in stdout.splitlines()
                   if line.strip('"') == 'NODE-MUST-STAY-DATA']
        ok = len(nodes) == 4 and not effects
        if ok:
            found = nodes[0][4]
            ok = found[:3] == ['HistoryField', 'Found', 2] and \
                found[3][:1] == ['quote'] and found[3][1][:4] == ['NeedNode', 2, 1, 1] and \
                nodes[1][3:] == ['Complete', ['HistoryField', 'Missing', 3, []]] and \
                nodes[2][3:] == ['Incomplete', ['HistoryField', 'Missing', 2, []]] and \
                nodes[3][4] == ['HistoryField', 'Duplicate', 2, []]
        checks.append(dict(case=name, passed=ok, reports=len(nodes), effects=effects))

    # This authored client queries actual retained graphs, then resumes the same
    # computation with recording stopped. Inspection must not replay production.
    dependency_client = r'''
    (: DependencyClient (-> Atom Atom Atom Atom))
    (= (debug:dependency-body)
       (let $_ (println! "DEPENDENCY-PRODUCER-ONCE") (history-pair (history-coin))))
    '''
    for variant, expression in (('pure', body), ('effect', '(debug:dependency-body)')):
        for language in ('prime', 'he', 'petta'):
            name = f'debug-retained-dependencies-{variant}-{language}'
            prefix_policy = policy(1, 1000).replace('(demand all)', '(demand absolute)')
            query = f'!(import! &self {debug_library}) ' + dependency_client + \
                f'!(let $observation (select 1 {prefix_policy} {expression}) ' \
                f'(let {pattern} $observation ' \
                '(let $view (history:dependencies $observation) ' \
                '(let (HistoryDependencies $explained $rows) $view ' \
                f'(let $continued (select 0 {policy(1, 1000, stop=True)} $r) ' \
                '(DependencyClient $view $h $continued))))))'
            run(name, query, language=language)
            stdout = (d / (name + '.stdout')).read_text()
            packet = tagged(parse(stdout), 'DependencyClient')
            effects = [line for line in stdout.splitlines()
                       if line.strip('"') == 'DEPENDENCY-PRODUCER-ONCE']
            ok = packet is not None and len(effects) == (1 if variant == 'effect' else 0)
            graphs = []
            if ok:
                view, original_history, continued = packet[1:]
                explanation = tagged(view, 'Explained')
                explanation_fields = {row[0]: row[1:] for row in explanation[1:]
                          if isinstance(row, list) and row}
                before = explanation_fields['values'][0][1]
                after = list(observations(continued))
                graph_rows = view[2]
                originals = fields(original_history)['selections']
                graphs = [row for row in walk(graph_rows) if row[0] == 'NeedGraph']
                ok = len(before) == 1 and explanation_fields['closed'] == ['False'] and len(after) == 1 and \
                    bag(before + after[0]['values']) == bag([['Pair', 3, 3], ['Pair', 3, 3], ['Pair', 6, 6]]) and \
                    after[0]['closed'] == 'True' and \
                    len(graph_rows) == len(originals) and all(
                        row[:6] == ['SelectionGraphs', *original[1:6]] and
                        row[6] == ['HistoryField', 'Found', 'preparation', ['quote', original[6][1]]] and
                        row[7] == ['HistoryField', 'Found', 'operation', ['quote', original[7][1]]]
                        for row, original in zip(graph_rows, originals))
                if language == 'prime' and variant == 'pure':
                    ok = ok and len(graphs) > 0 and all(
                        any(node[1] == graph[2] for node in graph[4]) for graph in graphs) and explanation_fields['recording'][0] == 'Complete'
                else:
                    # Guest interiors and the effectful producer explicitly lack
                    # complete native graph capture in this recording profile.
                    ok = ok and explanation_fields['recording'][0] == 'Incomplete'
            checks.append(dict(case=name, passed=ok, dependency_graphs=len(graphs), effects=effects))

    # Asking about an unrecorded computation reports the absent history field.
    for language in ('prime', 'he', 'petta'):
        name = 'debug-unrecorded-dependencies-' + language
        run(name, f'!(import! &self {debug_library}) '
            f'!(let $observed (select 0 {policy(1, 1000, h=False)} (history-pair (history-coin))) '
            '(history:dependencies $observed))', language=language)
        packet = tagged(parse((d / (name + '.stdout')).read_text()), 'HistoryDependencies')
        checks.append(dict(case=name, passed=packet ==
            ['HistoryDependencies', ['HistoryField', 'Missing', 'history', []], []]))

    # Bounded advancement returns from a continuing effectful agent and resumes
    # the same owner. Neither a diary nor a completed answer is required.
    client = w / 'examples/prime/native_control/responsive_monitor.metta'
    expected = client.with_suffix('.expected').read_text()
    name = 'debug-responsive-monitor-unrecorded'
    run(name, '', source_path=client)
    actual = (d / (name + '.stdout')).read_text()
    checks.append(dict(case=name, passed=actual == expected and
                       not (d / (name + '.stderr')).read_text()))
    # A quantum boundary may move within a loop iteration. Equal total
    # grants must retain the exact effects, work and suspended continuation.
    monitor_definitions = client.read_text().split('!(let (Observation', 1)[0]
    for allowance in (40, 60):
        pair = []
        for split in (False, True):
            query = f'(select 0 {policy(1, allowance, False)} (monitor:loop Heartbeat 0))'
            if split:
                query = f'(select 0 {policy(1, 20, False)} $r{allowance // 20 - 1})'
                for index in reversed(range(allowance // 20 - 1)):
                    frame = plain.replace('$r', f'$r{index + 1}')
                    source = '(monitor:loop Heartbeat 0)' if index == 0 else f'$r{index}'
                    query = f'(let {frame} (select 0 {policy(1, 20, False)} {source}) {query})'
            stem = f'debug-monitor-{allowance}-' + ('split' if split else 'whole')
            run(stem, '!' + query, definitions=monitor_definitions)
            output = (d / (stem + '.stdout')).read_text()
            observed = list(observations(parse(output)))
            effects = [line for line in output.splitlines()
                       if line.startswith(('(Heartbeat ', '(Service '))]
            pair.append((effects, observed[-1] if observed else None))
        checks.append(dict(case=f'debug-monitor-{allowance}-split-agreement',
            passed=pair[0][1] is not None and pair[1][1] is not None and
                pair[0][0] == pair[1][0] and same_observation(pair[0][1], pair[1][1]) and
                pair[1][1]['work'] == allowance and pair[1][1]['closed'] == 'False' and
                pair[1][1]['status'] == 'suspended'))
    name = 'debug-responsive-monitor-zero-budget'
    monitor = client.read_text().split('; A request/reply server', 1)[0]
    run(name, monitor.replace('(budget 20)', '(budget 0)'), definitions='')
    actual = (d / (name + '.stdout')).read_text()
    checks.append(dict(case=name, passed=actual ==
        '(ControlReturned 0 False suspended)\n'
        '[(MonitorPaused () () 0 0 False False suspended suspended)]\n' and
        not (d / (name + '.stderr')).read_text()))


if args.population == 'resource-breakpoint':
    resource_breakpoint_controls()
    finish()

if args.population == 'breakpoint':
    breakpoint_controls(recording=False)
    resource_breakpoint_controls(recording=False)
    finish()

if args.population == 'debug':
    breakpoint_controls()
    resource_breakpoint_controls()
    debug_controls()
    finish()

if args.population in ('all', 'resampling'):
    resampling_controls(recording=args.population == 'all')
if args.population == 'resampling':
    finish()


if args.population in ('all', 'origins'):
    origin_controls(recording=args.population == 'all')
if args.population == 'origins':
    finish()


if args.population in ('all', 'diagnostics'):
    diagnostic_controls(recording=args.population == 'all')
if args.population == 'diagnostics':
    finish()
if args.population == 'all':
    recording_policy_controls()
if args.population != 'forget':
    replay_controls()
if args.population != 'replay':
    forget_controls()
if args.population != 'all':
    finish()

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

breakpoint_controls()
resource_breakpoint_controls()
interaction_history_controls()
interaction_history_controls('MM2')
debug_controls()

finish()
