#!/usr/bin/env python3
"""Program-level native agenda and resource-contract controls."""
from __future__ import annotations
import argparse
from hashlib import sha256
import json
from pathlib import Path
import random
import re
from test_prime_native_control import Gate, ROOT, bag, parse


def policy(kind='Transaction', budget=100000):
    return f'(search-policy (control FIFO (resources {kind}) (budget {budget})))'


PATTERN = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t) (agenda $a))'


def fresh_frame_syntax(value):
    """Compare these syntax fixtures modulo one bijection of fresh frames.

    Authored names, variable sharing, physical row coordinates and all data
    remain rigid. Apply once to the complete compared observation, not once
    per row. These fixtures contain no string literals resembling variables;
    the generic result parser does not retain that token distinction.
    """
    frames = {}
    def visit(term):
        if isinstance(term, list):
            return [visit(child) for child in term]
        matched = re.fullmatch(r'(\$[^#]+)#([0-9]+)', term) if isinstance(term, str) else None
        if matched:
            name, frame = matched.groups()
            return f'{name}#frame{frames.setdefault(frame, len(frames))}'
        return term
    return visit(value)


def source(rows):
    return '!(bind! &jobs (new-space))\n' + '\n'.join(f'!(add-atom &jobs {row})' for row in rows) + '\n'


def inspect(gate, name, rows, kind='Transaction', suffix=''):
    trees, results=gate.run(name,source(rows)+f'!(select 100 {policy(kind)} &jobs)\n'+suffix+'\n!(collapse (get-atoms &jobs))')
    assert results, (name,trees)
    return trees[-1][0],results[-1]


def state_world_diagnostic(gate):
    """Expose comparison profiles without adopting one as the expected law."""
    source = '''
!(let $a (new-state 1) (let $b (new-state 2) (== $a $b)))
!(let $a (new-state 1) (let $b (new-state 1)
   (let $_ (change-state! $b 2)
     ((get-state $a) (get-state $b) (== $a $b)
      (== (get-state $a) (get-state $b))))))
!(let $n (/ 0.0 0.0) (let $s (new-state $n) (== $s $s)))
'''
    results = {}
    for language in ('prime', 'he'):
        trees, _ = gate.run(f'{language} State comparison world diagnostic', source,
                            language=language)
        assert len(trees) == 3 and len(trees[0]) == len(trees[2]) == 1, trees
        assert trees[0][0] == 'False', trees
        assert len(trees[1]) == 1 and trees[1][0][:2] == [1, 2], trees
        changed = trees[1][0]
        assert len(changed) == 4 and all(x in ('True', 'False') for x in changed[2:]), trees
        assert trees[2][0] in ('True', 'False'), trees
        results[language] = {'changed_world': changed, 'same_nan_state': trees[2][0]}
        gate.receipts[-1]['state_comparison_observation'] = results[language]
    print('State comparison diagnostic (no policy selected): ' +
          json.dumps(results, sort_keys=True))
    gate.passed_case('both routes expose the requested State values and comparison results')


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


def kept_read_identification(gate):
    base = [['Template', '$x#11', '$y#11'], ['Use', '$x#11']]
    assert fresh_frame_syntax(base) == fresh_frame_syntax([
        ['Template', '$x#97', '$y#97'], ['Use', '$x#97']])
    for changed in (
            [['Template', '$x#97', '$y#98'], ['Use', '$x#97']],
            [['Template', '$x#97', '$y#97'], ['Use', '$x#98']],
            [['Template', '$x#97', '$y#97'], ['Use', 'Captured']]):
        assert fresh_frame_syntax(base) != fresh_frame_syntax(changed)
    gate.passed_case('fresh-frame observation rejects changed frame sharing, aliases and captured values')
    for order in ('(read Token) (take Token)', '(take Token) (read Token)'):
        rule = f'(exec Claim (I {order}) (O (+ Done)))'
        for copies in (1, 2):
            values, result = inspect(gate, f'kept/taken identification {order} copies={copies}',
                                     ['Token'] * copies + [rule])
            commits = copies - 1
            assert len(result['values']) == commits, (values, result)
            assert values.count('Token') == 1 and values.count('Done') == commits, (values, result)
            gate.passed_case(f'kept input is distinct from takes {order} copies={copies}')
            _, result = gate.run(f'world kept/taken identification {order} copies={copies}',
                source(['Token'] * copies + [rule]) +
                f'!(select 100 {policy("(Transaction worlds)")} &jobs)')
            alternatives = worlds(result[-1])
            assert len(alternatives) == copies, alternatives
            assert all(len(trace) == commits and atoms.count('Token') == 1 and
                       atoms.count('Done') == commits for atoms, trace in alternatives), alternatives
            gate.passed_case(f'world matching preserves distinct kept/taken occurrences {order} copies={copies}')

    # Overlapping patterns intentionally express a reserve. The lint warns
    # about the migration risk; it does not reject this lawful use.
    rows = [f'(tok {n})' for n in (1, 2, 3)] + [
        f'(exec {key} (I (read (tok $x)) (take (tok $y))) (O (+ (took $y))))'
        for key in ('A', 'B', 'C')]
    values, result = inspect(gate, 'D2 keeps a reserve while consuming distinct tokens', rows)
    assert len(result['values']) == 2 and ['tok', 1] in values, (values, result)
    assert sorted(row for row in values if isinstance(row, list) and row[0] == 'took') == [
        ['took', 2], ['took', 3]], values
    gate.passed_case('D2 preserves one token and emits exactly two takes')

    for order in ('(all Token $seen) (take Token)', '(take Token) (all Token $seen)'):
        values, result = inspect(gate, f'predicate query and take {order}',
            ['Token', f'(exec Query (I {order}) (O (+ (Seen $seen))))'])
        assert values == [['Seen', ['Token']]] and len(result['values']) == 1, (values, result)
        gate.passed_case(f'predicate query observes pre-state without keeping the taken occurrence {order}')


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


def overlapping_workers(result):
    """Physical intervals witness distinct workers, not source independence."""
    return any(left[5] != right[5] and
               max(left[6], right[6]) < min(left[7], right[7])
               for wave in result.get('parallel-waves', [])
               for index, left in enumerate(wave[2])
               for right in wave[2][index + 1:])


