#!/usr/bin/env python3
"""LUBM: OWL 2 RL materialization over the univ-bench ontology and the 14
benchmark queries, for MORK (MM2) and CeTTa (MeTTa, PeTTa dialect).

Source: the Lehigh University Benchmark (Guo, Pan, Heflin, J. Web Semantics
3(2-3) 2005), data generator UBA 1.7 (ported exactly in uba.py), ontology
univ-bench.owl, queries queries-sparql.txt.  For LUBM(1,0) the answers
computed by oracle.py equal the published reference answers set for set.

Encoding.  IRIs and literals are dictionary-encoded as integers (in order of
first appearance), as RDF stores and Datalog engines do; UBA's IRIs are up to
80 bytes, beyond MM2's 63-byte symbol limit.  Classes and properties get
short names (legend in each program).  Facts: (ty C x) for rdf:type and
(tr P s o) for property triples; the TBox is data (sc, sp, inv, trans, dom,
rng, ex, ex1), and the rules are driven by it.

  MM2   semi-naive rounds: every rule joins one delta relation of the last
        round with the closure; candidates already known are subtracted, the
        rest become the next delta; a reverse view (rtr P o s) serves the
        object-bound probes, as in MORK's FlyBase work.
  MeTTa forward chaining by agenda: a new fact is stored once and fires the
        rules it can complete, joining with what is already known; the same
        reverse view.

Both then answer the 14 queries into answer relations q1 ... q14.

Writes gen/lubm_N.{mm2,metta,heads,values,expect.json,oracle_values.json}.
usage: gen_lubm.py N [--seed 0] [--values small|none]
"""
import argparse, json, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uba
import oracle as O

HERE = Path(__file__).resolve().parent

CLS = {'University': 'Univ', 'Department': 'Dept', 'Faculty': 'Fac', 'Professor': 'Prof',
       'FullProfessor': 'FProf', 'AssociateProfessor': 'AsoProf', 'AssistantProfessor': 'AstProf',
       'Lecturer': 'Lect', 'Student': 'Stud', 'UndergraduateStudent': 'UStud', 'GraduateStudent': 'GStud',
       'TeachingAssistant': 'TA', 'ResearchAssistant': 'RA', 'Course': 'Crs', 'GraduateCourse': 'GCrs',
       'Publication': 'Pub', 'Chair': 'Chair', 'Research': 'Res', 'ResearchGroup': 'RGrp', 'Person': 'Pers',
       'Employee': 'Emp', 'Organization': 'Org', 'Work': 'Work', 'AdministrativeStaff': 'AdmS',
       'Article': 'Art', 'Book': 'Book', 'ClericalStaff': 'ClrS', 'College': 'Coll', 'ConferencePaper': 'ConfP',
       'Dean': 'Dean', 'Director': 'Dir', 'Institute': 'Inst', 'JournalArticle': 'JArt', 'Manual': 'Man',
       'PostDoc': 'PDoc', 'Program': 'Prog', 'Schedule': 'Sched', 'Software': 'Soft', 'Specification': 'Spec',
       'SystemsStaff': 'SysS', 'TechnicalReport': 'TRep', 'UnofficialPublication': 'UPub',
       'VisitingProfessor': 'VProf'}
PROP = {'name': 'nm', 'takesCourse': 'tc', 'teacherOf': 'teach', 'undergraduateDegreeFrom': 'ugd',
        'mastersDegreeFrom': 'msd', 'doctoralDegreeFrom': 'phd', 'degreeFrom': 'deg', 'hasAlumnus': 'alum',
        'advisor': 'adv', 'memberOf': 'mof', 'member': 'mem', 'publicationAuthor': 'auth', 'headOf': 'head',
        'teachingAssistantOf': 'tao', 'researchInterest': 'ri', 'emailAddress': 'mail', 'telephone': 'tel',
        'subOrganizationOf': 'sub', 'worksFor': 'wf', 'affiliatedOrganizationOf': 'affo', 'affiliateOf': 'aff',
        'age': 'age', 'listedCourse': 'lc', 'orgPublication': 'opub', 'publicationDate': 'pdate',
        'publicationResearch': 'pres', 'researchProject': 'rproj', 'softwareDocumentation': 'sdoc',
        'softwareVersion': 'sver', 'tenured': 'ten', 'title': 'title'}

INC = "(O (pure (succ $k1 $n) $n (i64_to_string (sum_i64 (i64_from_string $k1) (i64_from_string 1)))))"


