#!/usr/bin/env python3
"""Program-level native agenda and resource-contract controls."""
from __future__ import annotations
import argparse
from hashlib import sha256
import json
from pathlib import Path
import random
from test_prime_native_control import Gate, ROOT, bag


def policy(kind='Transaction', budget=100000):
    return f'(search-policy (control FIFO (resources {kind}) (budget {budget})))'


PATTERN = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t) (agenda $a))'


def source(rows):
    return '!(bind! &jobs (new-space))\n' + '\n'.join(f'!(add-atom &jobs {row})' for row in rows) + '\n'


def inspect(gate, name, rows, kind='Transaction', suffix=''):
    trees, results=gate.run(name,source(rows)+f'!(select 100 {policy(kind)} &jobs)\n'+suffix+'\n!(collapse (get-atoms &jobs))')
    assert results, (name,trees)
    return trees[-1][0],results[-1]


def fixed(gate):
    values,result=inspect(gate,'persistent read then competing consumes',[
        'Token','(exec (0 read) (I (read Token) (read Token)) (O (+ Read)))',
        '(exec (1 take) (I (take Token)) (O (+ Taken)))',
        '(exec (2 blocked) (I (take Token)) (O (+ Wrong)))'])
    assert 'Read' in values and 'Taken' in values and 'Wrong' not in values and 'Token' not in values,(values,result)
    assert len(result['values'])==2 and result['closed']=='False' and result['status']=='suspended',result
    gate.passed_case('persistent read sharing and exclusive consuming occurrences')
    values,result=inspect(gate,'raw MM2 join and selected exec read copy',[
        '(p A)','(p B)','(q B)',
        '(exec (0 join) (, (p $x) (q $x)) (O (+ (hit $x)) (- (q $x)) (+ (exec (1 self) (, (exec $k $p $o)) (O (+ (seen $k)))))))'], 'MM2')
    assert ['hit','B'] in values and ['seen',[1,'self']] in values and ['q','B'] not in values,(values,result)
    assert result['closed']=='True' and len(result['values'])==2,result
    gate.passed_case('raw MM2 joins, read-copy reinsertion and created execs use native residual work')


def split(gate,count):
    rows=['(fact A)','(fact B)','Token','Token',
          '(exec (0 join) (I (take Token) (take Token) (all (fact $x) $rows) (absent Stop)) (O (+ (result $rows))))']
    randomizer=random.Random(422)
    for budget in [0,1,3,5,11,19,29,40,61]+[randomizer.randrange(1,75) for _ in range(count)]:
        trees,results=gate.run(f'resource split {budget}',source(rows)+
            f'!(let {PATTERN} (select 1 {policy(budget=budget)} &jobs) '
            f'(Split $v (select 1 {policy()} $r)))\n!(collapse (get-atoms &jobs))')
        assert results and len(trees[-2][0][1])+len(results[-1]['values'])==1,(budget,trees,results)
        values=trees[-1][0]
        assert ['result',[['fact','A'],['fact','B']]] in values and 'Token' not in values,(budget,trees)
        gate.passed_case(f'resource capture/match/commit residual survives {budget}-quantum pause')


def priorities(gate):
    rows=['(exec (Job 8) (I) (O (+ Eight)))','(exec (Job 2) (I) (O (+ Two)))']
    for strategy,expression,expected in [('WO','$n','Two'),('PO','$n','Two'),('WO','0','Eight')]:
        trees,results=gate.run(f'{strategy} admitted exec advice {expression}',source(rows)+
            f'!(select 1 (search-policy (control {strategy} (resources Transaction) '
            f'(key (exec (Job $n) $inputs $outputs) {expression}) (budget 10000))) &jobs)\n'
            '!(collapse (get-atoms &jobs))')
        assert len(results[-1]['values'])==1 and expected in trees[-1][0],(trees,results)
        assert dict(results[-1]['values'][0][2:])['order']=='program-advice',results
        gate.passed_case(f'{strategy} uses retained physical exec bindings; advice {expression}')
    trees,results=gate.run('divergent advisory agenda score',
        '(= (spin) (spin)) '+source(rows)+
        '!(select 2 (search-policy (control WO (resources Transaction) '
        '(key $exec (spin)) (budget 10000))) &jobs)\n!(collapse (get-atoms &jobs))')
    assert len(results[-1]['values'])==2 and 'Eight' in trees[-1][0] and 'Two' in trees[-1][0],(trees,results)
    gate.passed_case('divergent pure native advice cannot block resource body liveness')
    rows=['(exec 0 (, (exec 0 $in $out)) (O (+ (exec 0 $in $out))))',
          '(exec 9 (,) (O (+ Win)))']
    trees,results=gate.run('raw regenerating-byte starvation prefix',source(rows)+
        f'!(select 12 {policy("MM2",3000)} &jobs)\n!(collapse (get-atoms &jobs))')
    assert 'Win' not in trees[-1][0] and len(results[-1]['values'])==12,(trees,results)
    gate.passed_case('raw byte-priority regeneration can starve a larger key')
    trees,results=gate.run('protected program advice serves waiting exec',source(rows)+
        '!(select 12 (search-policy (control WO (resources MM2) '
        '(key (exec $n $inputs $outputs) $n) (age 3) (budget 3000))) &jobs)\n'
        '!(collapse (get-atoms &jobs))')
    assert 'Win' in trees[-1][0] and len(results[-1]['values'])==12,(trees,results)
    gate.passed_case('the existing recurrent-oldest discipline protects physical exec service')


