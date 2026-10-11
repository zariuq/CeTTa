#!/usr/bin/env python3
"""Executable algebra profiles and nonconsuming native-order refusal controls."""
from __future__ import annotations
from argparse import ArgumentParser
from fractions import Fraction
from hashlib import sha256
from pathlib import Path
import json
import re

from test_prime_native_control import Gate, ROOT, bag, observations
from test_prime_weights import authored_fixture, contribution, control, read, weight_observations, weight_order_record

def normalized(value):
    if isinstance(value,list):return [normalized(v) for v in value]
    if value in ('true','false'):return value.title()
    if isinstance(value,str) and re.fullmatch(r'-?[0-9]+[r/][1-9][0-9]*',value):
        return str(Fraction(value.replace('r','/')))
    return value

P='(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))'
PO='(Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t) $order)'
PREFIX=(f'!(import! &self {ROOT}/lib/native_weight_algebras.metta)\n'
        '!(pragma! prefer-rationals True)\n')
REAL=(f'!(import! &self {ROOT}/lib/probability/finite_bayes.metta)\n'
      f'!(import! &self {ROOT}/lib/native_weight_reals.metta)\n')

class FamilyGate:
    def __init__(self, gate, width=1):
        self.gate=gate
        self.language=getattr(gate, 'language', 'prime')
        self.width=width
    def run(self, name, source):
        directory=getattr(self.gate.args, 'program_dir', None)
        if directory is None:
            return self.gate.run(name, source)
        index=getattr(self.gate, '_family_program_index', 0)
        self.gate._family_program_index=index+1
        path=directory/f'{self.language}-{index:04d}.metta'
        path.write_text(source+'\n')
        return self.gate.run_file(name, path)
    def effects(self, tag):
        return self.gate.receipts[-1]['stdout'].splitlines().count(tag)
    def check(self, name):
        self.gate.passed_case(name)
    def control(self, budget=10000, mode='FIFO', extra=''):
        return control(budget,mode,extra+f' (workers {self.width})')

def boolean_counts(g):
    source='(= (coin) 2) (= (coin) 2) (= (coin) 5) (= (paired $x) (Pair $x $x))\n'
    source+='(= (rows) (let $_ (println! G1-BOOL-EFFECT) (paired (coin))))\n'
    unwrapped,_=g.run('ordinary host without weighted/control wrapper',PREFIX+source+'!(rows)')
    expected=[['Pair',2,2],['Pair',2,2],['Pair',5,5]]
    prefix_results=1 if g.language=='petta' else 2
    assert unwrapped[:prefix_results]==[[[]]]*prefix_results,unwrapped
    ordinary=[v for result in unwrapped[prefix_results:] for v in result]
    assert ordinary==expected and g.effects('G1-BOOL-EFFECT')==1,unwrapped
    g.check('unwrapped ordinary host preserves shared duplicate values and one effect')
    for width in (1,2):
        ctl=control(20000,extra=f'(workers {width}) (demand all)')
        _,plain=g.run('unannotated host rows',PREFIX+source+f'!(select 0 {ctl} (rows))')
        effects=g.effects('G1-BOOL-EFFECT')
        _,graded=g.run('neutral Boolean embedding',PREFIX+source+f'!(select 0 {ctl} '+read('(rows)','(weights:boolean)')+')')
        assert g.effects('G1-BOOL-EFFECT')==effects==1
        expected=[['Pair',2,2],['Pair',2,2],['Pair',5,5]]
        assert plain[-1]['values']==expected,plain
        assert [normalized(contribution(v)) for v in graded[-1]['values']]==[[x,'True',[]] for x in expected],graded
        assert plain[-1]['closed']==graded[-1]['closed']=='True'
        assert plain[-1]['status']==graded[-1]['status']=='closed'
    g.check('neutral Boolean preserves unannotated host sharing, duplicates, effects and closure')
    source='(=w (routes) A True) (=w (routes) A True)\n'
    client=f'(let {P} (select 0 {control(20000,extra="(demand all)")} '+read('(routes)','(weights:boolean)')+') (G1Boolean $v (weights:aggregate (weights:boolean) $v)))'
    t,_=g.run('Boolean sum is OR',PREFIX+source+f'!(select 1 {control(50000)} '+client+')')
    value=normalized(list(weight_observations(t))[-1]['values'][0]);raw=value[1];summed=value[2]
    assert len(raw)==2 and len({v[3][1][0][1] for v in raw})==2
    assert [[v[1],v[2]] for v in summed]==[['A','True']],value
    assert bag(summed[0][3][1])==bag(raw),value
    g.check('Boolean OR duplicate sum remains true, with two separate contributions')
    source='(=w (single) A 2) (=w (double) A 1) (=w (double) A 1)\n'
    for name,length in [('single',1),('double',2)]:
        client=f'(let {P} (select 0 {control(20000,extra="(demand all)")} '+read('('+name+')','(weights:natural)')+') (G1Count $v (weights:aggregate (weights:natural) $v)))'
        t,_=g.run('natural '+name,PREFIX+source+f'!(select 1 {control(50000)} '+client+')');value=normalized(list(weight_observations(t))[-1]['values'][0])
        assert len(value[1])==length and [[v[1],v[2]] for v in value[2]]==[['A',2]],value
        assert len({v[3][1][0][1] for v in value[1]})==length,value
    g.check('coefficient two and two unit derivations share a sum, not an occurrence count')