def tbox_facts():
    f = [f"(sc {CLS[a]} {CLS[b]})" for a, b in O.SC]
    f += [f"(sp {PROP[a]} {PROP[b]})" for a, b in O.SP]
    f += [f"(inv {PROP[a]} {PROP[b]})" for a, b in O.INV]
    f += [f"(trans {PROP[p]})" for p in O.TRANS]
    f += [f"(dom {PROP[a]} {CLS[b]})" for a, b in O.DOM]
    f += [f"(rng {PROP[a]} {CLS[b]})" for a, b in O.RNG]
    for c, a, p, d in O.EX:
        if a is None:
            f.append(f"(ex1 {CLS[c]} {PROP[p]} {CLS[d]})")
        else:
            f.append(f"(ex {CLS[c]} {CLS[a]} {PROP[p]} {CLS[d]})")
    return f


def rule_execs(DTY, DTR, k, k1, loc):
    """The closure rules with the TBox atom first and the delta atom second.
    DTY(c, x) / DTR(p, s, o) give the delta atoms; the input itself in round 0."""
    R = [
        ('a', f"(, (sc $c $d) {DTY('$c', '$x')}) (, (cty {k1} $d $x))"),
        ('b', f"(, (sp $p $q) {DTR('$p', '$s', '$o')}) (, (ctr {k1} $q $s $o))"),
        ('c', f"(, (inv $p $q) {DTR('$p', '$s', '$o')}) (, (ctr {k1} $q $o $s))"),
        ('d', f"(, (trans $p) {DTR('$p', '$s', '$o')} (tr $p $o $z)) (, (ctr {k1} $p $s $z))"),
        ('e', f"(, (trans $p) {DTR('$p', '$o', '$z')} (rtr $p $o $s)) (, (ctr {k1} $p $s $z))"),
        ('f', f"(, (dom $p $c) {DTR('$p', '$s', '$o')}) (, (cty {k1} $c $s))"),
        ('g', f"(, (rng $p $c) {DTR('$p', '$s', '$o')}) (, (cty {k1} $c $o))"),
        ('h', f"(, (ex $c $a $p $d) {DTY('$a', '$x')} (tr $p $x $y) (ty $d $y)) (, (cty {k1} $c $x))"),
        ('i', f"(, (ex $c $a $p $d) {DTR('$p', '$x', '$y')} (ty $d $y) (ty $a $x)) (, (cty {k1} $c $x))"),
        ('j', f"(, (ex $c $a $p $d) {DTY('$d', '$y')} (rtr $p $y $x) (ty $a $x)) (, (cty {k1} $c $x))"),
        ('k', f"(, (ex1 $c $p $d) {DTR('$p', '$x', '$y')} (ty $d $y)) (, (cty {k1} $c $x))"),
        ('l', f"(, (ex1 $c $p $d) {DTY('$d', '$y')} (rtr $p $y $x)) (, (cty {k1} $c $x))"),
        ('m', f"(, (cty {k1} $c $x) (ty $c $x)) (O (- (cty {k1} $c $x)))"),
        ('n', f"(, (ctr {k1} $p $s $o) (tr $p $s $o)) (O (- (ctr {k1} $p $s $o)))"),
        ('o', f"(, (cty {k1} $c $x)) (O (+ (ty $c $x)) (+ (dty {k1} $c $x)) (+ (live {k1})) (- (cty {k1} $c $x)))"),
        ('p', f"(, (ctr {k1} $p $s $o)) (O (+ (tr $p $s $o)) (+ (dtr {k1} $p $s $o)) (+ (live {k1})))"),
        ('q', f"(, (ctr {k1} $p $s $o)) (O (+ (rtr $p $o $s)) (- (ctr {k1} $p $s $o)))"),
    ]
    return [f"(exec ({loc} {k} {x}) {body})" for x, body in R]