def current_scope(gate):
    rows=['(fact A)','(exec (0 all) (I (all (fact $v) $rows)) (O (+ (result $rows))))']
    for budget in [1,7,12,18]:
        trees,results=gate.run(f'current all scope after {budget}',source(rows)+
            f'!(let {PATTERN} (select 1 {policy(budget=budget)} &jobs) '
            f'(let $_ (add-atom &jobs (fact B)) (select 1 {policy()} $r)))\n'
            '!(collapse (get-atoms &jobs))')
        assert ['result',[['fact','A'],['fact','B']]] in trees[-1][0],(budget,trees)
        gate.passed_case(f'all-current authority revalidates insertion after {budget} quanta')
    trees,results=gate.run('waiting native receive resumes on appended token',source([
        '(exec (0 wait) (I (take Wake)) (O (+ Awake)))'])+
        f'!(let {PATTERN} (select 1 {policy()} &jobs) '
        f'(let $_ (add-atom &jobs Wake) (select 1 {policy()} $r)))\n!(collapse (get-atoms &jobs))')
    assert 'Awake' in trees[-1][0] and 'Wake' not in trees[-1][0] and len(results[-1]['values'])==1,(trees,results)
    gate.passed_case('a retained disabled receive becomes ready after later input')
    trees,results=gate.run('current resource storage',source(rows)+
        f'!(let {PATTERN} (select 1 {policy(budget=7)} &jobs) '
        f'(Stored (cost:storage $r) (select 1 {policy()} $r)))')
    record=trees[-1][0][1]
    assert record[:2]==['NativeStorage','native-owned-storage-v1'],record
    assert dict(record[2:])['ownership']=='Complete',record
    assert len(results[-1]['values'])==1,(trees,results)
    gate.passed_case('suspended source/matching cursors participate in owned storage census')