def tropical(g,library):
    prefix=PREFIX+library+'\n'
    values=['TropicalInfinity','(TropicalFinite 0)','(TropicalFinite (/ -1 3))','(TropicalFinite (/ 7 3))','(TropicalFinite 2000001)']
    meaning=[None,Fraction(0),Fraction(-1,3),Fraction(7,3),Fraction(2000001)]
    for op in ('add','mul','compare'):
        expressions=[];expected=[]
        for x,a in zip(values,meaning):
            for y,b in zip(values,meaning):
                expressions.append(f'(tropical:{op} {x} {y})')
                if op=='compare':
                    answer='Equal' if a==b else 'Greater' if a is None else 'Less' if b is None else 'Less' if a<b else 'Greater'
                else:
                    number=(b if a is None else a if b is None else min(a,b)) if op=='add' else None if a is None or b is None else a+b
                    answer='TropicalInfinity' if number is None else ['TropicalFinite',int(number) if number.denominator==1 else str(number)]
                expected.append(answer)
        source='\n'.join(f'!(select 1 {control(50000)} (G1Value '+x+'))' for x in expressions)
        t,_=g.run('tropical exact table '+op,prefix+source)
        actual=[normalized(o['values'][0][1]) for o in list(weight_observations(t))[-len(expressions):]]
        assert actual==expected,(op,actual,expected)
    g.check('tagged exact rational minimum, sum and total tagged order match independent reference')
    t,_=g.run('finite sentinel is not infinity',prefix+'!(G1Sentinel (if (< 1000000 2000001) 1000000 2000001) (tropical:add TropicalInfinity (TropicalFinite 2000001)))')
    assert t[-1][0]==['G1Sentinel',1000000,['TropicalFinite',2000001]],t
    g.check('finite sentinel counterexample distinguishes actual top identity')
    bad=['(TropicalFinite 0.5)','(TropicalFinite 1 2)','(TropicalFinite Unknown)','0','Unknown']
    t,_=g.run('tropical malformed domain',prefix+'\n'.join('!(G1Valid (tropical:valid? '+x+'))' for x in bad))
    assert [normalized(v[0]) for v in t[-len(bad):]]==[['G1Valid','False']]*len(bad),t
    g.check('floating, malformed and unrelated carriers acquire no exact tropical laws')
    source='(=w (routes) A (TropicalFinite 2000001)) (=w (routes) A (TropicalFinite (/ 1 3))) (=w (routes) A (TropicalFinite (/ 1 3))) (=w (routes) B TropicalInfinity)\n'
    client=f'(let {P} (select 0 {control(30000,extra="(demand all)")} '+read('(routes)','(weights:tropical)')+') (G1Tropical $v (weights:aggregate (weights:tropical) $v)))'
    t,_=g.run('tropical authored duplicate aggregate',prefix+source+f'!(select 1 {control(50000)} '+client+')')
    value=normalized(list(weight_observations(t))[-1]['values'][0]);raw=value[1];summed=value[2]
    assert len(raw)==4 and len({v[3][1][0][1] for v in raw})==4,value
    assert dict((v[1],v[2]) for v in summed)=={'A':['TropicalFinite','1/3'],'B':'TropicalInfinity'},value
    assert [v[1] for v in summed]==list(dict.fromkeys(v[1] for v in raw)),value
    assert sum(len(v[3][1]) for v in summed)==4,value
    g.check('native tropical read and min aggregation retain equal physical rows and infinity')