def causal_world_readout(gate, resource_checker=None):
    """Use the cold reader on real native histories, including residual IDs."""
    import copy
    import sys
    sys.path.insert(0, str(ROOT / 'tools'))
    try:
        from native_resource_history import (read_history, HistoryRefused,
                                             check_resource_projection, resource_request, resource_cost)
        from mm2_to_metta import parse as read_tokens
    finally:
        sys.path.pop(0)

    def tokens(value):
        if isinstance(value, list):
            return [tokens(item) for item in value]
        return str(value)

    def capture(name, rows, control=None, require_workers=False, check_views=None):
        control = control or policy('(Transaction worlds)')
        initial = [read_tokens(row)[0] for row in rows]
        # A short slice may legally finish before another pool thread wakes.
        # Check every attempt's source observations while collecting a real
        # overlap witness; an unsuccessful attempt is retained in the receipt.
        for attempt in range(8 if require_workers else 1):
            label = name if not attempt else f'{name} scheduling attempt {attempt + 1}'
            _, results = gate.run(label, source(rows) +
                f'!(select 100 {control} &jobs)')
            assert results[-1]['closed'] == 'True', results
            if require_workers:
                from test_prime_native_parallel import check_cost
                check_cost(results[-1])
            views = [read_history(initial, tokens(world)) for world in results[-1]['values']]
            for view in views:
                view['resource-cost'] = resource_cost(view)
            if resource_checker is not None:
                source_checks = []
                for view in views:
                    checked = check_resource_projection(resource_checker, initial, view)
                    assert (checked['resourceWord'] and checked['freshOccurrences'] and
                            checked['dependencyGraph'] and checked['sourcePayload']), checked
                    source_checks.append({'request': resource_request(initial, view), 'reply': checked})
                gate.receipts[-1]['resource_checks'] = source_checks
            if check_views is not None:
                check_views(views)
            if not require_workers or overlapping_workers(results[-1]):
                return initial, results[-1]['values'], views
        raise AssertionError('No overlapping physical matcher work observed')

    _, _, views = capture('causal empty resource interval', ['Token'])
    assert len(views) == 1 and views[0]['ordered'] == [], views
    assert views[0]['resource-cost'] == {'profile': 'resource-firings-v1', 'work': 0,
                                        'dependencyDepth': 0}, views
    gate.passed_case('retained initial resources do not invent source work in an empty interval')

    rows = ['Token', '(exec Left (I (read Token)) (O (+ LeftRead)))',
            '(exec Right (I (read Token)) (O (+ RightRead)))']
    initial, worlds, views = capture('causal persistent read orders', rows)
    assert len(views) == 2 and views[0]['ordered'] != views[1]['ordered'], views
    assert views[0]['final'] != views[1]['final'], views
    assert views[0]['causal'] == views[1]['causal'], views
    assert all(view['authority'] == 'None' for view in views)
    gate.passed_case('different ordered read firings have equal causal readout without granting authority')
    assert all(view['resource-cost'] == {'profile': 'resource-firings-v1', 'work': 2,
                                        'dependencyDepth': 1} for view in views), views
    gate.passed_case('two independent source firings have work two and dependency depth one')

    damaged = copy.deepcopy(tokens(worlds[0]))
    trace = dict(damaged[2:])['trace'][1]
    trace[1] = []  # erase the first firing, retaining the claimed final state
    try:
        read_history(initial, damaged)
    except HistoryRefused:
        pass
    else:
        raise AssertionError('missing native firing accepted')
    gate.passed_case('causal reader refuses a missing firing instead of trusting final atoms')

    # Prepared matching really runs on the pool; source publication remains
    # with the coordinator. The causal view forgets independent source order,
    # not worker timing or a competing consumer's chosen result.
    payload = '(Fact (' + ' '.join(str(i) for i in range(8192)) + '))'
    controlled = ('(search-policy (control FIFO (resources (Transaction worlds)) '
                  '(workers 2) (quantum 16384) (budget 200000) '
                  '(receipts True) (costs native-events-v1)))')
    for kind in ('read', 'take'):
        rows = [payload,
                f'(exec Left (I ({kind} {payload})) (O (+ Left)))',
                f'(exec Right (I ({kind} {payload})) (O (+ Right)))']
        def check_physical(physical):
            assert len(physical) == 2 and physical[0]['ordered'] != physical[1]['ordered']
            if kind == 'read':
                assert physical[0]['final'] != physical[1]['final']
                assert physical[0]['causal'] == physical[1]['causal']
                assert all(view['resource-cost'] == {
                    'profile': 'resource-firings-v1', 'work': 2, 'dependencyDepth': 1}
                    for view in physical)
            else:
                assert all(len(view['ordered']) == 1 for view in physical)
                assert physical[0]['causal'] != physical[1]['causal']
        capture(f'causal physical worker {kind}', rows, controlled,
                require_workers=True, check_views=check_physical)
        if kind == 'read':
            gate.passed_case('overlapping workers feed different independent source orders with one checked causal view')
        else:
            gate.passed_case('overlapping competing matchers retain distinct consuming outcomes')

    rows = ['Token', '(exec Twice (I (read Token) (read Token)) (O (+ Done)))']
    _, _, views = capture('causal repeated persistent read', rows)
    assert len(views) == 1 and len(views[0]['ordered']) == 1, views
    firing = views[0]['ordered'][0]
    assert firing['inputs'] == [('read', (0,)), ('read', (0,))], firing
    assert firing['read'] == [(0,)] and firing['consumed'] == [(1,)], firing
    gate.passed_case('two persistent read requests retain both inputs and require one physical resource')
    assert views[0]['resource-cost']['work'] == views[0]['resource-cost']['dependencyDepth'] == 1, views
    gate.passed_case('repeated input reads do not charge a second source firing')

    rows = ['Token', '(exec Left (I (take Token)) (O (+ LeftWon)))',
            '(exec Right (I (take Token)) (O (+ RightWon)))']
    _, _, views = capture('causal competing consumes', rows)
    assert len(views) == 2 and views[0]['causal'] != views[1]['causal'], views
    assert all(len(view['ordered']) == 1 for view in views)
    gate.passed_case('competing consumes remain different causal observations')

    rows = ['Token', '(exec Read (I (read Token)) (O (+ ReadDone)))',
            '(exec Take (I (take Token)) (O (+ TakeDone)))']
    _, _, views = capture('causal read before consuming conflict', rows)
    completed = [view for view in views if len(view['ordered']) == 2]
    assert len(completed) == 1, views
    read, take = completed[0]['causal']['events']
    assert tuple(read['occurrence']) == (1,) and take['parents'] == [(1,)], views
    gate.passed_case('read-before-take dependency survives causal readout')
    assert completed[0]['resource-cost']['work'] == completed[0]['resource-cost']['dependencyDepth'] == 2, views
    gate.passed_case('read-before-take keeps two dependent source firings')

    rows = ['Token', '(exec Job (I (read Token)) (O (+ Done)))',
            '(exec Job (I (read Token)) (O (+ Done)))']
    _, _, views = capture('causal equal syntax physical occurrences', rows)
    assert len(views) == 2 and views[0]['ordered'] != views[1]['ordered'], views
    assert views[0]['causal'] == views[1]['causal'], views
    for view in views:
        assert {tuple(event['occurrence']) for event in view['ordered']} == {(1,), (2,)}
        assert len(view['causal']['final']) == 3
    gate.passed_case('equal exec syntax and duplicate answers retain separate physical occurrence keys')

    rows = ['(exec Start (I) (O (+ Token) (+ (exec Next (I (take Token)) (O (+ Done))))))']
    _, _, views = capture('causal created execution and producer edges', rows)
    assert len(views) == 1 and len(views[0]['ordered']) == 2, views
    first, second = views[0]['causal']['events']
    assert tuple(first['occurrence']) == (0,) and tuple(second['occurrence']) == (0, 1)
    assert second['parents'] == [(0,)], views
    gate.passed_case('created execution retains its producer and output-slot identities')

    rows = ['(exec Start (I) (O (+ Token) '
            '(+ (exec Left (I (read Token)) (O (+ LeftRead)))) '
            '(+ (exec Right (I (read Token)) (O (+ RightRead))))))']
    _, _, views = capture('causal independent work below created producer', rows)
    assert len(views) == 2 and views[0]['ordered'] != views[1]['ordered'], views
    assert views[0]['causal'] == views[1]['causal'], views
    for view in views:
        events = view['causal']['events']
        assert [tuple(event['occurrence']) for event in events] == [(0,), (0, 1), (0, 2)]
        assert events[1]['parents'] == events[2]['parents'] == [(0,)]
    gate.passed_case('independent created work commutes while retaining the shared producer prefix')
    assert all(view['resource-cost']['work'] == 3 and view['resource-cost']['dependencyDepth'] == 2
               for view in views), views
    gate.passed_case('shared producer contributes once to source work and dependency depth')

    if resource_checker is not None:
        import subprocess
        initial, _, views = capture('source graph mutation controls', [
            '(exec Start (I) (O (+ Token) '
            '(+ (exec Left (I (read Token)) (O (+ LeftRead)))) '
            '(+ (exec Right (I (read Token)) (O (+ RightRead))))))'])
        request = resource_request(initial, views[0])
        for label, enable_cost in [('omitted cost request', None), ('disabled cost request', False)]:
            plain = copy.deepcopy(request)
            if enable_cost is None:
                plain.pop('costs')
            else:
                plain['costs'] = enable_cost
            result = subprocess.run([str(resource_checker.resolve())],
                                    input=json.dumps(plain) + '\n',
                                    capture_output=True, text=True, check=True)
            reply = json.loads(result.stdout)
            assert reply['accepted'] is True and reply['resourceCost'] is None, reply
            gate.receipts[-1]['resource_checks'].append({'policy': label, 'request': plain, 'reply': reply})
            gate.passed_case('source graph checker keeps costs absent for ' + label)
        damaged = copy.deepcopy(request)
        damaged['graph'][1]['parents'] = []
        duplicate = copy.deepcopy(request)
        duplicate['graph'].append(copy.deepcopy(duplicate['graph'][1]))
        unavailable = copy.deepcopy(request)
        unavailable['word'][1]['reads'].append([999])
        for label, invalid in [('missing producer edge', damaged),
                               ('duplicate physical vertex', duplicate),
                               ('unavailable source resource', unavailable)]:
            result = subprocess.run([str(resource_checker.resolve())],
                                    input=json.dumps(invalid) + '\n',
                                    capture_output=True, text=True, check=True)
            reply = json.loads(result.stdout)
            assert reply['accepted'] is False and reply['authority'] == 'None', reply
            gate.receipts[-1]['resource_checks'].append({'mutation': label, 'request': invalid, 'reply': reply})
            gate.passed_case('source resource checker refuses ' + label)

        changed_cost = copy.deepcopy(views[0])
        changed_cost['resource-cost']['work'] += 1
        try:
            check_resource_projection(resource_checker, initial, changed_cost)
        except HistoryRefused as error:
            assert str(error) == 'ResourceCostMismatch', error
            gate.receipts[-1]['client_refusals'] = [{'mutation': 'changed claimed source work',
                'request': resource_request(initial, changed_cost),
                'claimed_cost': changed_cost['resource-cost'], 'refusal': str(error)}]
        else:
            raise AssertionError('changed source work accepted')
        gate.passed_case('a supplied work number cannot replace the source graph valuation')

        legacy = copy.deepcopy(request)
        legacy.pop('payloads')
        result = subprocess.run([str(resource_checker.resolve())],
                                input=json.dumps(legacy) + '\n',
                                capture_output=True, text=True, check=True)
        reply = json.loads(result.stdout)
        assert reply['accepted'] is True and reply['sourcePayload'] is None, reply
        gate.receipts[-1]['resource_checks'].append({'policy': 'resource view only',
            'request': legacy, 'reply': reply})
        gate.passed_case('a footprint-only request does not acquire a payload checking claim')

        def changed_at(original, path, value):
            altered = copy.deepcopy(original)
            current = altered
            for key in path[:-1]:
                current = current[key]
            current[path[-1]] = value
            return altered

        mutations = [
            ('changed selected action', ['payloads', 'word', 0, 'action', 1], 'Forged'),
            ('changed initial action', ['payloads', 'initial', 0, 1], 'Forged'),
            ('changed premise payload', ['payloads', 'word', 1, 'inputs', 0, 'value'], 'Forged'),
            ('omitted source premise', ['payloads', 'word', 1, 'inputs'], []),
            ('changed created payload', ['payloads', 'word', 0, 'produced', 0, 'value'], 'Forged'),
            ('changed created identity', ['payloads', 'word', 0, 'produced', 0, 'key'], [999]),
            ('changed final payload', ['payloads', 'final', 0, 'value'], 'Forged'),
            ('reordered final physical rows', ['payloads', 'final'],
             list(reversed(request['payloads']['final']))),
            ('changed output footprint', ['word', 0, 'outputs'], 99),
            ('unsupported payload profile', ['payloads', 'profile'], 'unknown'),
        ]
        for label, path, value in mutations:
            invalid = changed_at(request, path, value)
            result = subprocess.run([str(resource_checker.resolve())],
                                    input=json.dumps(invalid) + '\n',
                                    capture_output=True, text=True, check=True)
            reply = json.loads(result.stdout)
            assert reply['accepted'] is False and reply['sourcePayload'] is False, reply
            assert reply['authority'] == 'None'
            gate.receipts[-1]['resource_checks'].append({'mutation': label,
                'request': invalid, 'reply': reply})
            gate.passed_case('source payload checking refuses ' + label)

        initial, _, repeated = capture('source ordered repeated premise', [
            'Token', '(exec Twice (I (read Token) (read Token)) (O (+ Done)))'])
        invalid = resource_request(initial, repeated[0])
        invalid['payloads']['word'][0]['inputs'].pop()
        result = subprocess.run([str(resource_checker.resolve())],
                                input=json.dumps(invalid) + '\n',
                                capture_output=True, text=True, check=True)
        reply = json.loads(result.stdout)
        assert reply['resourceWord'] is True and reply['dependencyGraph'] is True, reply
        assert reply['accepted'] is False and reply['sourcePayload'] is False, reply
        gate.receipts[-1]['resource_checks'].append({'mutation': 'omitted repeated read',
            'request': invalid, 'reply': reply})
        gate.passed_case('a valid physical footprint cannot hide an omitted repeated source premise')

    rows = ['(exec Ready (I (absent Stop)) (O (+ Done)))']
    _, results = gate.run('causal unsupported absence profile', source(rows) +
        f'!(select 100 {policy("(Transaction worlds)")} &jobs)')
    try:
        read_history([read_tokens(row)[0] for row in rows], tokens(results[-1]['values'][0]))
    except HistoryRefused as error:
        assert str(error) == 'UnsupportedInput', error
    else:
        raise AssertionError('absence silently given a ground read footprint')
    gate.passed_case('unsupported absence remains outside the selected causal profile')


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
    # Selection consumes this exec. Reflection needs its pre-state syntax,
    # so use a predicate query rather than promise to keep the same occurrence.
    looprows=['Token','(exec Spin (I (take Token) (all (exec Spin $i $o) ((exec Spin $inputs $outputs)))) (O (+ Token) (+ (exec Spin $inputs $outputs))))',
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
    # Matching retains bounded node slices independently of evaluator
    # quanta. A wider wave supplies enough real work for pool overlap.
    # Consuming one shared permit leaves one winning directive per world,
    # rather than exploring every ordering of the independent readers.
    size=1024
    readers=8
    pattern=' '.join(f'$v{i}' for i in range(size))
    values=' '.join(str(i) for i in range(size))
    rows=['Permit',f'(Fact ({values}))']+[
        f'(exec Reader{i} (I (take Permit) (read (Fact ({pattern})))) '
        f'(O (+ (Won Reader{i} $v{size-1}))))' for i in range(readers)]
    _,serial=gate.run('wide resource matcher serial',source(rows)+f'!(select 100 {controlled(1)} &jobs)')
    expected=world_observations(serial[-1])
    overlap_policy=controlled(4)
    _,results=gate.run('physical resource matcher overlap',source(rows)+f'!(select 100 {overlap_policy} &jobs)')
    result=results[-1];check_cost(result)
    waves=result.get('parallel-waves',[])
    assert result['closed']=='True' and len(worlds(result))==readers,result
    assert world_observations(result)==expected,(result,expected)
    assert overlapping_workers(result),('No overlapping physical matching intervals',waves)
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


INTERACTION_PAGE_PATTERN = ('(InteractionChoices (choices $choices) (status $status) '
                            '(pending $pending) (quanta $quanta) (residual $residual))')


def interaction_records(value, head):
    if isinstance(value, list):
        if value and value[0] == head:
            yield value
        else:
            for child in value:
                yield from interaction_records(child, head)


def interaction_discovery(budget=1000, profile=None):
    option = '' if profile is None else profile + ' '
    return (f'!(bind! &menu (interaction:choices &jobs {option}{budget}))\n'
            f'!(bind! &discovery (let {INTERACTION_PAGE_PATTERN} &menu $residual))\n')


def semantic_choices(gate):
    records = interaction_records
    discovery = interaction_discovery
    held = f'!(bind! &held (let {PATTERN} &selected $r))\n'
    rows = ['(exec A (I) (O (+ A)))', '(exec B (I) (O (+ B)))']
    trees, _ = gate.run('authored discovery reference', source(rows) + discovery() +
        '!(let $menu &menu $menu)\n!(collapse (get-atoms &jobs))')
    reference = next(records(trees, 'InteractionChoices'))
    fields = dict(reference[1:])
    assert fields['status'] == 'Exhausted' and len(fields['choices']) == 2, trees
    assert [choice[1] for choice in fields['choices']] == [1, 2], fields
    assert trees[-1][0] == [['exec', 'A', ['I'], ['O', ['+', 'A']]],
                           ['exec', 'B', ['I'], ['O', ['+', 'B']]]], trees
    gate.passed_case('authored discovery returns two owned alternatives without publication')
    for cut in (0, 1, 3, 7, 13, 19, 100):
        trees, _ = gate.run(f'authored discovery cut {cut}', source(rows) + discovery(cut) +
            '!(let $menu &menu $menu)\n!(interaction:choices &discovery 1000)\n'
            '!(interaction:choices &discovery 0)\n!(collapse (get-atoms &jobs))')
        pages = [dict(record[1:]) for record in records(trees, 'InteractionChoices')]
        assert len(pages) == 3, trees
        assert pages[0]['choices'] + pages[1]['choices'] == fields['choices'], pages
        assert pages[-1]['choices'] == [] and pages[-1]['status'] == 'Exhausted', pages
        assert pages[1]['quanta'] == pages[2]['quanta'] == fields['quanta'], pages
        if cut == 0:
            assert pages[0]['quanta'] == 0 and pages[0]['status'] == 'Pending', pages
        gate.passed_case(f'authored discovery cut {cut} retains suffix, physical choices and paid quanta')
    for cut in (0, 1, 3, 7, 11, 100):
        trees, results = gate.run(f'authored selected execution cut {cut}', source(rows) + discovery() +
            f'!(bind! &selected (interaction:perform &discovery 2 {cut}))\n'
            '!(let $selected &selected $selected)\n' + held +
            '!(interaction:perform &held 1000)\n!(interaction:perform &held 1000)\n'
            f'!(select 2 {policy()} &held)\n!(collapse (get-atoms &jobs))')
        assert len(results) == 4, trees
        first, resumed, again, remaining = results
        selected_values = first['values'] + resumed['values']
        assert len(selected_values) == 1 and selected_values[0][1][1][1] == 'B', results
        assert again['values'] == [] and again['work'] == resumed['work'], results
        assert len(remaining['values']) == 1 and remaining['values'][0][1][1][1] == 'A', results
        assert trees[-1][0] == ['B', 'A'], trees
        assert all('history' not in result for result in results), results
        if cut == 0:
            assert first['values'] == [] and first['work'] == 0, first
        gate.passed_case(f'selected B at cut {cut} commits once, then the normal residual preserves A')
    for name, inputs, before, change in (
        ('consumed permit', '(take Token)', ['Token'], '!(remove-atom &jobs Token)'),
        ('changed absence', '(absent Stop)', [], '!(add-atom &jobs Stop)'),
        ('changed aggregate', '(all (Fact $x) $rows)', ['(Fact A)'], '!(add-atom &jobs (Fact B))'),
    ):
        trees, results = gate.run('authored stale ' + name, source(before +
            [f'(exec Chosen (I {inputs}) (O (+ Wrong)))']) + discovery() + change + '\n'
            '!(interaction:perform &discovery 1 1000)\n!(collapse (get-atoms &jobs))')
        assert results and results[-1]['values'] == [] and results[-1]['closed'] == 'False', results
        assert 'Wrong' not in trees[-1][0], trees
        gate.passed_case('authored selected publication refuses ' + name)
    trees, results = gate.run('authored unrelated extension', source(rows) + discovery() +
        '!(add-atom &jobs Unrelated)\n!(interaction:choices &discovery 1)\n'
        '!(interaction:perform &discovery 2 1000)\n!(collapse (get-atoms &jobs))')
    page = dict(next(records(trees, 'InteractionChoices'))[1:])
    assert page['status'] == 'Invalidated' and page['choices'] == [], trees
    assert len(results[-1]['values']) == 1 and trees[-1][0][-2:] == ['Unrelated', 'B'], trees
    gate.passed_case('enumeration invalidates on change while a selected unchanged proposal revalidates')
    trees, results = gate.run('authored counterfeit discovery', source(rows) + discovery() +
        '!(interaction:perform (NativeHandle "prime-space-discovery" 1) 2 1000)\n'
        '!(interaction:perform &discovery 2 1000)\n!(collapse (get-atoms &jobs))')
    assert len(results) == 1 and len(results[0]['values']) == 1 and trees[-1][0][-1] == 'B', trees
    gate.passed_case('a printed discovery identifier cannot transfer or execute a choice')
    trees, results = gate.run('authored scoring cannot take a choice', source(rows) + discovery() +
        '!(select 1 (search-policy (control WO (budget 10000) '
        '(key $candidate (interaction:perform &discovery 2 1000)))) (superpose (1 2)))\n'
        '!(collapse (get-atoms &jobs))\n'
        '!(interaction:perform &discovery 2 1000)\n!(collapse (get-atoms &jobs))')
    assert trees[-3][0] == [['exec', 'A', ['I'], ['O', ['+', 'A']]],
                             ['exec', 'B', ['I'], ['O', ['+', 'B']]]], trees
    assert len(results[-1]['values']) == 1 and trees[-1][0][-1] == 'B', trees
    gate.passed_case('a scoring caller cannot consume the retained choice or its permit')
    trees, results = gate.run('authored selection is linear', source(rows) + discovery() +
        '!(interaction:perform &discovery 2 1000)\n'
        '!(interaction:perform &discovery 2 1000)\n!(collapse (get-atoms &jobs))')
    assert len(results) == 1 and trees[-1][0] == [['exec', 'A', ['I'], ['O', ['+', 'A']]], 'B'], trees
    gate.passed_case('a transferred local choice cannot be taken twice')
    trees, results = gate.run('disposing discovery preserves selected continuation',
        source(rows) + discovery() +
        '!(bind! &selected (interaction:perform &discovery 2 0))\n' + held +
        '!(interaction:dispose &discovery)\n'
        '!(interaction:perform &held 1000)\n' +
        f'!(select 2 {policy()} &held)\n!(collapse (get-atoms &jobs))')
    assert list(records(trees, 'InteractionDisposed')) == [['InteractionDisposed', 'Discovery', 'True']], trees
    assert len(results) == 2 and all(len(report['values']) == 1 for report in results), trees
    assert trees[-1][0] == ['B', 'A'], trees
    gate.passed_case('disposing the cursor releases its catalogue while the selected owner resumes and retains other source work')
    trees, results = gate.run('disposing selected continuation preserves sibling',
        source(rows) + discovery() +
        '!(bind! &selected (interaction:perform &discovery 1 0))\n' + held +
        '!(interaction:dispose &held)\n!(interaction:perform &held 1000)\n'
        '!(interaction:perform &discovery 2 1000)\n!(collapse (get-atoms &jobs))')
    assert list(records(trees, 'InteractionDisposed')) == [['InteractionDisposed', 'Selected', 'True']], trees
    assert len(results) == 1 and len(results[0]['values']) == 1, trees
    assert trees[-1][0] == [['exec', 'A', ['I'], ['O', ['+', 'A']]], 'B'], trees
    gate.passed_case('cancelling one selected owner neither publishes its action nor destroys a sibling choice')
    trees, results = gate.run('counterfeit discovery cannot dispose the cursor',
        source(rows) + discovery() +
        '!(interaction:dispose (NativeHandle "prime-space-discovery" 1))\n'
        '!(interaction:perform &discovery 2 1000)\n!(collapse (get-atoms &jobs))')
    assert not list(records(trees, 'InteractionDisposed')) and len(results) == 1, trees
    assert trees[-1][0][-1] == 'B', trees
    gate.passed_case('textual handle data cannot dispose an owned cursor')
    for holder in ('discovery', 'held'):
        selected = ('!(bind! &selected (interaction:perform &discovery 2 0))\n' + held
                    if holder == 'held' else '')
        trees, results = gate.run('pure admission cannot dispose ' + holder,
            source(rows) + discovery() + selected +
            f'(= (dispose-verdict $weight) (chain (interaction:dispose &{holder}) $ignored True))\n'
            '(=w (dispose-guarded) GuardedBody 2 (admit dispose-verdict))\n'
            '!(weight:read (algebra 0 1 + *) (dispose-guarded))\n' +
            ('!(interaction:perform &held 1000)\n' if holder == 'held'
             else '!(interaction:perform &discovery 2 1000)\n') +
            '!(collapse (get-atoms &jobs))')
        assert not list(records(trees, 'InteractionDisposed')), trees
        assert results[-2]['status'] == 'fault' and results[-2]['values'] == [], results
        assert len(results[-1]['values']) == 1 and trees[-1][0][-1] == 'B', trees
        assert 'GuardedBody' not in str(trees), trees
        gate.passed_case('forced pure admission cannot dispose the ' + holder + ' owner or poison later use')


def raw_semantic_choices(gate):
    """Choose a raw activation before removal, matching or publication."""
    held = f'!(bind! &held (let {PATTERN} &selected $r))\n'
    rows = ['Permit', '(exec 0 (, Permit) (O (+ Alpha) (- Permit)))',
            '(exec 9 (, Permit) (O (+ Beta) (- Permit)))',
            '(exec 10 (, Missing) (O (+ Wrong)))']
    trees, _ = gate.run('raw activation discovery reference', source(rows) +
        interaction_discovery(profile='MM2') +
        '!(let $menu &menu $menu)\n!(collapse (get-atoms &jobs))')
    fields = dict(next(interaction_records(trees, 'InteractionChoices'))[1:])
    choices = fields['choices']
    assert fields['status'] == 'Exhausted' and len(choices) == 3, trees
    assert [choice[:3] for choice in choices] == [
        ['ActivationChoice', i, ['occurrence', i]] for i in (1, 2, 3)], choices
    assert all(len(choice) == 4 and choice[3][0] == 'source' for choice in choices), choices
    assert [choice[3][1][:2] for choice in choices] == [['exec', k] for k in (0, 9, 10)]
    initial = trees[-1][0]
    assert len(initial) == 4 and initial[0] == 'Permit', trees
    gate.passed_case('raw discovery offers unmatched directives without prepared outputs or mutation')
    for cut in (0, 1, 3, 7, 19):
        trees, _ = gate.run(f'raw activation discovery cut {cut}', source(rows) +
            interaction_discovery(cut, 'MM2') + '!(let $menu &menu $menu)\n'
            '!(interaction:choices &discovery 1000)\n!(interaction:choices &discovery MM2 0)\n'
            '!(collapse (get-atoms &jobs))')
        pages = [dict(record[1:]) for record in interaction_records(trees, 'InteractionChoices')]
        assert len(pages) == 3 and pages[0]['choices'] + pages[1]['choices'] == choices, pages
        assert pages[-1]['choices'] == [] and pages[-1]['status'] == 'Exhausted', pages
        assert pages[1]['quanta'] == pages[2]['quanta'] == fields['quanta'], pages
        assert trees[-1][0] == initial, trees
        if cut == 0:
            assert pages[0]['quanta'] == 0 and pages[0]['status'] == 'Pending', pages
        gate.passed_case(f'raw discovery cut {cut} retains exact suffix and paid quanta without matching')

    selected_work = None
    for cut in (0, 1, 3, 7, 11, 100):
        trees, results = gate.run(f'raw nonleast selection cut {cut}', source(rows) +
            interaction_discovery(profile='MM2') +
            f'!(bind! &selected (interaction:perform &discovery 2 {cut}))\n'
            '!(let $selected &selected $selected)\n' + held +
            '!(interaction:dispose &discovery)\n'
            '!(interaction:perform &held 1000)\n!(interaction:perform &held 1000)\n' +
            f'!(select 7 {policy("MM2")} &held)\n!(collapse (get-atoms &jobs))')
        assert len(results) == 4, trees
        first, resumed, again, remaining = results
        chosen = first['values'] + resumed['values']
        assert len(chosen) == 1 and chosen[0][1][1][1] == 9, results
        assert dict(chosen[0][2:])['order'] == 'program-choice', chosen
        assert again['values'] == [] and again['work'] == resumed['work'], results
        if selected_work is None:
            selected_work = resumed['work']
        assert resumed['work'] == selected_work, (cut, results)
        assert [row[1][1][1] for row in remaining['values']] == [0, 10], results
        assert all(dict(row[2:])['environments'] == 0 and
                   dict(row[2:])['order'] == 'compact-byte' for row in remaining['values']), results
        assert remaining['closed'] == 'True' and trees[-1][0] == ['Beta'], trees
        assert all('history' not in report for report in results), results
        if cut == 0:
            assert first['work'] == 0 and first['values'] == [], first
        gate.passed_case(f'raw selected Beta at cut {cut} consumes once; ordinary continuation retains both unmatched rules')
    values, _ = inspect(gate, 'raw default strategy competing permit', rows, 'MM2')
    assert values == ['Alpha'], values
    gate.passed_case('explicit nonleast choice differs from the unchanged default MM2 strategy')

    for label, change in (
        ('unrelated append', '!(add-atom &jobs Unrelated)\n'),
        ('equal directive replacement', f'!(remove-atom &jobs {rows[2]})\n!(add-atom &jobs {rows[2]})\n'),
    ):
        trees, results = gate.run('raw selected stale ' + label, source(rows) +
            interaction_discovery(profile='MM2') +
            '!(bind! &selected (interaction:perform &discovery 2 0))\n' + held + change +
            '!(interaction:perform &held 1000)\n!(interaction:perform &held 1000)\n'
            '!(collapse (get-atoms &jobs))')
        assert len(results) == 2 and all(not report['values'] and report['closed'] == 'False'
                                        for report in results), results
        assert all(dict(report['agenda'][1:])['obligation'] == 'StaleActivationSource'
                   for report in results), results
        assert 'Permit' in trees[-1][0] and 'Beta' not in trees[-1][0], trees
        assert any(row[:2] == ['exec', 9] for row in trees[-1][0] if isinstance(row, list)), trees
        gate.passed_case('raw captured activation refuses ' + label + ' without retargeting or consuming')

    trees, results = gate.run('raw choice profiles and linear ownership', source(rows) +
        interaction_discovery(profile='MM2') +
        '!(interaction:choices &discovery Transaction 1000)\n'
        '!(interaction:choices &jobs Unknown 1000)\n'
        '!(interaction:perform (NativeHandle "prime-space-discovery" 1) 2 1000)\n'
        '!(bind! &selected (interaction:perform &discovery 1 0))\n' + held +
        '!(interaction:dispose &held)\n!(interaction:perform &held 1000)\n'
        '!(interaction:perform &discovery 2 1000)\n'
        '!(interaction:perform &discovery 2 1000)\n!(collapse (get-atoms &jobs))')
    assert len(results) == 1 and len(results[0]['values']) == 1, trees
    assert list(interaction_records(trees, 'InteractionDisposed')) == [
        ['InteractionDisposed', 'Selected', 'True']], trees
    assert 'Beta' in trees[-1][0] and 'Permit' not in trees[-1][0], trees
    assert any(row[:2] == ['exec', 0] for row in trees[-1][0] if isinstance(row, list)), trees
    assert not list(interaction_records(trees, 'InteractionChoices')), trees
    gate.passed_case('profile changes, forged handles and repeat take grant no raw advancement; cancellation preserves siblings')

    trees, results = gate.run('raw discovery preserves unsupported source shell',
        source(['(exec 12 Unknown (O (+ Wrong)))']) + interaction_discovery(profile='MM2') +
        '!(let $menu &menu $menu)\n!(interaction:perform &discovery 1 1000)\n'
        '!(collapse (get-atoms &jobs))')
    offered = dict(next(interaction_records(trees, 'InteractionChoices'))[1:])['choices']
    assert len(offered) == 1 and offered[0][3][1][2] == 'Unknown', trees
    assert len(results) == 1 and results[0]['closed'] == 'False' and results[0]['values'] == [], results
    assert dict(results[0]['agenda'][1:])['obligation'] == 'UninterpretedExec', results
    assert trees[-1][0] == [], trees
    gate.passed_case('raw unsupported input consumes the selected directive and retains uninterpreted owned work')


def matching_choices(gate, history_controls=False):
    rows = ['(Fact A)', '(Fact B)', '(Fact A)',
            '(exec Pick (I (take (Fact $x))) (O (+ (Seen $x))))']
    profile = '(Transaction matches)'
    show = '!(let $menu &menu $menu)\n'
    held = f'!(bind! &held (let {PATTERN} &selected $r))\n'
    trees, _ = gate.run('all binding discovery reference', source(rows) +
                       interaction_discovery(profile=profile) + show)
    reference = dict(next(interaction_records(trees, 'InteractionChoices'))[1:])
    choices = reference['choices']
    assert reference['status'] == 'Exhausted' and len(choices) == 3, trees
    assert [choice[1] for choice in choices] == [1, 2, 3], choices
    assert [choice[2] for choice in choices] == [['occurrence', 3]] * 3, choices
    assert [choice[4][1] for choice in choices] == [
        ['O', ['+', ['Seen', value]]] for value in ('A', 'B', 'A')], choices
    assert [choice[5][1][0][2] for choice in choices] == [0, 1, 2], choices
    gate.passed_case('all bindings preserve equal results as distinct physical choices of one directive')
    trees, _ = gate.run('first binding discovery remains default', source(rows) +
                       interaction_discovery() + show)
    ordinary = dict(next(interaction_records(trees, 'InteractionChoices'))[1:])
    assert len(ordinary['choices']) == 1 and ordinary['choices'][0][4] == choices[0][4], trees
    gate.passed_case('ordinary Transaction discovery retains its first-binding contract')
    for cut in (0, 1, 5, 11, 17, 23, 100):
        trees, _ = gate.run(f'all binding discovery cut {cut}', source(rows) +
            interaction_discovery(cut, profile) + show +
            '!(interaction:choices &discovery 1000)\n!(interaction:choices &discovery 0)\n')
        pages = [dict(page[1:]) for page in interaction_records(trees, 'InteractionChoices')]
        assert len(pages) == 3 and pages[0]['choices'] + pages[1]['choices'] == choices, pages
        assert pages[1]['status'] == pages[2]['status'] == 'Exhausted', pages
        assert pages[2]['choices'] == [] and pages[1]['quanta'] == pages[2]['quanta'] == reference['quanta'], pages
        if cut == 0:
            assert pages[0]['quanta'] == 0 and pages[0]['status'] == 'Pending', pages
        gate.passed_case(f'all binding discovery retains the same join frontier and ordered suffix at cut {cut}')
    for ticket, value, index in ((2, 'B', 1), (3, 'A', 2)):
        for cut in (0, 3, 11, 100):
            trees, results = gate.run(f'chosen binding {ticket} cut {cut}', source(rows) +
                interaction_discovery(profile=profile) +
                f'!(bind! &selected (interaction:perform &discovery {ticket} {cut}))\n' +
                '!(let $selected &selected $selected)\n' + held +
                '!(interaction:dispose &discovery)\n!(interaction:perform &held 1000)\n'
                '!(interaction:perform &held 1000)\n!(collapse (get-atoms &jobs))')
            assert len(results) == 3, results
            completed = results[0]['values'] + results[1]['values']
            assert len(completed) == 1 and completed[0][0] == 'Committed', results
            dependencies = quoted(dict(completed[0][2:])['dependencies'])
            assert dependencies[0][0][2] == index, dependencies
            assert ['Seen', value] in trees[-1][0] and len(trees[-1][0]) == 3, trees
            assert not results[2]['values'] and results[2]['work'] == results[1]['work'], results
            if cut == 0:
                assert not results[0]['values'] and results[0]['work'] == 0, results
            gate.passed_case(f'binding ticket {ticket} at cut {cut} commits its exact occurrence once after catalogue disposal')
    guarded = ['(Fact A)', '(Fact B)', '(Item 1)',
               '(exec Join (I (read (Fact $x)) (all (Item $y) $rows) (absent (Stop $x))) '
               '(O (+ (Seen $x $rows))))']
    for change, allowed in (('(Stop A)', True), ('(Stop B)', False), ('(Item 2)', False)):
        for cut in (0, 7):
            trees, results = gate.run(f'chosen binding revalidation {change} cut {cut}',
                source(guarded) + interaction_discovery(profile=profile) +
                '!(bind! &selected (interaction:perform &discovery 2 0))\n' + held +
                f'!(interaction:perform &held {cut})\n!(add-atom &jobs {change})\n'
                '!(interaction:perform &held 1000)\n!(collapse (get-atoms &jobs))')
            assert all(not report['values'] for report in results[:-1]), results
            if allowed:
                assert len(results[-1]['values']) == 1, results
                assert ['Seen', 'B', [['Item', 1]]] in trees[-1][0], trees
            else:
                assert not results[-1]['values'] and results[-1]['closed'] == 'False', results
                assert dict(results[-1]['agenda'][1:])['obligation'] == 'ChangedPreparedChoice', results
                assert all(not isinstance(row, list) or row[0] != 'Seen' for row in trees[-1][0]), trees
            gate.passed_case(f'exact binding revalidation {change} at cut {cut} never retargets another valid match')
    trees, _ = gate.run('retained match discovery profile', source(rows) +
        interaction_discovery(0, profile) + show +
        '!(interaction:choices &discovery Transaction 1000)\n'
        '!(interaction:choices &discovery MM2 1000)\n'
        '!(interaction:choices &jobs (Transaction unknown) 1000)\n'
        '!(interaction:choices &discovery 1000)\n!(collapse (get-atoms &jobs))')
    pages = [dict(page[1:]) for page in interaction_records(trees, 'InteractionChoices')]
    assert len(pages) == 2 and pages[0]['choices'] == [] and pages[1]['choices'] == choices, pages
    assert len(trees[-1][0]) == len(rows), trees
    gate.passed_case('a retained discovery cannot change between first-binding, raw-activation and all-binding profiles')
    lexical = (f'!(let {INTERACTION_PAGE_PATTERN} (interaction:choices &jobs {profile} 1000) '
        f'(let {PATTERN} (interaction:perform $residual 2 1000) '
        '(BindingPort (size-atom $choices) $s (collapse (match &jobs (Seen $x) (Seen $x))))))')
    for language in ('prime', 'he', 'petta'):
        gate.run(f'{language} extended binding choice', source(rows) + lexical,
                 language=language, profile=None if language == 'prime' else 'extended')
        ports = list(interaction_records(parse(gate.receipts[-1]['stdout']), 'BindingPort'))
        assert ports == [['BindingPort', 3, 'True', [['Seen', 'B']]]], ports
        gate.passed_case(f'{language} keeps binding selection in the common extended interface')
    for language in ('he', 'petta'):
        trees, _ = gate.run(f'{language} default leaves binding discovery uninterpreted',
            source(rows) + f'!(interaction:choices &jobs {profile} 1000)\n'
            '!(collapse (get-atoms &jobs))', language=language)
        assert not list(interaction_records(trees, 'InteractionChoices')), trees
        state = (parse(gate.receipts[-1]['stdout'])[-1] if language == 'petta'
                 else trees[-1][0])
        assert len(state) == len(rows), state
        gate.passed_case(f'{language} default does not acquire the extended interaction service')
    if history_controls:
        setup = (source(rows) + interaction_discovery(profile=profile) +
            '!(bind! &selected (interaction:perform &discovery 2 0))\n' + held +
            '!(interaction:dispose &discovery)\n')
        state = '!(collapse (get-atoms &jobs))\n'
        def advance(allowance, observer=''):
            return ('!(select 1 (search-policy (control FIFO (resources Transaction) '
                    f'(budget {allowance}) {observer})) &held)\n')
        recording = '(history (selections 2048) (nodes 32) (bytes 65536) (owners 10000))'
        for cut in (0, 7, 1000):
            ordinary_trees, ordinary = gate.run(f'binding history reference {cut}',
                setup + advance(cut) + advance(1000) + state)
            for forget in (False, True):
                stop = '!(history:forget &held 100000)\n' if forget else ''
                trees, recorded = gate.run(f'binding history {cut} forget {forget}',
                    setup + advance(cut, recording) + stop +
                    advance(1000, '' if forget else '(history False)') + state)
                assert len(ordinary) == len(recorded) == 2, (ordinary, recorded)
                for left, right in zip(ordinary, recorded):
                    assert all(left[key] == right[key] for key in
                        ('values', 'work', 'satisfied', 'closed', 'status')), (left, right)
                assert trees[-1] == ordinary_trees[-1], (trees, ordinary_trees)
                if forget:
                    policies = list(interaction_records(trees, 'HistoryPolicy'))
                    assert policies and dict(policies[0][1:])['status'] == 'Unrecorded', trees
                    assert 'history' not in recorded[-1], recorded[-1]
                else:
                    flags = [field[1] for field in recorded[-1]['history'][1:]
                             if isinstance(field, list) and len(field) == 2 and field[0] == 'recording']
                    assert flags == ['False'], recorded[-1]
                gate.passed_case(f'chosen binding history cut {cut} forget {forget} preserves exact publication and paid work')


def selected_world_choices(gate, history_controls=False):
    rows = ['(Fact A)', '(Fact B)', '(Fact A)',
            '(exec Pick (I (take (Fact $x))) (O (+ (Seen $x))))']
    profile = '(Transaction worlds)'
    held = f'!(bind! &held (let {PATTERN} &selected $r))\n'
    state = '!(collapse (get-atoms &jobs))\n'
    trees, _ = gate.run('private choice discovery', source(rows) +
        interaction_discovery(profile=profile) + '!(let $menu &menu $menu)\n' + state)
    offered = dict(next(interaction_records(trees, 'InteractionChoices'))[1:])
    assert offered['status'] == 'Exhausted' and len(offered['choices']) == 3, offered
    assert [choice[5][1][0][2] for choice in offered['choices']] == [0, 1, 2], offered
    assert trees[-1][0] == parse(' '.join(rows)), trees
    gate.passed_case('private-world discovery exposes every physical tuple without copying or consuming the parent')
    for ticket, chosen, index in ((2, 'B', 1), (3, 'A', 2)):
        for cut in (0, 1, 7, 23, 1000):
            trees, results = gate.run(f'private binding {ticket} cut {cut}', source(rows) +
                interaction_discovery(profile=profile) +
                f'!(bind! &selected (interaction:perform &discovery {ticket} {cut}))\n'
                '!(let $selected &selected $selected)\n' + held +
                '!(interaction:dispose &discovery)\n!(interaction:perform &held 1000)\n'
                '!(interaction:perform &held 1000)\n' + state)
            assert len(results) == 3, results
            complete = [world for report in results for world in worlds(report)]
            assert len(complete) == 1, complete
            atoms, trace = complete[0]
            assert ['Seen', chosen] in atoms and len(atoms) == 3 and len(trace) == 1, complete
            assert quoted(dict(trace[0][2:])['dependencies'])[0][0][2] == index, trace
            assert results[1]['closed'] == 'True' and not results[2]['values'], results
            assert results[1]['work'] == results[2]['work'], results
            assert trees[-1][0] == parse(' '.join(rows)), trees
            if cut == 0:
                assert not results[0]['values'] and results[0]['work'] == 0, results
            gate.passed_case(f'private binding {ticket} at cut {cut} preserves the selected occurrence and live parent')
    trees, results = gate.run('separate private choices', source(rows) +
        interaction_discovery(profile=profile) +
        '!(bind! &left (interaction:perform &discovery 2 0))\n'
        '!(bind! &right (interaction:perform &discovery 3 0))\n'
        f'!(bind! &left-held (let {PATTERN} &left $r))\n'
        f'!(bind! &right-held (let {PATTERN} &right $r))\n'
        '!(interaction:dispose &discovery)\n!(interaction:perform &left-held 1000)\n'
        '!(interaction:perform &right-held 1000)\n' + state)
    assert len(results) == 2 and all(report['closed'] == 'True' for report in results), results
    assert [['Seen', value] in worlds(report)[0][0] for value, report in zip(('B', 'A'), results)] == [True, True], results
    assert trees[-1][0] == parse(' '.join(rows)), trees
    gate.passed_case('two transferred choices own separate private worlds and never duplicate a live consumption')
    immediate = ['(exec Now (I) (O (+ Done)))']
    trees, results = gate.run('private zero-input choice', source(immediate) +
        interaction_discovery(profile=profile) + '!(interaction:perform &discovery 1 1000)\n' + state)
    assert len(results) == 1 and worlds(results[0])[0][0] == ['Done'], results
    assert trees[-1][0] == parse(' '.join(immediate)), trees
    gate.passed_case('a zero-input private choice retains its completed matcher and original live directive')
    trees, results = gate.run('private publication breakpoint', source(rows) +
        interaction_discovery(profile=profile) +
        '!(bind! &selected (interaction:perform &discovery 2 0))\n' + held +
        f'!(select 1 (search-policy (control FIFO (resources {profile}) '
        '(budget 1000) (breakpoint (exec Pick $inputs $outputs)))) &held)\n' + state +
        f'!(select 1 (search-policy (control FIFO (resources {profile}) '
        '(budget 1000) (breakpoint False))) &held)\n' + state)
    assert len(results) == 2 and results[0]['status'] == 'breakpoint', results
    assert not results[0]['values'] and dict(results[0]['agenda'][1:])['commits'] == 0, results
    assert worlds(results[1])[0][0][-1] == ['Seen', 'B'], results
    assert trees[-3][0] == trees[-1][0] == parse(' '.join(rows)), trees
    gate.passed_case('a private publication breakpoint retains its exact owner and resumes without touching the parent')
    trees, results = gate.run('selected live publication breakpoint', source(rows) +
        interaction_discovery(profile='(Transaction matches)') +
        '!(bind! &selected (interaction:perform &discovery 2 0))\n' + held +
        '!(select 1 (search-policy (control FIFO (resources Transaction) '
        '(budget 1000) (breakpoint (exec Pick $inputs $outputs)))) &held)\n'
        '!(select 1 (search-policy (control FIFO (resources Transaction) '
        '(budget 1000) (breakpoint False))) &held)\n' + state)
    assert len(results) == 2 and results[0]['status'] == 'breakpoint', results
    assert not results[0]['values'] and results[1]['values'][0][0] == 'Committed', results
    assert trees[-1][0] == [['Fact', 'A'], ['Fact', 'A'], ['Seen', 'B']], trees
    gate.passed_case('arming a later breakpoint also authenticates the existing selected live invocation')
    for cut in (0, 7, 23):
        trees, results = gate.run(f'private source edit cut {cut}', source(rows) +
            interaction_discovery(profile=profile) +
            '!(bind! &selected (interaction:perform &discovery 2 0))\n' + held +
            f'!(interaction:perform &held {cut})\n!(add-atom &jobs Unrelated)\n'
            '!(interaction:perform &held 1000)\n' + state)
        assert all(not report['values'] and report['closed'] == 'False' for report in results), results
        assert trees[-1][0] == parse(' '.join(rows)) + ['Unrelated'], trees
        gate.passed_case(f'exact snapshot source edit at cut {cut} refuses without changing the live parent')
    created = ['Token', '(Fact A)', '(Fact B)',
        '(exec Start (I (take Token)) (O (+ (exec Next (I (take (Fact $x))) (O (+ (Done $x)))))))']
    _, reference = gate.run('ordinary created-world reference', source(created) +
        f'!(select 10 {policy(profile)} &jobs)\n')
    expected = world_observations(reference[-1])
    for workers in (1,):
        trees, results = gate.run(f'chosen world created rule workers {workers}', source(created) +
            interaction_discovery(profile=profile) +
            '!(bind! &selected (interaction:perform &discovery 1 0))\n' + held +
            '!(interaction:dispose &discovery)\n'
            f'!(select 10 (search-policy (control FIFO (resources {profile}) '
            f'(workers {workers}) (quantum 3) (budget 10000))) &held)\n' + state)
        assert len(results) == 1 and results[0]['closed'] == 'True', results
        assert world_observations(results[0]) == expected, (results, reference)
        assert trees[-1][0] == parse(' '.join(created)), trees
        gate.passed_case(f'chosen world rejoins ordinary created-rule branching with {workers} workers')
    for workers in (2, 4):
        trees, results = gate.run(f'chosen world retains serial owner workers {workers}', source(created) +
            interaction_discovery(profile=profile) +
            '!(bind! &selected (interaction:perform &discovery 1 0))\n' + held +
            f'!(select 10 (search-policy (control FIFO (resources {profile}) '
            f'(workers {workers}) (budget 10000))) &held)\n' + state +
            f'!(select 10 {policy(profile)} &held)\n' + state)
        assert len(results) == 1 and results[0]['closed'] == 'True', results
        assert world_observations(results[0]) == expected, (results, reference)
        assert trees[-3][0] == trees[-1][0] == parse(' '.join(created)), trees
        gate.passed_case(f'{workers} workers leave the selected serial owner available for supported resumption')
    trees, results = gate.run('private choice current authority', source(rows) +
        interaction_discovery(profile=profile) +
        '!(select 1 (search-policy (control WO (budget 10000) '
        '(key $candidate (interaction:perform &discovery 2 1000)))) (superpose (1 2)))\n'
        '!(interaction:perform (NativeHandle "prime-space-discovery" 1) 2 1000)\n' + state +
        '!(interaction:perform &discovery 2 1000)\n' + state)
    assert trees[-3][0] == trees[-1][0] == parse(' '.join(rows)), trees
    assert len(worlds(results[-1])) == 1, results
    assert all(not any(isinstance(value, list) and value and value[0] == 'SnapshotWorld'
                       for value in report['values']) for report in results[:-1]), results
    gate.passed_case('private evidence and printed selectors grant no permission to advance under a pure caller')
    lexical = (f'!(let {INTERACTION_PAGE_PATTERN} (interaction:choices &jobs {profile} 1000) '
        f'(let {PATTERN} (interaction:perform $residual 2 1000) '
        '(PrivateWorldPort (size-atom $choices) (size-atom $v) $c '
        '(collapse (match &jobs (Fact $x) $x)))))')
    for language in ('prime', 'he', 'petta'):
        gate.run(f'{language} extended private choice', source(rows) + lexical,
                 language=language, profile=None if language == 'prime' else 'extended')
        ports = list(interaction_records(parse(gate.receipts[-1]['stdout']), 'PrivateWorldPort'))
        assert ports == [['PrivateWorldPort', 3, 1, 'True', ['A', 'B', 'A']]], ports
        gate.passed_case(f'{language} private choice uses the common extended interface')
    for language in ('he', 'petta'):
        trees, _ = gate.run(f'{language} default private choice', source(rows) +
            f'!(interaction:choices &jobs {profile} 1000)\n' + state, language=language)
        assert not list(interaction_records(trees, 'InteractionChoices')), trees
        live = parse(gate.receipts[-1]['stdout'])[-1] if language == 'petta' else trees[-1][0]
        variable = live[-1][2][1][1][1]
        assert isinstance(variable, str) and variable.startswith('$'), live
        assert live == parse(' '.join(rows).replace('$x', variable)), live
        gate.passed_case(f'{language} default preserves its ordinary dialect without acquiring private-world choices')
    if history_controls:
        setup = (source(rows) + interaction_discovery(profile=profile) +
            '!(bind! &selected (interaction:perform &discovery 2 0))\n' + held +
            '!(interaction:dispose &discovery)\n')
        def advance(allowance, observer=''):
            return (f'!(select 1 (search-policy (control FIFO (resources {profile}) '
                    f'(budget {allowance}) {observer})) &held)\n')
        recording = '(history (selections 2048) (nodes 32) (bytes 65536) (owners 10000))'
        for cut in (0, 7, 23):
            ordinary_trees, ordinary = gate.run(f'private history reference {cut}',
                setup + advance(cut) + advance(1000) + state)
            for forget in (False, True):
                stop = '!(history:forget &held 100000)\n' if forget else ''
                trees, recorded = gate.run(f'private history {cut} forget {forget}',
                    setup + advance(cut, recording) + stop +
                    advance(1000, '' if forget else '(history False)') + state)
                assert len(ordinary) == len(recorded) == 2, (ordinary, recorded)
                for left, right in zip(ordinary, recorded):
                    assert all(left[key] == right[key] for key in
                        ('values', 'work', 'satisfied', 'closed', 'status')), (left, right)
                assert trees[-1] == ordinary_trees[-1], (trees, ordinary_trees)
                if forget:
                    assert 'history' not in recorded[-1], recorded[-1]
                else:
                    flags = [field[1] for field in recorded[-1]['history'][1:]
                             if isinstance(field, list) and len(field) == 2 and field[0] == 'recording']
                    assert flags == ['False'], recorded[-1]
                gate.passed_case(f'private world history cut {cut} forget {forget} preserves the answer and paid work')


def resume_choices(gate, history_controls=False):
    plain = '(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'

    def setup(cut=4, expression='(superpose (A B B))', workers=1, kind=None):
        resources = '' if kind is None else f'(resources {kind}) '
        pattern = plain if kind is None else PATTERN
        return (f'!(bind! &start (select 100 (search-policy (control FIFO {resources}'
                f'(workers {workers}) (quantum 3) (budget {cut}))) {expression}))\n'
                f'!(bind! &held (let {pattern} &start $r))\n'
                '!(let $initial &start $initial)\n')

    def discover(allowance=100):
        return (f'!(bind! &menu (interaction:choices &held Resume {allowance}))\n'
                '!(let $page &menu $page)\n'
                f'!(bind! &cursor (let {INTERACTION_PAGE_PATTERN} &menu $residual))\n')

    def pages(trees):
        return [dict(record[1:]) for record in interaction_records(trees, 'InteractionChoices')]

    trees, _ = gate.run('Resume discovery reference', setup() + discover())
    reference = pages(trees)[0]
    assert [row[0] for row in reference['choices']] == ['ResumeChoice'] * 2, reference
    assert [dict(row[2:])['operation'] for row in reference['choices']] == ['Call', 'Return'], reference
    assert reference['quanta'] == 2 and reference['status'] == 'Exhausted', reference
    first, second = reference['choices']
    gate.passed_case('Resume descriptors name the actual retained occurrences, rather than computed successor answers')
    for cut in (0, 1, 2, 10):
        trees, results = gate.run(f'Resume page split {cut}', setup() + discover(cut) +
            '!(interaction:choices &cursor 100)\n!(interaction:choices &cursor 0)\n')
        reports = pages(trees)
        assert reports[0]['choices'] + reports[1]['choices'] == reference['choices'], reports
        assert reports[-1]['choices'] == [] and reports[-1]['quanta'] == 2, reports
        assert results[0]['work'] == 4 and results[0]['values'] == [], results
        gate.passed_case(f'Resume inspection split {cut} preserves identity and performs no evaluator work')

    trees, results = gate.run('Resume selected second operation', setup() + discover() +
        '!(interaction:perform &cursor 2 0)\n!(interaction:perform &cursor 1 100)\n'
        '!(interaction:choices &cursor 100)\n!(interaction:perform &held 1)\n'
        '!(bind! &next-menu (interaction:choices &held Resume 100))\n'
        '!(let $page &next-menu $page)\n'
        f'!(bind! &next (let {INTERACTION_PAGE_PATTERN} &next-menu $residual))\n'
        '!(interaction:perform &next 2 1)\n'
        '!(interaction:perform &held 10000)\n'
        '!(select 100 (search-policy (control FIFO (budget 10000))) &held)\n')
    reports = pages(trees)
    assert [r['work'] for r in results[:3]] == [4, 4, 5], results
    assert reports[1]['choices'] == [] and reports[1]['pending'] == 'Selected', reports
    assert reports[2]['choices'][0] == first, reports
    assert [dict(row[2:])['source'] for row in reports[2]['choices'][:2]] == ['A', 'B'], reports
    second_occurrence = dict(second[2:])['occurrence']
    assert all(dict(row[2:])['parent'] == second_occurrence for row in reports[2]['choices'][1:]), reports
    assert results[-2]['values'] == ['B'], results
    assert bag(results[-2]['values'] + results[-1]['values']) == bag(['A', 'B', 'B']) and results[-1]['closed'] == 'True', results
    assert all('history' not in result for result in results), results
    gate.passed_case('program chooses a return, then the offered B call; A and the duplicate B both survive on the same controller')

    for cut in (0, 1, 3, 7, 19):
        trees, results = gate.run(f'Resume selected split {cut}', setup() + discover() +
            f'!(interaction:perform &cursor 2 {cut})\n'
            '!(interaction:dispose &cursor)\n'
            '!(select 100 (search-policy (control FIFO (budget 10000))) &held)\n')
        assert bag(results[-2]['values'] + results[-1]['values']) == bag(['A', 'B', 'B']), results
        assert results[-1]['closed'] == 'True', results
        if cut == 0:
            assert results[-2]['work'] == 4 and not results[-2]['values'], results
        gate.passed_case(f'Resume take cut {cut} retains the complete selected computation and all sibling answers')

    trees, results = gate.run('Resume unseen, competing, forged and disposed owners', setup() + discover(1) +
        '!(interaction:perform &cursor 2 100)\n'
        '!(select 100 (search-policy (control FIFO (budget 10000))) &held)\n'
        '!(interaction:choices &held Resume 100)\n'
        '!(interaction:perform (NativeHandle "prime-space-discovery" 2) 1 100)\n'
        '!(interaction:choices &cursor 100)\n!(interaction:dispose &cursor)\n'
        '!(interaction:perform &cursor 1 100)\n'
        '!(select 100 (search-policy (control FIFO (budget 10000))) &held)\n')
    assert len(results) == 2 and results[0]['work'] == 4, results
    assert bag(results[-1]['values']) == bag(['A', 'B', 'B']) and results[-1]['closed'] == 'True', results
    assert len(pages(trees)) == 2 and pages(trees)[-1]['choices'] == [second], trees
    gate.passed_case('exclusive paging refuses another resume, unseen or printed tickets; disposing only the cursor restores ordinary control')

    trees, results = gate.run('Resume source invalidation', setup() + discover() +
        '!(add-atom &self Outside)\n!(interaction:choices &cursor 100)\n'
        '!(interaction:perform &cursor 2 100)\n!(interaction:dispose &cursor)\n'
        '!(select 100 (search-policy (control FIFO (budget 10000))) &held)\n')
    assert pages(trees)[-1]['status'] == 'Invalidated', trees
    assert len(results) == 2 and results[-1]['work'] == 4 and not results[-1]['values'], results
    gate.passed_case('Resume revalidates its actual source world before accepting the inspected intent')

    scorer = ('!(select 1 (search-policy (control WO (budget 10000) '
              '(key $candidate {action}))) (superpose (1 2)))\n')
    for action, opened in (('(interaction:choices &held Resume 1)', False),
                           ('(interaction:perform &cursor 2 1000)', True)):
        trees, results = gate.run(f'Resume current pure authority {opened}', setup() +
            (discover() if opened else '') + scorer.format(action=action) +
            ('!(interaction:choices &cursor 100)\n!(interaction:dispose &cursor)\n' if opened else '') +
            '!(select 100 (search-policy (control FIFO (budget 10000))) &held)\n')
        reports = pages(trees)
        assert (reports[-1]['choices'] == [] and reports[-1]['pending'] == 'None'
                if opened else not reports), trees
        assert bag(results[-1]['values']) == bag(['A', 'B', 'B']) and results[-1]['closed'] == 'True', results
        gate.passed_case(f'pure scoring cannot acquire or advance an owned Resume cursor; existing cursor {opened}')

    for kind in ('Transaction', 'MM2'):
        rows = ['Token', '(exec Start (I (take Token)) (O (+ Done)))'] if kind == 'Transaction' else [
            'Token', '(exec Start (, Token) (O (+ Done) (- Token)))']
        _, ordinary = gate.run(f'{kind} Resume reference', source(rows) +
            f'!(select 100 {policy(kind)} &jobs)\n')
        for workers in (1, 2, 4):
            trees, results = gate.run(f'{kind} Resume workers {workers}', source(rows) +
                setup(0, '&jobs', workers, kind) + discover(1) +
                '!(interaction:perform &cursor 1 0)\n!(interaction:dispose &cursor)\n'
                f'!(select 100 (search-policy (control FIFO (resources {kind}) '
                f'(workers {workers}) (quantum 3) (budget 10000))) &held)\n'
                '!(collapse (get-atoms &jobs))')
            assert bag(results[-1]['values']) == bag(ordinary[-1]['values']), (results, ordinary)
            assert results[-1]['closed'] == 'True' and 'Token' not in trees[-1][0] and 'Done' in trees[-1][0], trees
            gate.passed_case(f'{kind} Resume rejoins existing coordinated effects and {workers}-worker controller')

    for language in ('prime', 'he', 'petta'):
        pattern = plain
        initial = ('(Observation $initial-values (satisfied $initial-s) (closed $initial-c) '
                   '(residual $held) (work $initial-w) (status $initial-t))')
        lexical = (f'!(let {initial} (select 100 (search-policy (control FIFO (budget 0))) '
            '(superpose (A B B))) '
            f'(let {INTERACTION_PAGE_PATTERN} (interaction:choices $held Resume 1) '
            '(let $_ (interaction:perform $residual 1 0) '
            f'(let {pattern} (select 100 (search-policy (control FIFO (budget 10000))) $held) '
            '(ResumePort $v $c)))))')
        trees, _ = gate.run(f'{language} Resume lexical port', lexical, language=language,
            profile=None if language == 'prime' else 'extended')
        ports = list(interaction_records(parse(gate.receipts[-1]['stdout']), 'ResumePort'))
        assert ports == [['ResumePort', ['A', 'B', 'B'], 'True']], trees
        gate.passed_case(f'{language} extended exposes the same owned Resume service')

    if history_controls:
        _, reference = gate.run('Resume unrecorded history reference', setup() + discover() +
            '!(interaction:perform &cursor 2 1)\n'
            '!(select 100 (search-policy (control FIFO (budget 10000))) &held)\n')
        recording = '(history (selections 4096) (nodes 64) (bytes 65536) (owners 20000))'
        for forget in (False, True):
            arm = f'!(select 100 (search-policy (control FIFO (budget 0) {recording})) &held)\n'
            stop = '!(history:forget &held 100000)\n' if forget else ''
            trees, results = gate.run(f'Resume optional history forget {forget}', setup() + arm +
                discover() + '!(interaction:perform &cursor 2 1)\n' + stop +
                f'!(select 100 (search-policy (control FIFO (budget 10000) {"" if forget else recording})) &held)\n')
            assert results[-1]['closed'] == 'True' and bag(results[-1]['values']) == bag(['A', 'B', 'B']), results
            assert ('history' in results[-1]) != forget, results
            for actual, expected in zip(results[-2:], reference[-2:]):
                assert all(actual[key] == expected[key] for key in
                           ('values', 'work', 'satisfied', 'closed', 'status')), (actual, expected)
            gate.passed_case(f'Resume uses existing requested history and explicit forget {forget}')
        exhausted = '(history (selections 0) (nodes 0) (bytes 0) (owners 0))'
        _, results = gate.run('Resume exhausted optional recorder', setup() +
            f'!(select 100 (search-policy (control FIFO (budget 0) {exhausted})) &held)\n' +
            discover() + '!(interaction:perform &cursor 2 1)\n'
            f'!(select 100 (search-policy (control FIFO (budget 10000) {exhausted})) &held)\n')
        assert bag(results[-1]['values']) == bag(['A', 'B', 'B']) and results[-1]['closed'] == 'True', results
        for actual, expected in zip(results[-2:], reference[-2:]):
            assert all(actual[key] == expected[key] for key in
                       ('values', 'work', 'satisfied', 'closed', 'status')), (actual, expected)
        gate.passed_case('exhausting the explicitly requested recorder preserves the selected Resume and ordinary remainder')


def continuing_server(gate, history_controls=False):
    from test_prime_native_cost import compact_receipt
    client = ROOT / 'examples/prime/native_control/responsive_monitor.metta'
    authored = client.read_text()
    start = authored.index('!(bind! &server-jobs')
    end = authored.index('!(let (Observation', start)
    prefix = authored[start:end].replace('&server-jobs', '&jobs')

    accounted_pattern = PATTERN[:-1] + ' (cost $cost))'

    def control(budget=10000, workers=1, observer='', accounted=False):
        cost = '(costs native-counts-v1)' if accounted else ''
        return ('(search-policy (control FIFO (resources Transaction) '
                f'(demand additional) (workers {workers}) (budget {budget}) {observer} {cost}))')

    def setup(workers=1, accounted=False):
        pattern = accounted_pattern if accounted else PATTERN
        return (prefix + f'!(bind! &initial (select 100 {control(workers=workers, accounted=accounted)} &jobs))\n'
                f'!(bind! &held (let {pattern} &initial $r))\n'
                '!(let $initial &initial $initial)\n')

    def advance(budget=10000, workers=1, observer='', accounted=False):
        return f'!(select 100 {control(budget, workers, observer, accounted)} &held)\n'

    def accounts(report):
        counts = compact_receipt(report['cost'])['counts']
        assert report['work'] == counts['quantum'], (report, counts)
        assert counts['publish'] == dict(report['agenda'][1:])['commits'], (report, counts)
        return counts

    def source_counts(report):
        counts = accounts(report)
        return {key: counts[key] for key in ('candidate-row', 'match-attempt', 'match-node',
                'binding-clone', 'analysis-step', 'publish', 'commit-check')}

    def add(row):
        return f'!(add-atom &jobs {row})\n'

    def states(trees):
        return [row[1] for row in interaction_records(trees, 'State')]

    def rows(state, head):
        return [row for row in state if isinstance(row, list) and row and row[0] == head]

    def inputs(commits, role, message):
        found = []
        for commit in commits:
            if quoted(commit[1])[1] != role:
                continue
            dependencies = quoted(dict(commit[2:])['dependencies'])
            assert len(dependencies) == 1, dependencies
            chosen = [dep[3] for dep in dependencies[0] if dep[0] == 'take' and
                      isinstance(dep[3], list) and dep[3][0] == message]
            assert len(chosen) == 1, dependencies
            found.extend(chosen)
        return found

    state = '!(State (collapse (get-atoms &jobs)))\n'
    equal = '(Request Equal Same)'
    for width in (1, 2, 3):
        trees, reports = gate.run(f'continuing request reply workers {width}', setup(width) +
            add(equal) * 2 + advance(workers=width) + state +
            advance(workers=width) + state +
            add('Acknowledge') + advance(workers=width) + state +
            add('(Request Later Same)') + add('Acknowledge') + advance(workers=width) + state +
            add('Acknowledge') + advance(workers=width) + state)
        assert [len(report['values']) for report in reports] == [0, 1, 0, 2, 2, 1], reports
        assert all(report['closed'] == 'False' and report['status'] == 'suspended' and
                   'history' not in report for report in reports), reports
        assert len({json.dumps(report['residual']) for report in reports}) == 1, reports
        assert all(left['work'] <= right['work'] for left, right in zip(reports, reports[1:])), reports
        snapshots = states(trees)
        assert len(snapshots) == 5 and snapshots[0] == snapshots[1], snapshots
        assert rows(snapshots[0], 'Request') == [['Request', 'Equal', 'Same']], snapshots
        assert rows(snapshots[0], 'Reply') == [['Reply', 'Equal', 'Same']], snapshots
        assert 'Slot' not in snapshots[0] and not rows(snapshots[0], 'Delivered'), snapshots
        assert rows(snapshots[2], 'Delivered') == [['Delivered', 'Equal', 'Same']], snapshots
        assert rows(snapshots[2], 'Reply') == [['Reply', 'Equal', 'Same']], snapshots
        assert rows(snapshots[3], 'Delivered') == [['Delivered', 'Equal', 'Same']] * 2, snapshots
        assert rows(snapshots[3], 'Reply') == [['Reply', 'Later', 'Same']], snapshots
        assert rows(snapshots[4], 'Delivered') == [
            ['Delivered', 'Equal', 'Same'], ['Delivered', 'Equal', 'Same'],
            ['Delivered', 'Later', 'Same']], snapshots
        assert snapshots[4].count('Slot') == 1 and 'Acknowledge' not in snapshots[4], snapshots
        assert not rows(snapshots[4], 'Request') and not rows(snapshots[4], 'Reply'), snapshots
        commits = [value for report in reports for value in report['values']]
        assert [quoted(value[1])[1] for value in commits] == ['Server', 'Consumer'] * 3, commits
        requests = inputs(commits, 'Server', 'Request')
        assert requests == [['Request', 'Equal', 'Same']] * 2 + [['Request', 'Later', 'Same']], requests
        assert [row[1:] for row in inputs(commits, 'Consumer', 'Reply')] == [row[1:] for row in requests]
        gate.passed_case(f'continuing server workers {width}: same owner, duplicate occurrences, ordered late arrivals and ack backpressure')

    complete = setup(accounted=True) + add(equal) * 2 + add('Acknowledge') * 2
    trees, reports = gate.run('continuing server complete fixed arrivals',
        complete + advance(accounted=True) + state)
    baseline, final = reports[-1], states(trees)[-1]
    assert len(baseline['values']) == 4, reports
    initial_counts = accounts(reports[0])
    expected_counts = source_counts(baseline)
    assert expected_counts['publish'] == 4 and expected_counts['commit-check'] == 8, expected_counts
    for cut in (0, 1, 7, 19, 53, 101):
        trees, reports = gate.run(f'continuing server fixed arrivals cut {cut}', complete +
            f'!(let {accounted_pattern} (select 100 {control(cut, accounted=True)} &held) '
            f'(Split $v $w $cost (select 100 {control(10000-cut, accounted=True)} $r)))\n' + state)
        split = next(interaction_records(trees, 'Split'))
        actual = {**reports[-1], 'values': split[1] + reports[-1]['values']}
        prefix_counts = compact_receipt(split[3])['counts']
        assert split[2] == prefix_counts['quantum'], (cut, split)
        assert 0 <= split[2] - reports[0]['work'] <= cut, (cut, split, reports)
        assert 0 <= actual['work'] - split[2] <= 10000-cut, (cut, actual, split)
        assert source_counts(actual) == expected_counts, (cut, actual, baseline)
        if cut == 0:
            before = accounts(reports[0])
            assert before == initial_counts, (split, reports)
            # Returning the initial account itself has its declared price.
            # A zero execution grant adds exactly that one observation, and
            # performs no matcher, provider or scheduler work.
            expected_zero = {**before, 'cost-read': before['cost-read'] + 1}
            assert prefix_counts == expected_zero, (split, reports)
        assert all(actual[field] == baseline[field] for field in
                   ('satisfied', 'closed', 'status')), (cut, actual, baseline)
        assert fresh_frame_syntax([actual['values'], states(trees)[-1]]) == \
               fresh_frame_syntax([baseline['values'], final]), (cut, actual, baseline)
        # Actual probing is retained and metered, but its bucket collisions
        # depend on native addresses. Exact suffix conservation on the same
        # retained cursor is checked by test_native_space_agenda.c.
        gate.passed_case(f'continuing server cut {cut} conserves source operations and exact per-run quantum accounts, commits and receive bodies')

    for cut in (0, 7, 31, 47, 101):
        trees, reports = gate.run(f'continuing server arrival during paid prefix {cut}', setup() +
            add('(Request Earlier A)') + advance(cut) +
            add('(Request Later B)') + add('Acknowledge') * 2 + advance() + state)
        commits = reports[1]['values'] + reports[2]['values']
        assert inputs(commits, 'Server', 'Request') == [
            ['Request', 'Earlier', 'A'], ['Request', 'Later', 'B']], reports
        assert rows(states(trees)[-1], 'Delivered') == [
            ['Delivered', 'Earlier', 'A'], ['Delivered', 'Later', 'B']], trees
        assert all(report['closed'] == 'False' and report['status'] == 'suspended'
                   for report in reports), reports
        assert reports[-1]['work'] >= reports[-2]['work'] >= reports[0]['work'], reports
        gate.passed_case(f'late arrival at cut {cut} retains older request and paid work through current-world validation')

    trees, reports = gate.run('continuing server missing capacity', setup().replace(
        add('Slot'), '') + add(equal) + add('Acknowledge') + advance() + state)
    assert all(report['values'] == [] and report['closed'] == 'False' for report in reports), reports
    assert rows(states(trees)[-1], 'Request') == [['Request', 'Equal', 'Same']], trees
    assert not rows(states(trees)[-1], 'Reply') and not rows(states(trees)[-1], 'Delivered'), trees
    gate.passed_case('a queued request and acknowledgment cannot manufacture missing reply capacity')

    trees, reports = gate.run('continuing server zero grants', complete + advance(0) + advance(0) + state)
    assert len(reports) == 3 and all(report['values'] == [] for report in reports), reports
    assert reports[0]['work'] == reports[1]['work'] == reports[2]['work'], reports
    assert len(rows(states(trees)[-1], 'Request')) == 2, trees
    gate.passed_case('eligible requests get no service promise without an actual admitted advancement grant')

    gate.run('HE extended continuing server', authored[start:], language='he', profile='extended')
    expected = client.with_suffix('.expected').read_text()
    expected = expected[expected.index('[()]'):]
    assert gate.receipts[-1]['stdout'] == expected and not gate.receipts[-1]['stderr'], gate.receipts[-1]
    gate.passed_case('HE extended runs the maintained request/reply client with identical observations')

    # PeTTa binds named states through new-state/get-state. Keep this port's
    # execution owner lexically instead of importing another dialect's bind!.
    def view(index):
        return (f'(Observation $v{index} (satisfied $s{index}) (closed $c{index}) '
                f'(residual $r{index}) (work $w{index}) (status $t{index}) (agenda $a{index}))')

    def arrivals(items, body):
        for index, row in reversed(list(enumerate(items))):
            body = f'(chain (add-atom &jobs {row}) $ignored{index} {body})'
        return body

    delivered = '(collapse (match &jobs (Delivered $id $payload) (Delivered $id $payload)))'
    reply = '(collapse (match &jobs (Reply $id $payload) (Reply $id $payload)))'
    body = (f'(let $n3 (size-atom $v3) (let $delivered {delivered} '
            '(ServerPort $n1 $n2 $n3 $buffer $delivered '
            '$t0 $t1 $t2 $t3 $c0 $c1 $c2 $c3)))')
    body = f'(let {view(3)} (select 100 {control()} $r2) {body})'
    body = arrivals(['(Request Later Same)', 'Acknowledge'], body)
    body = f'(let $n2 (size-atom $v2) {body})'
    body = f'(let {view(2)} (select 100 {control()} $r1) {body})'
    body = arrivals(['Acknowledge', 'Acknowledge'], body)
    body = f'(let $n1 (size-atom $v1) (let $buffer {reply} {body}))'
    body = f'(let {view(1)} (select 100 {control()} $r0) {body})'
    body = arrivals([equal, equal], body)
    gate.run('PeTTa extended lexical continuing server', prefix +
             f'!(let {view(0)} (select 100 {control()} &jobs) {body})',
             language='petta', profile='extended')
    ports = list(interaction_records(parse(gate.receipts[-1]['stdout']), 'ServerPort'))
    assert ports == [['ServerPort', 1, 3, 2, [['Reply', 'Equal', 'Same']],
                      [['Delivered', 'Equal', 'Same'], ['Delivered', 'Equal', 'Same'],
                       ['Delivered', 'Later', 'Same']]] + ['suspended'] * 4 + ['False'] * 4], ports
    assert not gate.receipts[-1]['stderr'], gate.receipts[-1]
    gate.passed_case('PeTTa extended carries the same waiting owner lexically through backpressure, duplicate requests and later bursts')

    if history_controls:
        recording = '(history (selections 2048) (nodes 32) (bytes 65536) (owners 10000))'
        for cut in (0, 19, 71):
            trees, ordinary = gate.run(f'continuing server history reference {cut}',
                complete + advance(cut, accounted=True) + advance(10000-cut, accounted=True) + state)
            expected = states(trees)[-1]
            for forget in (False, True):
                stop = ('!(history:forget &held 100000)\n' if forget else '')
                trees, recorded = gate.run(f'continuing server history {cut} forget {forget}',
                    complete + advance(cut, observer=recording, accounted=True) + stop +
                    advance(10000-cut, observer='' if forget else '(history False)', accounted=True) + state)
                assert len(recorded) == len(ordinary) == 3, recorded
                for left, right in zip(ordinary, recorded):
                    accounts(left); accounts(right)
                    assert all(left[key] == right[key] for key in
                               ('satisfied', 'closed', 'status')), (left, right)
                assert source_counts(ordinary[-1]) == source_counts(recorded[-1]), (ordinary, recorded)
                assert fresh_frame_syntax([[r['values'] for r in recorded], states(trees)[-1]]) == \
                       fresh_frame_syntax([[r['values'] for r in ordinary], expected]), (ordinary, recorded)
                if forget:
                    policies = list(interaction_records(trees, 'HistoryPolicy'))
                    assert policies and dict(policies[0][1:])['status'] == 'Unrecorded', trees
                    assert 'history' not in recorded[-1], recorded[-1]
                else:
                    flags = [field[1] for field in recorded[-1]['history'][1:]
                             if isinstance(field, list) and len(field) == 2 and field[0] == 'recording']
                    assert flags == ['False'], recorded[-1]
                gate.passed_case(f'continuing server history cut {cut} forget {forget} preserves reply protocol and exact paid work')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,default=ROOT/'cetta')
    parser.add_argument('--timeout',type=int,default=30)
    parser.add_argument('--cases',type=int,default=5)
    parser.add_argument('--receipt',type=Path)
    parser.add_argument('--population', choices=('all', 'choices', 'server', 'state-world'), default='all')
    parser.add_argument('--history-controls', action='store_true',
                        help='Check optional recording, stopping and forgetting on selected bindings and the continuing server.')
    parser.add_argument('--resource-checker',type=Path,
                        help='Optional executable for the resource-word source model')
    args=parser.parse_args();args.binary=args.binary.resolve();gate=Gate(args)
    try:
        if args.population == 'state-world':
            state_world_diagnostic(gate)
        elif args.population != 'server':
            semantic_choices(gate)
            raw_semantic_choices(gate)
            matching_choices(gate, args.history_controls)
            selected_world_choices(gate, args.history_controls)
            resume_choices(gate, args.history_controls)
        if args.population in ('all', 'server'):
            continuing_server(gate, args.history_controls)
        if args.population == 'all':
            fixed(gate);kept_read_identification(gate);split(gate,args.cases);priorities(gate);current_scope(gate);generated(gate,args.cases)
            mm2_correspondence(gate, args.cases)
            pending_resumption(gate)
            causal_world_readout(gate, args.resource_checker)
            snapshot_worlds(gate,args.cases)
            physical_matches(gate)
    finally:
        if args.receipt:
            args.receipt.parent.mkdir(parents=True,exist_ok=True)
            receipt = {'binary_sha256':sha256(args.binary.read_bytes()).hexdigest(),
                       'passed':gate.passed,'receipts':gate.receipts}
            if args.resource_checker is not None:
                receipt['resource_checker_sha256'] = sha256(args.resource_checker.read_bytes()).hexdigest()
            args.receipt.write_text(json.dumps(receipt,indent=2)+'\n')
    if args.population == 'state-world':
        print(f'Prime native resources: {gate.passed} plumbing checks; comparison policy not qualified')
    else:
        print(f'Prime native resources: {gate.passed} passed, 0 failed')


if __name__=='__main__':main()