def generated(gate,count):
    rng=random.Random(791)
    for i in range(count):
        ps=[rng.randrange(8) for _ in range(9)]
        qs=[rng.randrange(8) for _ in range(7)]
        rows=[f'(p {x})' for x in ps]+[f'(q {x})' for x in qs]+[
            '(exec (0 join) (, (p $x) (q $x)) (O (+ (hit $x)) (- (q $x))))']
        values,result=inspect(gate,f'MM2 ground set join {i}',rows,'MM2')
        expected=[['p',x] for x in set(ps)]+[['q',x] for x in set(qs)-set(ps)]+[['hit',x] for x in set(ps)&set(qs)]
        assert bag(values)==bag(expected) and result['closed']=='True',(i,values,expected,result)
        gate.passed_case(f'independent ground MM2 set-join reference {i}')
        tokens=rng.randrange(1,10);execs=rng.randrange(1,7)
        rows=['Token']*tokens+[f'(exec ({n} take) (I (take Token) (take Token)) (O (+ (done {n}))))' for n in range(execs)]
        values,result=inspect(gate,f'collective bag demand {i}',rows)
        commits=min(tokens//2,execs)
        assert len(result['values'])==commits and values.count('Token')==tokens-2*commits,(values,result)
        assert result['closed']==('True' if commits==execs else 'False'),result
        gate.passed_case(f'exclusive duplicate-token demand reference {i}')


def mm2_correspondence(gate, count):
    """Compare the declared native MM2 fragment, not the separate GSLT runner.

    Positive rule conclusions are explicitly rendered as O/+ sinks. This is
    not a literal or reflection-preserving translation of comma output.
    """
    import sys
    sys.path.insert(0, str(ROOT / 'tools'))
    try:
        # The ordinary result parser skips answer-separating commas. MM2's
        # comma constructor is data here, so reuse the existing MM2 reader.
        from mm2_to_metta import parse as parse_mm2
    finally:
        sys.path.pop(0)

    def term(text):
        forms = parse_mm2(text)
        assert len(forms) == 1, forms
        return forms[0]

    def exact_output():
        forms = []
        for line in gate.receipts[-1]['stdout'].splitlines():
            if line.startswith('['):
                assert line.endswith(']'), line
                entries = parse_mm2(line[1:-1])
                # Every form authored in this panel returns one result;
                # there is no ambiguous top-level answer separator.
                assert len(entries) == 1, (line, entries)
                forms.append(entries[0])
        return forms

    def explicit(key, premises, conclusions):
        return f'(exec {key} (, {" ".join(premises)}) ' + \
            f'(O {" ".join(f"(+ {item})" for item in conclusions)}))'

    def run(name, rows, expected, environments, wanted=100):
        _, results = gate.run(name, source(rows) +
            f'!(select {wanted} {policy("MM2", 10000)} &jobs)\n'
            '!(collapse (get-atoms &jobs))')
        output = exact_output()
        result = results[-1]
        assert result['closed'] == 'True' and result['status'] == 'closed', result
        assert bag(output[-1]) == bag([term(item) for item in expected]), (name, output[-1], expected)
        records = result['values']
        assert all(record[0] == 'Committed' for record in records), records
        fields = [dict(record[2:]) for record in records]
        assert [record['environments'] for record in fields] == environments, (name, fields)
        assert all(record['store'] == 'set' and record['order'] == 'compact-byte'
                   for record in fields), fields
        return result, output, fields

    # Same positive two-derivation fixture as the independent GCL model.
    # The source control atom has an exec head and cannot match these premises.
    context = ['(selected (query A C))', '(edge A B)', '(edge B C)',
               '(edge A D)', '(edge D C)']
    premises = ['(selected (query $from $to))', '(edge $from $middle)', '(edge $middle $to)']
    directive = explicit('inference', premises, ['(reachable $from $to)'])
    rows = context + [directive]
    baseline, baseline_output, fields = run('MM2 GCL two derivations one support fact',
        rows, context + ['(reachable A C)'], [2])
    dependencies = quoted(fields[0]['dependencies'])
    assert [[part[2] for part in row] for row in dependencies] == [[0, 1, 2], [0, 3, 4]], dependencies
    assert all(len(row) == 3 and all(part[0] == 'read' for part in row) for row in dependencies)
    assert quoted(baseline_output[-2][1][0][1]) == term(directive), baseline_output
    gate.passed_case('native O/+ GCL generation retains two physical derivations and one set-support value')

    for budget in (0, 1, 7, 19, 53, 101):
        trees, results = gate.run(f'MM2 GCL split {budget}', source(rows) +
            f'!(let {PATTERN} (select 100 {policy("MM2", budget)} &jobs) '
            f'(Split $v (select 100 {policy("MM2", 10000-budget)} $r)))\n'
            '!(collapse (get-atoms &jobs))')
        output = exact_output()
        actual = {**results[-1], 'values': trees[-2][0][1] + results[-1]['values']}
        for field in ('values', 'work', 'satisfied', 'closed', 'status'):
            assert actual[field] == baseline[field], (budget, field, actual[field], baseline[field])
        # Keep literal quoted directive and dependency syntax, including commas.
        combined = output[-2][1] + output[-2][2][1]
        assert combined == baseline_output[-2][1], (budget, combined, baseline_output)
        assert bag(output[-1]) == bag(baseline_output[-1]), (budget, output)
        gate.passed_case(f'positive native GCL split {budget} retains rows, exact work and completed support')

    rng = random.Random(14073)
    for case in range(count):
        edges = list(dict.fromkeys((rng.randrange(5), rng.randrange(5)) for _ in range(12)))
        start, end = rng.randrange(5), rng.randrange(5)
        matches = [(i, j) for i, (left, mid) in enumerate(edges)
                   for j, (mid2, right) in enumerate(edges)
                   if left == start and mid == mid2 and right == end]
        facts = [f'(selected (query N{start} N{end}))'] + [f'(edge N{x} N{y})' for x, y in edges]
        expected = facts + ([f'(reachable N{start} N{end})'] if matches else [])
        _, _, records = run(f'independent positive GCL graph join {case}',
            facts + [directive], expected, [len(matches)])
        actual = quoted(records[0]['dependencies'])
        assert [[part[2] for part in row] for row in actual] == [
            [0, i + 1, j + 1] for i, j in matches], (case, actual, matches)
        gate.passed_case(f'positive GCL graph {case} matches independent shared-variable relation and physical rows')

    run('MM2 duplicate source facts use set support', context + [context[1], directive],
        context + ['(reachable A C)'], [2])
    gate.passed_case('native MM2 duplicate source rows do not become duplicate set members or extra derivations')

    # Two native selected activations compose; no test-side evaluator advances
    # either directive or inserts the second activation.
    second = explicit('second', ['(reachable A $to)', '(edge $to E)'], ['(finished $to)'])
    first = explicit('first', premises, ['(reachable $from $to)', second])
    run('MM2 positive native generated activation composition',
        context + ['(edge C E)', first], context + ['(edge C E)', '(reachable A C)', '(finished C)'], [2, 1])
    gate.passed_case('positive native generations compose through one created exec and retain support union')

    self_reader = explicit('self', ['(exec self $input $output)'], ['ReadSelf'])
    _, _, fields = run('MM2 selected exec reads private snapshot self', [self_reader], ['ReadSelf'], [1])
    assert quoted(fields[0]['dependencies'])[0][0][2] == 0, fields
    gate.passed_case('native selected exec is absent live and retained exactly once in its read snapshot')

    reflected = explicit('inference', ['(exec inference $input $output)'], ['(seen-output $output)'])
    _, output, _ = run('MM2 reflected O/+ output is literal data', [reflected],
        ['(seen-output (O (+ (seen-output $output))))'], [1])
    assert output[-1] != [term('(seen-output (, (seen-output $output)))')], output
    gate.passed_case('reflected native output retains O/+ spelling; positive interpretation is not unrestricted reflection equivalence')

    destructive = '(exec a (, (fact)) (O (- (fact)) (+ (gone))))'
    later = explicit('b', ['(fact)'], ['(late)'])
    run('MM2 destructive selected transaction changes later match',
        [destructive, later, '(fact)'], ['(gone)'], [1, 0])
    gate.passed_case('native deletion changes the next match and is not treated as monotone positive inference')

    tagged = explicit('derive', ['(route $identity A C)'], ['(occurrence $identity (reachable A C))'])
    tagged_context = ['(route Left A C)', '(route Right A C)']
    run('MM2 tagged occurrence support', tagged_context + [tagged], tagged_context + [
        '(occurrence Left (reachable A C))', '(occurrence Right (reachable A C))'], [2])
    untagged = explicit('derive', ['(route $identity A C)'], ['(reachable A C)'])
    run('MM2 untagged occurrence erasure loses multiplicity', tagged_context + [untagged],
        tagged_context + ['(reachable A C)'], [2])
    gate.passed_case('explicit occurrence tags retain equal conclusions; erasing tags loses multiplicity in set storage')

    # Unknown syntax is retained as owned work; it is not an accepted
    # leave-inert or completed-consume source transition.
    unsupported = '(exec unsupported (, (seed A)) (, (rejected A)))'
    for profile in ('MM2', 'Transaction'):
        gate.run(f'{profile} unsupported comma output remains owned',
            source(['(seed A)', unsupported]) +
            f'!(bind! &one (select 100 {policy(profile, 10000)} &jobs))\n'
            f'!(bind! &two (let {PATTERN} &one (select 100 {policy(profile, 0)} $r)))\n'
            f'!(bind! &two2 (let {PATTERN} &two (select 100 {policy(profile, 0)} $r)))\n'
            f'!(bind! &two3 (let {PATTERN} &two2 (select 100 {policy(profile, 0)} $r)))\n'
            f'!(bind! &three (let {PATTERN} &two3 (select 100 {policy(profile, 10000)} $r)))\n'
            '!(let ' + PATTERN + ' &one (Stopped (quote $v){$v} $c $w $t (quote $a){$a}))\n'
            '!(let ' + PATTERN + ' &two (Stopped (quote $v){$v} $c $w $t (quote $a){$a}))\n'
            '!(let ' + PATTERN + ' &two2 (Stopped (quote $v){$v} $c $w $t (quote $a){$a}))\n'
            '!(let ' + PATTERN + ' &two3 (Stopped (quote $v){$v} $c $w $t (quote $a){$a}))\n'
            '!(let ' + PATTERN + ' &three (Stopped (quote $v){$v} $c $w $t (quote $a){$a}))\n'
            '!(collapse (get-atoms &jobs))')
        output = exact_output()
        stopped = output[-6:-1]
        assert all(item[0] == 'Stopped' and quoted(item[1]) == [] and
                   item[2] == 'False' and item[4] == 'suspended' for item in stopped), stopped
        assert stopped[:4] == [stopped[0]] * 4, stopped
        for item in stopped:
            agenda = dict(quoted(item[5])[1:])
            assert agenda['phase'] == 'Wait' and agenda['obligation'] == 'UninterpretedExec', agenda
            assert quoted(agenda['selected']) == term(unsupported) and agenda['commits'] == '0', agenda
        expected = ['(seed A)'] + ([unsupported] if profile == 'Transaction' else [])
        assert bag(output[-1]) == bag([term(item) for item in expected]), output
        gate.passed_case(f'{profile} comma-output refusal retains selected syntax, zero-grant packet and unchanged committed state')


def pending_resumption(gate):
    """Zero evaluator allowance observes retained waits without retrying them.

    Ready publication and source-currentness failures are different cases;
    this is not an identity claim for every public select invocation.
    """
    def snapshots(names):
        return ''.join(f'!(let {PATTERN} {name} '
                       '(Waiting (quote $v){$v} $s $c $w $t (quote $a){$a}))\n'
                       for name in names)

    def zero_chain(start='&wait'):
        code = ''
        names = [start]
        for i in range(3):
            name = f'&zero{i}'
            code += f'!(bind! {name} (let {PATTERN} {names[-1]} '
            code += f'(select 100 {policy(budget=0)} $r)))\n'
            names.append(name)
        return code, names

    wait = '(exec Wait (I (take Wake)) (O (+ Awake)))'
    zero, names = zero_chain()
    trees, results = gate.run('waiting receive zero grants then later input',
        source([wait]) + f'!(bind! &wait (select 100 {policy()} &jobs))\n' + zero +
        snapshots(names) +
        '!(add-atom &jobs Wake)\n' +
        f'!(bind! &changed (let {PATTERN} &zero2 (select 100 {policy(budget=0)} $r)))\n' +
        snapshots(['&changed']) +
        f'!(let {PATTERN} &changed (select 100 {policy()} $r))\n' +
        '!(collapse (get-atoms &jobs))')
    waiting = [row[0] for row in trees if row and isinstance(row[0], list) and row[0][:1] == ['Waiting']]
    assert len(waiting) == 5 and waiting == [waiting[0]] * 5, waiting
    fields = dict(quoted(waiting[0][-1])[1:])
    assert fields['obligation'] == 'WaitingForMatch' and fields['commits'] == 0, fields
    assert len(quoted(fields['waiting'])) == 1, fields
    assert results[-1]['closed'] == 'True' and len(results[-1]['values']) == 1, results
    assert trees[-1] == [['Awake']], trees[-1]
    gate.passed_case('three zero grants and appended input retain WaitingForMatch; positive grant consumes once')

    # A removed selected occurrence cannot authorize another physical row.
    # This cut is in source capture, before matching or commit begins.
    selected = '(exec Selected (I (read Token)) (O (+ Wrong)))'
    zero, names = zero_chain()
    trees, results = gate.run('stale selected occurrence zero grants retain refusal',
        source(['Token', selected]) +
        f'!(bind! &cut (select 100 {policy(budget=3)} &jobs))\n' +
        f'!(remove-atom &jobs {selected})\n' +
        f'!(bind! &wait (let {PATTERN} &cut (select 100 {policy()} $r)))\n' +
        zero + snapshots(names) +
        f'!(bind! &retry (let {PATTERN} &zero2 (select 100 {policy()} $r)))\n' +
        snapshots(['&retry']) + '!(collapse (get-atoms &jobs))')
    waiting = [row[0] for row in trees if row and isinstance(row[0], list) and row[0][:1] == ['Waiting']]
    assert len(waiting) == 5 and waiting[:4] == [waiting[0]] * 4, waiting
    for item in waiting:
        fields = dict(quoted(item[-1])[1:])
        assert fields['obligation'] == 'StaleSelectedOccurrence' and fields['commits'] == 0, fields
        assert item[1:4] == [['quote', []], 'False', 'False'], item
    assert trees[-1] == [['Token']], trees[-1]
    gate.passed_case('stale selected occurrence remains owned across zero grants and refuses a later retry')

    for cut in (0, 3, 7):
        kind = '(Transaction worlds)'
        _, results = gate.run(f'zero grant still validates world source {cut}', source([wait]) +
            f'!(let {PATTERN} (select 100 {policy(kind, cut)} &jobs) '
            f'(let $_ (add-atom &jobs Wake) (select 100 {policy(kind, 0)} $r)))')
        assert results[-1]['status'] == 'invalidated' and results[-1]['closed'] == 'False', results
        assert not results[-1]['values'], results
        gate.passed_case(f'zero grant checks captured world authority at cut {cut}')


def quoted(value):
    assert isinstance(value,list) and len(value)==2 and value[0]=='quote',value
    return value[1]


def worlds(result):
    found=[]
    for value in result['values']:
        assert value[0]=='SnapshotWorld' and value[1]>0,value
        fields=dict(value[2:])
        assert fields['state']=='Quiescent',value
        steps=[]
        trace=quoted(fields['trace'])
        while trace:
            assert trace[0]=='then' and len(trace)==3,trace
            step=trace[2]
            assert step[0]=='WorldStep',step
            steps.append(step)
            trace=trace[1]
        found.append((quoted(fields['atoms']),list(reversed(steps))))
    return found


def world_observations(result):
    return sorted((json.dumps(bag(atoms),sort_keys=True),
                   json.dumps([[step[1],dict(step[2:])['dependencies'],
                                dict(step[2:])['selected-occurrence']] for step in trace],sort_keys=True))
                  for atoms,trace in worlds(result))


def snapshot_worlds(gate,count):
    kind='(Transaction worlds)'
    cases=[
        ('competing consumes',['Token',
            '(exec Left (I (take Token)) (O (+ LeftWon)))',
            '(exec Right (I (take Token)) (O (+ RightWon)))'],2),
        ('equal physical tokens',['Token','Token',
            '(exec Take (I (take Token)) (O (+ Taken)))'],2),
        ('duplicate-consuming orders',['Token','Token',
            '(exec Left (I (take Token)) (O (+ LeftWon)))',
            '(exec Right (I (take Token)) (O (+ RightWon)))'],4),
        ('persistent read orders',['Token',
            '(exec Left (I (read Token)) (O (+ LeftRead)))',
            '(exec Right (I (read Token)) (O (+ RightRead)))'],2),
        ('binding alternatives',['(Token A)','(Token B)',
            '(exec Take (I (take (Token $x))) (O (+ (Taken $x))))'],2),
        ('complete read vs insertion',['(fact A)',
            '(exec Collect (I (all (fact $x) $rows)) (O (+ (Result $rows))))',
            '(exec Insert (I) (O (+ (fact B))))'],2),
        ('absence vs insertion',[
            '(exec Absent (I (absent Stop)) (O (+ Passed)))',
            '(exec Insert (I) (O (+ Stop)))'],2),
        ('created exec and retained bindings',['(Token A)','(Token B)',
            '(exec First (I (take (Token $x))) (O (+ (exec Next (I (read (Token $y))) (O (+ (Pair $x $y)))))))'],2),
    ]
    for name,rows,expected in cases:
        trees,results=gate.run('snapshot worlds '+name,source(rows)+
            f'!(collapse (get-atoms &jobs))\n!(select 100 {policy(kind)} &jobs)\n!(collapse (get-atoms &jobs))')
        result=results[-1];out=worlds(result)
        assert len(out)==expected and result['closed']=='True',(name,out,result)
        # Worlds never commit their speculative writes to the live source.
        assert bag(trees[-3][0])==bag(trees[-1][0]),(name,trees)
        if name=='competing consumes':
            assert sorted('LeftWon' if 'LeftWon' in a else 'RightWon' for a,_ in out)==['LeftWon','RightWon']
        elif name=='equal physical tokens':
            assert all(a.count('Token')==1 and 'Taken' in a for a,_ in out)
            assert len({quoted(dict(t[0][2:])['dependencies'])[0][0][2] for _,t in out})==2
        elif name=='duplicate-consuming orders':
            assert all('Token' not in a and 'LeftWon' in a and 'RightWon' in a for a,_ in out)
        elif name=='persistent read orders':
            assert all(a.count('Token')==1 and 'LeftRead' in a and 'RightRead' in a for a,_ in out)
        elif name=='binding alternatives':
            assert sorted(x[1] for a,_ in out for x in a if isinstance(x,list) and x[0]=='Taken')==['A','B']
        elif name=='complete read vs insertion':
            assert sorted(len(x[1]) for a,_ in out for x in a if isinstance(x,list) and x[0]=='Result')==[1,2]
        elif name=='absence vs insertion':
            assert sorted('Passed' in a for a,_ in out)==[False,True]
        elif name=='created exec and retained bindings':
            assert sorted(x for a,_ in out for x in a if isinstance(x,list) and x[0]=='Pair')==[['Pair','A','B'],['Pair','B','A']]
        gate.passed_case('snapshot worlds '+name)

    rows=['Token','Token','(fact A)',
        '(exec A (I (take Token) (all (fact $v) $rows)) (O (+ (A $rows))))',
        '(exec B (I (take Token)) (O (+ B)))']
    _,baseline=gate.run('world whole baseline',source(rows)+f'!(select 100 {policy(kind)} &jobs)')
    expected=world_observations(baseline[-1])
    for budget in [0,1,7,23,59,113,211,389]:
        trees,results=gate.run(f'world split {budget}',source(rows)+
            f'!(bind! &saved (select 100 {policy(kind,budget)} &jobs))\n'
            f'!(let {PATTERN} &saved (Split $v (select 100 {policy(kind)} $r)))')
        first=trees[-1][0][1];last=results[-1]
        joined={**last,'values':first+last['values']}
        assert last['closed']=='True' and world_observations(joined)==expected,(budget,trees,results)
        gate.passed_case(f'every world/return alternative survives {budget}-quantum episode split')
    for budget in [0,7,59,211]:
        _,results=gate.run(f'world stale snapshot {budget}',source(rows)+
            f'!(let {PATTERN} (select 100 {policy(kind,budget)} &jobs) '
            f'(let $_ (add-atom &jobs Phantom) (select 100 {policy(kind)} $r)))')
        assert results[-1]['status']=='invalidated' and results[-1]['closed']=='False' and not results[-1]['values'],results
        gate.passed_case(f'pinned world authority rejects source insertion after {budget} quanta')
    # A reflected exec can regenerate itself while a conflicting take closes.
    looprows=['Token','(exec Spin (I (take Token) (read (exec Spin $inputs $outputs))) (O (+ Token) (+ (exec Spin $inputs $outputs))))',
              '(exec Done (I (take Token)) (O (+ Win)))']
    _,results=gate.run('finite world beside infinite descendant',source(looprows)+
        f'!(select 1 {policy(kind,2000)} &jobs)')
    assert len(worlds(results[-1]))==1 and results[-1]['satisfied']=='True' and results[-1]['closed']=='False',results
    assert 'Win' in worlds(results[-1])[0][0],results
    gate.passed_case('a finite snapshot world is served beside an infinite competing lineage')

    effectrows=['(exec Write (I) (O (+ Marker) (+ (println! "RECEIPT-MUST-BE-DATA"))))']
    for profile in ('Transaction',kind):
        trees,results=gate.run('stored inert resource receipt '+profile,source(effectrows)+
            f'!(bind! &saved (select 100 {policy(profile,9)} &jobs))\n'
            f'!(let {PATTERN} &saved (Stored (quote $v){{$v}} $c $t))\n'
            f'!(bind! &finished (let {PATTERN} &saved (select 100 {policy(profile)} $r)))\n'
            f'!(let {PATTERN} &finished (Finished (quote $v){{$v}} $c $t))\n'
            f'!(let {PATTERN} &finished (select 100 {policy(profile)} $r))\n!(collapse (get-atoms &jobs))')
        assert results[-1]['closed']=='True' and not results[-1]['values'],(profile,trees,results)
        assert gate.receipts[-1]['stdout'].splitlines().count('RECEIPT-MUST-BE-DATA')==0
        finished=trees[-3][0]
        assert finished[0]=='Finished' and finished[2:]==['True','closed'],finished
        settled={'values':quoted(finished[1])}
        if profile=='Transaction':
            assert trees[-1][0].count('Marker')==1,trees
            assert len(settled['values'])==1,settled
        else:
            assert len(worlds(settled))==1 and 'Marker' in worlds(settled)[0][0]
            assert 'Marker' not in trees[-1][0],trees
        gate.passed_case('stored '+profile+' receipts execute no embedded code and resume once')

    trees,results=gate.run('world residual owned storage',source(rows)+
        f'!(let {PATTERN} (select 100 {policy(kind,59)} &jobs) '
        f'(Stored (cost:storage $r) (select 100 {policy(kind)} $r)))')
    storage=trees[-1][0][1]
    assert dict(storage[2:])['ownership']=='Complete' and results[-1]['closed']=='True',(trees,results)
    gate.passed_case('snapshot worlds, borrowed ancestors and unissued children are in owned census')

    trees,results=gate.run('native conflicting-world client',
        (ROOT/'tests/prime/native_resources/competing_worlds.metta').read_text())
    assert results[-1]['closed']=='True' and len(worlds(results[-1]))==2,(trees,results)
    assert 'Token' in trees[-1][0] and len(trees[-1][0])==3,trees
    gate.passed_case('authored conflicting-world client retains both choices under WO')

    rng=random.Random(6817)
    for case in range(count):
        tokens=[rng.randrange(3) for _ in range(rng.randrange(1,4))]
        execs=list(range(rng.randrange(1,4)))
        atom_rows=[['Token',x] for x in tokens]+[['exec',n,['I',['take',['Token','$x']]],
            ['O',['+',['Done',n,'$x']]]] for n in execs]
        def reference(state,trace):
            children=[]
            for e,entry in enumerate(state):
                if entry[0]!='exec':continue
                for t,token in enumerate(state):
                    if token[0]!='Token':continue
                    child=[x for i,x in enumerate(state) if i not in (e,t)]+[['Done',entry[1],token[1]]]
                    children.extend(reference(child,trace+[[entry[1],e,t,token[1]]]))
            return children or [(state,trace)]
        expected=sorted((json.dumps(bag(atoms),sort_keys=True),json.dumps(trace))
                        for atoms,trace in reference(atom_rows,[]))
        authored=[f'(Token {x})' for x in tokens]+[
            f'(exec {n} (I (take (Token $x))) (O (+ (Done {n} $x))))' for n in execs]
        for strategy in ('FIFO','WO'):
            pol=policy(kind) if strategy=='FIFO' else '(search-policy (control WO (resources (Transaction worlds)) (key (exec $n $in $out) (- 10 $n)) (budget 100000)))'
            _,results=gate.run(f'generated world {case} {strategy}',source(authored)+f'!(select 100 {pol} &jobs)')
            actual=[]
            for atoms,trace in worlds(results[-1]):
                history=[]
                for step in trace:
                    fields=dict(step[2:]);dep=quoted(fields['dependencies'])[0][0]
                    history.append([quoted(step[1])[1],fields['selected-occurrence'],dep[2],dep[3][1]])
                actual.append((json.dumps(bag(atoms),sort_keys=True),json.dumps(history)))
            assert results[-1]['closed']=='True' and sorted(actual)==expected,(case,strategy,actual,expected,results)
            gate.passed_case(f'independent occurrence/ordered-history world reference {case} {strategy}')


def physical_matches(gate):
    from test_prime_native_parallel import check_cost
    def controlled(workers,budget=100000,evidence=True):
        receipt='(receipts True) (costs native-events-v1)' if evidence else ''
        return f'(search-policy (control FIFO (resources (Transaction worlds)) (workers {workers}) (budget {budget}) {receipt}))'
    cases=[
        ('persistent read',['Token','(exec Left (I (read Token)) (O (+ Left)))',
            '(exec Right (I (read Token)) (O (+ Right)))']),
        ('competing consumes',['Token','(exec Left (I (take Token)) (O (+ Left)))',
            '(exec Right (I (take Token)) (O (+ Right)))']),
        ('duplicate tokens and joins',['(Token A)','(Token A)','(Fact A)',
            '(exec Left (I (take (Token $x)) (read (Fact $x))) (O (+ (Left $x))))',
            '(exec Right (I (take (Token $x)) (read (Fact $x))) (O (+ (Right $x))))']),
        ('all reads and generated work',['(Fact A)',
            '(exec Read (I (all (Fact $x) $rows)) (O (+ (Result $rows))))',
            '(exec Insert (I) (O (+ (Fact B)) (+ (exec Later (I (read (Fact B))) (O (+ Done))))))']),
    ]
    issued=0
    for name,rows in cases:
        _,serial=gate.run('resource matcher serial '+name,source(rows)+f'!(select 100 {controlled(1)} &jobs)')
        expected=world_observations(serial[-1])
        for width in (2,3):
            trees,parallel=gate.run(f'resource matcher width {width} '+name,source(rows)+
                f'!(select 100 {controlled(width)} &jobs)\n!(collapse (get-atoms &jobs))')
            result=parallel[-1]
            assert result['closed']=='True' and world_observations(result)==expected,(name,width,result,expected)
            check_cost(result);issued+=len(result.get('parallel-waves',[]))
            assert len(trees[-1][0])==len(rows),trees
            gate.passed_case(f'physical prepared matches preserve {name} at width {width}')
    assert issued>0,'No resource matching wave was issued'
    rows=cases[2][1]
    _,serial=gate.run('resource matcher split reference',source(rows)+f'!(select 100 {controlled(1)} &jobs)')
    expected=world_observations(serial[-1])
    for budget in (0,1,27,83,151,307):
        trees,split=gate.run(f'resource matcher split {budget}',source(rows)+
            f'!(let {PATTERN} (select 100 {controlled(2,budget,False)} &jobs) '
            f'(Split $v (select 100 {controlled(3,evidence=False)} $r)))')
        assert split and split[-1]['closed']=='True',split
        combined={'values':trees[-1][0][1]+split[-1]['values']}
        assert world_observations(combined)==expected,(budget,trees,split,expected)
        gate.passed_case(f'prepared match residual survives pause/reconfigured width at {budget}')
    pattern=' '.join(f'$v{i}' for i in range(512))
    values=' '.join(str(i) for i in range(512))
    rows=[f'(Fact ({values}))',
          f'(exec Left (I (read (Fact ({pattern})))) (O (+ (Left $v511))))',
          f'(exec Right (I (read (Fact ({pattern})))) (O (+ (Right $v511))))']
    _,results=gate.run('physical resource matcher overlap',source(rows)+f'!(select 100 {controlled(2)} &jobs)')
    result=results[-1];check_cost(result)
    waves=result.get('parallel-waves',[])
    assert result['closed']=='True' and len(worlds(result))==2,result
    overlap=any(left[5]!=right[5] and max(left[6],right[6])<min(left[7],right[7])
        for wave in waves for i,left in enumerate(wave[2]) for right in wave[2][i+1:])
    assert overlap,('No overlapping physical matching intervals',waves)
    gate.passed_case('actual private resource matcher work overlaps on distinct pool workers')

    pattern=' '.join(f'$v{i}' for i in range(96))
    values=' '.join(str(i) for i in range(96))
    rows=[f'(Fact ({values}))',f'(Fact ({values}))',
        f'(exec Left (I (take (Fact ({pattern})))) (O (+ (Left $v95))))',
        f'(exec Right (I (read (Fact ({pattern})))) (O (+ (Right $v95))))']
    # A fixed controller retains the partially matched row, paid worker
    # result and full continuation across cuts inside the canonical matcher.
    # Width changes have a separate observation comparison above.
    for width in (1,2,3):
        _,whole=gate.run(f'deep resource whole width {width}',source(rows)+
            f'!(select 100 {controlled(width,5000,False)} &jobs)')
        whole=whole[-1]
        assert whole['closed']=='True' and len(worlds(whole))==6,whole
        for cut in (0,1,7,19,27,39,51,83,151,307):
            trees,split=gate.run(f'deep resource split width {width} cut {cut}',source(rows)+
                f'!(let {PATTERN} (select 100 {controlled(width,cut,False)} &jobs) '
                f'(Split $v (select 100 {controlled(width,5000-cut,False)} $r)))\n'
                '!(collapse (get-atoms &jobs))')
            last=split[-1]
            joined={**last,'values':trees[-2][0][1]+last['values']}
            assert worlds(joined)==worlds(whole),(width,cut,joined,whole)
            assert all(last[k]==whole[k] for k in ('work','closed','satisfied','status')),(width,cut,last,whole)
            assert len(trees[-1][0])==len(rows),trees
            gate.passed_case(f'deep matcher width {width} cut {cut} retains ordered worlds and exact work')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,default=ROOT/'cetta')
    parser.add_argument('--timeout',type=int,default=30)
    parser.add_argument('--cases',type=int,default=5)
    parser.add_argument('--receipt',type=Path)
    args=parser.parse_args();args.binary=args.binary.resolve();gate=Gate(args)
    try:
        fixed(gate);split(gate,args.cases);priorities(gate);current_scope(gate);generated(gate,args.cases)
        mm2_correspondence(gate, args.cases)
        pending_resumption(gate)
        snapshot_worlds(gate,args.cases)
        physical_matches(gate)
    finally:
        if args.receipt:
            args.receipt.parent.mkdir(parents=True,exist_ok=True)
            args.receipt.write_text(json.dumps({'binary_sha256':sha256(args.binary.read_bytes()).hexdigest(),
                'passed':gate.passed,'receipts':gate.receipts},indent=2)+'\n')
    print(f'Prime native resources: {gate.passed} passed, 0 failed')


if __name__=='__main__':main()