def real_pending(g):
    prefix=PREFIX+REAL+'''
(= (g1:strict $result) (if (== $result Less) True (if (or (== $result Greater) (== $result Equal)) False $result)))
(= (g1:same $weight) (g1:strict (real:compare $weight (RealLog 2) 0)))
(= (g1:less $weight) (g1:strict (real:refine (real:compare $weight (RealQ (/ 7 10)) 0) 4)))
(= (g1:greater $weight) (g1:strict (real:refine (real:compare $weight (RealQ (/ 1 2)) 0) 4)))
'''
    for precision in (0,1,4,8):
        t,_=g.run('identical log not equality oracle',prefix+f'!(G1Real (real:compare (RealLog 2) (RealLog 2) {precision}))')
        v=normalized(t[-1][0][1]);assert v[:4]==['RealUnresolved',['RealLog',2],['RealLog',2],precision],v
        assert v[4]==v[5] and Fraction(v[4][1])<Fraction(v[4][2]),v
    g.check('identical non-point log enclosures remain explicit unresolved comparisons')
    _,obs=g.run('completed symbolic coefficient',prefix+'!(select 7 '+control(30000)+' '+read('(weight:where Claim (RealLog 2))','(weights:real)')+')')
    assert [contribution(v) for v in obs[-1]['values']]==[['Claim',['RealTimes',['RealQ',1],['RealLog',2]],[['RealLog',2]]]],obs
    assert obs[-1]['closed']=='True'
    g.check('completed symbolic real coefficient has no unfinished comparison requirement')
    for predicate,status,effects in [('g1:same','suspended',0),('g1:less','closed',1),('g1:greater','closed',0)]:
        source=prefix+f'(=w (real-row) (let $_ (println! G1-REAL-BODY) Claim) (RealLog 2) (admit {predicate}))\n'
        expr=read('(real-row)','(weights:real)')
        _,whole=g.run(predicate+' whole',source+f'!(select 7 {control(30000)} {expr})');whole=whole[-1]
        assert whole['status']==status and whole['closed']==('False' if status=='suspended' else 'True'),whole
        assert len(whole['values'])==effects and g.effects('G1-REAL-BODY')==effects,whole
        for cut in (0,17,113,997):
            t,o=g.run(predicate+' split',source+f'!(let {P} (select 7 {control(cut)} {expr}) (Split $v (select 7 {control(30000-cut)} $r)))')
            joined=t[-1][0][1]+o[-1]['values'];assert joined==whole['values'],(cut,joined,whole)
            for field in ['work','closed','status','satisfied']:assert o[-1][field]==whole[field],(predicate,cut,field,o,whole)
            assert g.effects('G1-REAL-BODY')==effects
        if status=='suspended':
            zero=f'(let {P} (select 7 {control(0)} $r) (Zero $v $w $s $c $t))'
            t,_=g.run('same real zero resume',source+f'!(let {P} (select 7 {control(30000)} {expr}) (Zeros {zero} {zero}))')
            expected=['Zero',[],whole['work'],'False','False','suspended'];assert t[-1][0]==['Zeros',expected,expected],t
            assert g.effects('G1-REAL-BODY')==0
        g.check(predicate+' preserves exact result IDs, work, effects and pending/closed observation across splits')