def queries(ids):
    D0 = ids[O.D0]; U0 = ids[O.U0]
    GC0 = ids[O.D0 + '/GraduateCourse0']
    AP0 = ids[O.D0 + '/AssistantProfessor0']
    AsoP0 = ids[O.D0 + '/AssociateProfessor0']
    # (head, variables, conjuncts in join order); the same order on both engines
    return [
        ('q1', '$x', [f"(rtr tc {GC0} $x)", "(ty GStud $x)"]),
        ('q2', '$x $y $z', ["(ty GStud $x)", "(tr mof $x $z)", "(ty Dept $z)", "(tr sub $z $y)",
                            "(ty Univ $y)", "(tr ugd $x $y)"]),
        ('q3', '$x', [f"(rtr auth {AP0} $x)", "(ty Pub $x)"]),
        ('q4', '$x $y1 $y2 $y3', [f"(rtr wf {D0} $x)", "(ty Prof $x)", "(tr nm $x $y1)",
                                  "(tr mail $x $y2)", "(tr tel $x $y3)"]),
        ('q5', '$x', [f"(rtr mof {D0} $x)", "(ty Pers $x)"]),
        ('q6', '$x', ["(ty Stud $x)"]),
        ('q7', '$x $y', [f"(tr teach {AsoP0} $y)", "(ty Crs $y)", "(rtr tc $y $x)", "(ty Stud $x)"]),
        ('q8', '$x $y $z', [f"(rtr sub {U0} $y)", "(ty Dept $y)", "(rtr mof $y $x)", "(ty Stud $x)",
                            "(tr mail $x $z)"]),
        ('q9', '$x $y $z', ["(tr adv $x $y)", "(ty Stud $x)", "(ty Fac $y)", "(tr teach $y $z)",
                            "(ty Crs $z)", "(tr tc $x $z)"]),
        ('q10', '$x', [f"(rtr tc {GC0} $x)", "(ty Stud $x)"]),
        ('q11', '$x', [f"(rtr sub {U0} $x)", "(ty RGrp $x)"]),
        ('q12', '$x $y', [f"(rtr sub {U0} $y)", "(ty Dept $y)", "(rtr wf $y $x)", "(ty Chair $x)"]),
        ('q13', '$x', [f"(tr alum {U0} $x)", "(ty Pers $x)"]),
        ('q14', '$x', ["(ty UStud $x)"]),
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('N', type=int)
    ap.add_argument('--seed', type=int, default=0)
    ap.add_argument('--values', default='small', choices=['small', 'none'])
    ap.add_argument('--outdir', default=str(HERE / 'gen'))
    a = ap.parse_args()
    triples = uba.generate(a.N, 0, a.seed)
    ids = {}

    def enc(x):
        if x not in ids:
            ids[x] = len(ids)
        return ids[x]

    tyf, trf = set(), set()
    for s, p, o, lit in triples:
        if p == 'type':
            tyf.add((CLS[o], enc(s)))
        else:
            trf.add((PROP[p], enc(s), enc(o)))
    tyl = sorted(tyf, key=lambda t: (t[1], t[0]))
    trl = sorted(trf, key=lambda t: (t[1], t[0], t[2]))
    data = '\n'.join(f"(ty {c} {x})" for c, x in tyl) + '\n' + '\n'.join(f"(tr {p} {s} {o})" for p, s, o in trl) + '\n'
    tbox = '\n'.join(tbox_facts()) + '\n'
    legend = ('; classes: ' + ' '.join(f"{v}={k}" for k, v in CLS.items()) + '\n'
              '; properties: ' + ' '.join(f"{v}={k}" for k, v in PROP.items()) + '\n')
    Q = queries(ids)
    name = f'lubm_{a.N}'
    out = Path(a.outdir); out.mkdir(parents=True, exist_ok=True)

    # ---------------- MM2
    round0 = '\n'.join(rule_execs(lambda c, x: f"(ty {c} {x})", lambda p, s_, o: f"(tr {p} {s_} {o})", 1, 1, 0))
    rk = rule_execs(lambda c, x: f"(dty $k {c} {x})", lambda p, s_, o: f"(dtr $k {p} {s_} {o})", '$k', '$k1', 1)
    rk += ["(exec (1 $k r) (, (dty $k $c $x)) (O (- (dty $k $c $x))))",
           "(exec (1 $k s) (, (dtr $k $p $s $o)) (O (- (dtr $k $p $s $o))))",
           f"(exec (1 $k t) (, (live $k1)) {INC})"]
    roundk = '\n                 '.join(rk)
    qexecs = '\n'.join(f"(exec (3 0 {chr(97 + i)}) (, {' '.join(body)}) (, ({h} {v})))" for i, (h, v, body) in enumerate(Q))
    mm2 = f"""; {name}: LUBM({a.N}, {a.seed}), {len(tyl) + len(trl)} input facts; OWL 2 RL closure by semi-naive rounds, then the 14 queries
{legend}; ids: integers in order of first appearance; Department0.University0 = {ids[O.D0]}, University0 = {ids[O.U0]}
{tbox}{data}(succ 1 2)
; the reverse view serves object-bound probes
(exec (0 0 0) (, (tr $p $s $o)) (, (rtr $p $o $s)))
; round 0: the rules over the whole input give the first delta
{round0}
; round k: each rule joins this round's delta with the closure; known candidates are dropped,
; the new ones join the closure and form the next delta
(exec (2 1 c) (, (exec (2 $k c) $ps $ts) (succ $k $k1) (live $k))
              (, (exec (2 $k1 c) $ps $ts)
                 {roundk}))
; the queries, over the closure
{qexecs}
"""
    (out / f'{name}.mm2').write_text(mm2)

    # ---------------- MeTTa
    def nest(body, tail):
        s = tail
        for atom in reversed(body):
            s = f"(match &kb {atom} {s})"
        return s
    qlines = '\n'.join(f"!(size-atom (collapse {nest(body, f'(add-atom &q ({h} {v}))')}))" for h, v, body in Q)
    small = ['q1', 'q2', 'q3', 'q4', 'q7', 'q9', 'q10', 'q11', 'q12', 'q13']
    prints = '\n'.join(f"!(match &q ({h} {v}) ({h} {v}))" for h, v, body in Q if h in small) if a.values == 'small' else ''
    counts = '\n'.join(f"!(size-atom (collapse (match &q ({h} {v}) t)))" for h, v, body in Q)
    metta = f"""; {name}: the same closure and queries in MeTTa (PeTTa dialect), forward chaining by agenda
{legend}{tbox}{data}!(bind! &kb (new-space set))
; a new fact is stored once (with its reverse view) and fires every rule it can complete,
; joining with what is already known; known facts fire nothing
(= (add-ty $c $x)
   (if (== (collapse (match &kb (ty $c $x) t)) ())
       (let $_ (add-atom &kb (ty $c $x)) (fire-ty $c $x))
       (empty)))
(= (add-tr $p $s $o)
   (if (== (collapse (match &kb (tr $p $s $o) t)) ())
       (let* (($_ (add-atom &kb (tr $p $s $o))) ($_ (add-atom &kb (rtr $p $o $s)))) (fire-tr $p $s $o))
       (empty)))
(= (fire-ty $c $x) (match &self (sc $c $d) (add-ty $d $x)))
(= (fire-ty $a $x) (match &self (ex $c $a $p $d) (match &kb (tr $p $x $y) (match &kb (ty $d $y) (add-ty $c $x)))))
(= (fire-ty $d $y) (match &self (ex $c $a $p $d) (match &kb (rtr $p $y $x) (match &kb (ty $a $x) (add-ty $c $x)))))
(= (fire-ty $d $y) (match &self (ex1 $c $p $d) (match &kb (rtr $p $y $x) (add-ty $c $x))))
(= (fire-tr $p $s $o) (match &self (sp $p $q) (add-tr $q $s $o)))
(= (fire-tr $p $s $o) (match &self (inv $p $q) (add-tr $q $o $s)))
(= (fire-tr $p $s $o) (match &self (trans $p) (match &kb (tr $p $o $z) (add-tr $p $s $z))))
(= (fire-tr $p $o $z) (match &self (trans $p) (match &kb (rtr $p $o $s) (add-tr $p $s $z))))
(= (fire-tr $p $s $o) (match &self (dom $p $c) (add-ty $c $s)))
(= (fire-tr $p $s $o) (match &self (rng $p $c) (add-ty $c $o)))
(= (fire-tr $p $x $y) (match &self (ex $c $a $p $d) (match &kb (ty $d $y) (match &kb (ty $a $x) (add-ty $c $x)))))
(= (fire-tr $p $x $y) (match &self (ex1 $c $p $d) (match &kb (ty $d $y) (add-ty $c $x))))
!(size-atom (collapse (match &self (ty $c $x) (add-ty $c $x))))
!(size-atom (collapse (match &self (tr $p $s $o) (add-tr $p $s $o))))
!(bind! &q (new-space set))
{qlines}
{prints}
!(size-atom (collapse (match &kb (ty $c $x) t)))
!(size-atom (collapse (match &kb (tr $p $s $o) t)))
{counts}
"""
    (out / f'{name}.metta').write_text(metta)

    heads = ['ty', 'tr'] + [h for h, v, b in Q]
    (out / f'{name}.heads').write_text(' '.join(heads) + '\n')
    if a.values == 'small':
        (out / f'{name}.values').write_text(' '.join(small) + '\n')
    # oracle: the Python closure and queries, re-encoded
    ty, tr = O.closure(triples)
    ix = O.Index(ty, tr)
    expect = {'ty': len(ty), 'tr': len(tr)}
    ov = {}
    for qn in range(1, 15):
        ans = O.answer(O.QUERIES[qn], ty, tr, ix)
        expect[f'q{qn}'] = len(ans)
        if f'q{qn}' in small:
            ov[f'q{qn}'] = sorted(f"(q{qn} {' '.join(str(ids[v]) for v in t)})" for t in ans)
    (out / f'{name}.expect.json').write_text(json.dumps(expect))
    (out / f'{name}.oracle_values.json').write_text(json.dumps(ov))
    print(name, 'input facts', len(tyl) + len(trl), 'expect', expect)


if __name__ == '__main__':
    main()