def pln_projection(g):
    prefix=PREFIX+f'!(import! &self {ROOT}/lib/lib_wmpln_evidence.metta)\n'
    source='!(G1Evidence (evidence-revision (evidence 4 2) (evidence 3 1)) (evidence-toSTV (evidence 7 3)) (evidence-hplus (evidence 2 3) (evidence 2 3)) (evidence-tensor (evidence 1 0) (evidence 0 1)))'
    t,_=g.run('count operations and STV projection',prefix+source)
    assert normalized(t[-1][0])==['G1Evidence',['evidence',7,3],['stv','7/10','10/11'],['evidence',4,6],['evidence',0,0]],t
    g.check('existing evidence addition, nonidempotence, zero-divisor tensor and calibrated projection stay distinct')

def law_refusal(g):
    source=PREFIX+'(= (uncertified-times $x $y) (* $x $y)) (=w (route) A 2) (=w (route) B 3)\n'
    for law in ('','(law unit-interval-product)'):
        t,_=g.run('program operator does not inherit law',source+'!(select 1 '+control(10000,'WO','(order by-weight descending) '+law)+' '+read('(route)','(algebra 0 1 + uncertified-times)')+')')
        o=list(weight_observations(t))[-1]
        assert [contribution(v)[:2] for v in o['values']]==[['B',3]],o
        direction,pruning,stop=weight_order_record(o)
        assert pruning[0]=='Refused' and stop=='Exhausted' and o['closed']=='True',o
        assert pruning[1][0]=='LawDoesNotFitAlgebra' if law else pruning[1]=='NoDeclaredLaw'
    g.check('program-defined multiplication executes but gains no undeclared pruning authority')

def born_normalization(g):
    source=PREFIX+"(=w (paths) A (Complex 3 0)) (=w (paths) A (Complex 1 3)) (=w (paths) B (Complex 0 1))\n"
    client=f'(let {P} (select 0 {control(30000,extra="(demand all)")} '+read('(paths)','(weights:complex)')+') (let $sums (weights:aggregate (weights:complex) $v) (G1Born $v $sums (weights:born $sums))))'
    _,obs=g.run('Born after coherent contribution sum',source+f'!(select 1 {control(50000)} '+client+')')
    v=normalized(obs[-1]['values'][0]);raw,sums,probabilities=v[1:]
    assert len(raw)==3 and len({x[3][1][0][1] for x in raw})==3,v
    assert dict((x[1],x[2]) for x in sums)=={'A':['Complex',4,3],'B':['Complex',0,1]},v
    assert dict((x[1],x[2]) for x in probabilities)=={'A':'25/26','B':'1/26'},v
    assert sum(Fraction(x[2]) for x in probabilities)==1 and all(Fraction(x[2])>=0 for x in probabilities),v
    assert [x[1] for x in probabilities]==[x[1] for x in sums],v
    for probability,total in zip(probabilities,sums):assert probability[3]==total[3],v
    assert bag([r for x in probabilities for r in x[3][1]])==bag(raw),v
    assert sum(contribution(x)[1][1]**2+contribution(x)[1][2]**2 for x in raw)==20
    assert sum(x[2][1]**2+x[2][2]**2 for x in sums)==26
    _,o=g.run('Born zero norm refuses normalization',PREFIX+f'!(select 1 {control(30000)} (weights:born ((WeightedSum A (Complex 0 0) EvidenceA) (WeightedSum B (Complex 0 0) EvidenceB))))')
    assert o[-1]['values']==[['BornUndefined','zero-norm']],o
    g.check('exact Born 25/26 plus 1/26 uses aggregated amplitude, retains all evidence and refuses zero norm')


def receipt(value):
    assert value[:2]==['NativeCost','native-events-v1'],value
    f=dict(value[2:]);f['counts']={v[1]:v[2] for v in f['operations']}
    assert len(f['counts'])==len(f['operations'])==29,value
    assert f['work']==['Exact',sum(f['counts'].values())],value
    assert f['span'][0] in ('Sequential','ForkJoin') and f['span'][1][0]=='Exact',value
    assert 0<=f['span'][1][1]<=f['work'][1] and f['trace-status']=='Complete',value
    assert f['read'][4:]==['cost-read',1,1] and f['read'][2]==f['scope'],value
    return f

def pattern(i,order=True,cost=False):
    return f'(Observation $v{i} (satisfied $s{i}) (closed $c{i}) (residual $r{i}) (work $w{i}) (status $t{i})'+(f' (cost $a{i})' if cost else '')+(f' $o{i}' if order else '')+')'
def state(i,cost=False):
    return f'(Saved $v{i} $s{i} $c{i} $w{i} $t{i} $o{i}'+(f' $a{i}' if cost else '')+')'
def exercise(g,case,algebra,rows,expected,law=''):
    prefix=PREFIX+'\n(= (g1:right $x $y) $y)\n'+rows
    expression=read('(rows)',algebra)
    plain=prefix+f'!(select 0 {g.control(30000,extra="(demand all)")} {expression})'
    _,whole=g.run(case+' uninterrupted FIFO',plain);whole=whole[-1]
    assert bag(normalized(contribution(v)[:2]) for v in whole['values'])==bag(expected),whole
    assert whole['closed']=='True',whole
    effects=g.effects('G1-RECOVER-BODY');assert effects==len(expected),(effects,expected)
    for costs in (False,True):
        extra='(costs native-events-v1)' if costs else ''
        pol=lambda n:g.control(n,'WO','(order by-weight ascending) '+law+' '+extra)
        fifo=g.control(0,extra='(demand all) '+extra)
        body=f'(Recovered {state(0,costs)} {state(1,costs)} {state(2,costs)} (select 0 {fifo} $r2))'
        for i in (2,1,0):
            value=expression if i==0 else f'$r{i-1}'
            body=f'(let {pattern(i,cost=costs)} (select 1 {pol(30000 if i==0 else 0)} {value}) {body})'
        t,o=g.run(case+f' refused then zero/FIFO costs{costs}',prefix+'!'+body)
        value=t[-1][0];assert value[0]=='Recovered',value
        saved=value[1:4];final=o[-1]
        for s in saved:
            assert s[1:4]==[[],'False','False'] and s[5]=='suspended',s
            assert s[6][0]=='weight-order' and next(v[1] for v in s[6][1:] if v[0]=='pruning')[0]=='Refused',s
        assert all(s[4]==saved[0][4] for s in saved),saved
        assert final['values']==whole['values'],(final,whole)
        assert final['work']==saved[0][4] and final['closed']=='True' and final['status']=='closed',final
        assert g.effects('G1-RECOVER-BODY')==effects
        ids=[v[3][1][0][1] for v in final['values']];assert len(ids)==len(set(ids))==len(expected),ids
        if costs:
            acc=[receipt(s[7]) for s in saved]+[receipt(final['cost'])]
            first=acc[0]['counts']
            for index,a in enumerate(acc):
                delta=index+(len(expected) if index==3 else 0)
                assert a['span']==[acc[0]['span'][0],['Exact',acc[0]['span'][1][1]+delta]],(index,a['span'],acc[0]['span'])
            for index,a in enumerate(acc[1:],1):
                for key,count in a['counts'].items():
                    delta=index if key=='cost-read' else len(expected) if key=='publish' and index==3 else 0
                    assert count==first[key]+delta,(case,index,key,count,first[key],delta)
            assert len({a['scope'] for a in acc})==1,acc
        g.check(case+f' costs{costs}: completed coefficients, raw occurrence IDs, effects and paid work survive two zero resumes and FIFO')
    # Numeric-order entry at finite cuts uses the same source; later FIFO carries
    # the saved output together with whatever source continuation remains.
    for cut in (0,1,17,113):
        t,o=g.run(case+f' policy replacement cut{cut}',prefix+
            f'!(let {pattern(0)} (select 1 {g.control(cut,"WO","(order by-weight ascending) "+law)} {expression}) '
            f'(Split $v0 (select 0 {g.control(30000,extra="(demand all)")} $r0)))')
        assert t[-1][0][1]+o[-1]['values']==whole['values'],(cut,t,o,whole)
        assert o[-1]['closed']=='True' and g.effects('G1-RECOVER-BODY')==effects,(cut,o)
    g.check(case+': FIFO replacement preserves all outputs through four incomplete-source cuts')


def nested(g):
    prefix=PREFIX+'\n(=w (rows) (let $_ (println! G1-RECOVER-BODY) V) (TropicalFinite 2))\n'
    expression=read('(rows)','(weights:tropical)')
    inner=f'(select 1 {g.control(20000,"WO","(order by-weight ascending) (law nonnegative-sum)")} {expression})'
    pair=[]
    for grant in (2000,10000):
        t,o=g.run('nested explicit pending observation',prefix+f'!(select 0 {g.control(grant,extra="(demand all)")} {inner})')
        outer=o[0];assert len(outer['values'])==1 and outer['closed']=='True',o
        child=list(observations(outer['values']))[0]
        assert child['values']==[] and child['status']=='suspended' and child['closed']=='False',child
        assert g.effects('G1-RECOVER-BODY')==1
        pair.append((outer,child))
    for field in ('values','work','closed','status','satisfied'):assert pair[0][0][field]==pair[1][0][field],(field,pair)
    body=f'(let {pattern(0)} {inner} (NestedRecovery $w0 $t0 (select 7 {g.control(0)} $r0)))'
    _,o=g.run('nested suspended Observation can change consumer',prefix+f'!(select 1 {g.control(30000)} {body})')
    value=o[0]['values'][0];assert value[:1]==['NestedRecovery'] and value[2]=='suspended',o
    child=list(observations(value))[0]
    assert len(child['values'])==1 and contribution(child['values'][0])==['V',['TropicalFinite',2],[['TropicalFinite',2]]],child
    assert child['work']==value[1] and g.effects('G1-RECOVER-BODY')==1,o
    g.check('nested explicit suspension publishes once without grant polling and recovers paid child')
    # An implicit surrounding read must expose the nested observer result as data.
    _,o=g.run('implicit outer read with explicit pending child',prefix+'!'+read(inner,'(weights:natural)'))
    child=list(observations(o[0]['values']))[0] if o and o[0]['values'] else None
    # Readout shape is asserted directly instead of interpreting absence as refusal.
    assert g.effects('G1-RECOVER-BODY')==1
    trees=g.gate.receipts[-1]['stdout']
    assert 'suspended' in trees and 'fault' not in trees and 'Weighted' in trees,trees
    g.check('ordinary implicit outer handler retains explicit suspended child as a weighted value')


def numeric(g):
    source='(=w (rows) A 1/2) (=w (rows) B 3/4) (=w (rows) B 3/4)'
    source=source.replace('1/2','(/ 1 2)').replace('3/4','(/ 3 4)')
    for law in ('','(law unit-interval-product)'):
        _,o=g.run('numeric by-weight remains exact',PREFIX+source+'\n!(select 2 '+g.control(30000,'WO','(order by-weight descending) '+law)+' '+read('(rows)')+')')
        assert normalized([contribution(v)[:2] for v in o[-1]['values']])==[['B','3/4'],['B','3/4']],o
        assert o[-1]['satisfied']=='True',o
    g.check('numeric duplicate best answers remain unaffected with absent and certified law')


def _weighted(tree):
    if isinstance(tree,list):
        if tree and tree[0]=='Weighted':yield tree
        else:
            for child in tree:yield from _weighted(child)


def file_clients(g):
    trees,_=g.gate.run_file('ordinary tagged tropical file client',authored_fixture(g.gate, 'tropical_coefficients.metta'))
    values=list(_weighted(trees))
    assert bag(normalized(contribution(v)[:2]) for v in values)==bag([
        ['A',['TropicalFinite','1/3']],['A',['TropicalFinite','1/3']],['B','TropicalInfinity']]),trees
    assert len({v[3][1][0][1] for v in values})==3,values
    trees,obs=g.gate.run_file('ordinary pending real file client',authored_fixture(g.gate, 'real_admission.metta'))
    assert len(obs)==1 and obs[0]['values']==[] and obs[0]['status']=='suspended',obs
    assert [contribution(v)[0] for v in _weighted(trees)]==['Approved'],trees
    assert g.effects('APPROVED-REAL')==1 and g.effects('UNRESOLVED-REAL-BODY')==0
    trees,obs=g.gate.run_file('recover refused numeric order file client',authored_fixture(g.gate, 'weight_order_recovery.metta'))
    value=trees[-1][0]
    assert value[0]=='Recovery' and value[2]=='suspended',value
    assert obs[-1]['work']==value[1] and obs[-1]['closed']=='True',obs
    assert [contribution(v)[:2] for v in obs[-1]['values']]==[['V',['TropicalFinite',2]]],obs
    assert g.effects('RECOVERY-BODY')==1
    _,obs=g.gate.run_file('finite Born normalization file client',authored_fixture(g.gate, 'born_normalization.metta'))
    value=normalized(obs[-1]['values'][0]);raw,sums,probs=value[1:]
    assert dict((p[1],p[2]) for p in probs)=={'A':'25/26','B':'1/26'},value
    assert bag(r for p in probs for r in p[3][1])==bag(raw),value
    g.check('four ordinary file clients preserve tropical duplicates, pending real, policy recovery and Born evidence')


def families(gate):
    g=FamilyGate(gate)
    boolean_counts(g)
    tropical(g, '')
    real_pending(g)
    pln_projection(g)
    born_normalization(g)
    law_refusal(g)
    file_clients(g)


def ordering_recovery(gate):
    cases=[
      ('tagged tropical','(weights:tropical)',
       [('A','(TropicalFinite 2)'),('B','TropicalInfinity'),('A','(TropicalFinite 2)')],
       [['A',['TropicalFinite',2]],['B','TropicalInfinity'],['A',['TropicalFinite',2]]],
       '(law nonnegative-sum)'),
      ('mixed numeric/tagged','(algebra 0 Identity + g1:right)',
       [('A','2'),('B','(Tagged 3)'),('A','2')],
       [['A',2],['B',['Tagged',3]],['A',2]],'')]
    for width in (1,2):
        g=FamilyGate(gate,width)
        for name,algebra,rows,expected,law in cases:
            source='\n'.join(f'(=w (rows) (let $_ (println! G1-RECOVER-BODY) {v}) {w})' for v,w in rows)
            exercise(g,name,algebra,source,expected,law)
        nested(g)
        numeric(g)


def main():
    from test_native_dialects import DialectGate
    p=ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--timeout', type=int, default=30)
    p.add_argument('--program-dir', type=Path)
    p.add_argument('--receipt', type=Path)
    args=p.parse_args();args.binary=args.binary.resolve()
    if args.program_dir: args.program_dir.mkdir(parents=True,exist_ok=False)
    gates=[]
    try:
        for language in ('prime','he','petta'):
            gate=Gate(args) if language=='prime' else DialectGate(args,language)
            gates.append(gate)
            families(gate)
            ordering_recovery(gate)
    finally:
        if args.receipt:
            args.receipt.write_text(json.dumps({'binary':str(args.binary),
                'binary_sha256':sha256(args.binary.read_bytes()).hexdigest(),
                'passed':sum(g.passed for g in gates),
                'runs':[r for g in gates for r in g.receipts]},indent=2)+'\n')
    print(f'Native weight families: {sum(g.passed for g in gates)} passed, 0 failed')


if __name__=='__main__':main()
