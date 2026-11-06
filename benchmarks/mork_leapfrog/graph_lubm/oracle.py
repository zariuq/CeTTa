#!/usr/bin/env python3
"""The univ-bench TBox as Datalog data, the OWL 2 RL closure it entails over
UBA data (semi-naive, in Python), and the 14 LUBM queries.

Rules (each driven by TBox facts, as an RDF store's rule engine applies them):
  sc(C, D), ty(C, x)                  -> ty(D, x)        subClassOf
  sp(P, Q), tr(P, s, o)               -> tr(Q, s, o)     subPropertyOf
  inv(P, Q), tr(P, s, o)              -> tr(Q, o, s)     inverseOf (both directions listed)
  trans(P), tr(P, s, o), tr(P, o, z)  -> tr(P, s, z)     TransitiveProperty
  dom(P, C), tr(P, s, o)              -> ty(C, s)        domain
  rng(P, C), tr(P, s, o)              -> ty(C, o)        range
  ex(C, A, P, D), ty(A, x), tr(P, x, y), ty(D, y) -> ty(C, x)
                                     C == A and some P.D (A is Thing for Dean)

`python3 oracle.py --check REFDIR` generates LUBM(1,0), answers the 14 queries
and compares every answer set with the published reference answers.
"""
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uba

# ---- the univ-bench ontology (http://swat.cse.lehigh.edu/onto/univ-bench.owl)
SC = [('AdministrativeStaff', 'Employee'), ('Article', 'Publication'), ('AssistantProfessor', 'Professor'),
      ('AssociateProfessor', 'Professor'), ('Book', 'Publication'), ('Chair', 'Professor'),
      ('ClericalStaff', 'AdministrativeStaff'), ('College', 'Organization'), ('ConferencePaper', 'Article'),
      ('Course', 'Work'), ('Dean', 'Professor'), ('Department', 'Organization'), ('Faculty', 'Employee'),
      ('FullProfessor', 'Professor'), ('GraduateCourse', 'Course'), ('GraduateStudent', 'Person'),
      ('Institute', 'Organization'), ('JournalArticle', 'Article'), ('Lecturer', 'Faculty'),
      ('Manual', 'Publication'), ('PostDoc', 'Faculty'), ('Professor', 'Faculty'), ('Program', 'Organization'),
      ('Research', 'Work'), ('ResearchAssistant', 'Person'), ('ResearchGroup', 'Organization'),
      ('Software', 'Publication'), ('Specification', 'Publication'), ('SystemsStaff', 'AdministrativeStaff'),
      ('TechnicalReport', 'Article'), ('UndergraduateStudent', 'Student'), ('University', 'Organization'),
      ('UnofficialPublication', 'Publication'), ('VisitingProfessor', 'Professor'),
      # the subsumptions inside the intersection definitions below
      ('Chair', 'Person'), ('Director', 'Person'), ('Employee', 'Person'), ('Student', 'Person'),
      ('TeachingAssistant', 'Person')]
SP = [('doctoralDegreeFrom', 'degreeFrom'), ('mastersDegreeFrom', 'degreeFrom'),
      ('undergraduateDegreeFrom', 'degreeFrom'), ('headOf', 'worksFor'), ('worksFor', 'memberOf')]
INV = [('degreeFrom', 'hasAlumnus'), ('hasAlumnus', 'degreeFrom'), ('memberOf', 'member'), ('member', 'memberOf')]
TRANS = ['subOrganizationOf']
DOM = [('advisor', 'Person'), ('affiliatedOrganizationOf', 'Organization'), ('affiliateOf', 'Organization'),
       ('age', 'Person'), ('degreeFrom', 'Person'), ('doctoralDegreeFrom', 'Person'), ('emailAddress', 'Person'),
       ('hasAlumnus', 'University'), ('listedCourse', 'Schedule'), ('mastersDegreeFrom', 'Person'),
       ('member', 'Organization'), ('orgPublication', 'Organization'), ('publicationAuthor', 'Publication'),
       ('publicationDate', 'Publication'), ('publicationResearch', 'Publication'),
       ('researchProject', 'ResearchGroup'), ('softwareDocumentation', 'Software'), ('softwareVersion', 'Software'),
       ('subOrganizationOf', 'Organization'), ('teacherOf', 'Faculty'), ('teachingAssistantOf', 'TeachingAssistant'),
       ('telephone', 'Person'), ('tenured', 'Professor'), ('title', 'Person'), ('undergraduateDegreeFrom', 'Person')]
RNG = [('advisor', 'Professor'), ('affiliatedOrganizationOf', 'Organization'), ('affiliateOf', 'Person'),
       ('degreeFrom', 'University'), ('doctoralDegreeFrom', 'University'), ('hasAlumnus', 'Person'),
       ('listedCourse', 'Course'), ('mastersDegreeFrom', 'University'), ('member', 'Person'),
       ('orgPublication', 'Publication'), ('publicationAuthor', 'Person'), ('publicationResearch', 'Research'),
       ('researchProject', 'Research'), ('softwareDocumentation', 'Publication'),
       ('subOrganizationOf', 'Organization'), ('teacherOf', 'Course'), ('teachingAssistantOf', 'Course'),
       ('undergraduateDegreeFrom', 'University')]
# C == A and (some P . D); A = None for Dean (no Person conjunct)
EX = [('Chair', 'Person', 'headOf', 'Department'), ('Dean', None, 'headOf', 'College'),
      ('Director', 'Person', 'headOf', 'Program'), ('Employee', 'Person', 'worksFor', 'Organization'),
      ('Student', 'Person', 'takesCourse', 'Course'),
      ('TeachingAssistant', 'Person', 'teachingAssistantOf', 'Course')]

D0 = 'http://www.Department0.University0.edu'
U0 = 'http://www.University0.edu'
# the 14 queries: (select variables, conjuncts); a conjunct is ('ty', C, t) or ('tr', P, s, o);
# terms starting with '?' are variables
QUERIES = {
    1: (['X'], [('ty', 'GraduateStudent', '?X'), ('tr', 'takesCourse', '?X', D0 + '/GraduateCourse0')]),
    2: (['X', 'Y', 'Z'], [('ty', 'GraduateStudent', '?X'), ('ty', 'University', '?Y'), ('ty', 'Department', '?Z'),
                          ('tr', 'memberOf', '?X', '?Z'), ('tr', 'subOrganizationOf', '?Z', '?Y'),
                          ('tr', 'undergraduateDegreeFrom', '?X', '?Y')]),
    3: (['X'], [('ty', 'Publication', '?X'), ('tr', 'publicationAuthor', '?X', D0 + '/AssistantProfessor0')]),
    4: (['X', 'Y1', 'Y2', 'Y3'], [('ty', 'Professor', '?X'), ('tr', 'worksFor', '?X', D0),
                                  ('tr', 'name', '?X', '?Y1'), ('tr', 'emailAddress', '?X', '?Y2'),
                                  ('tr', 'telephone', '?X', '?Y3')]),
    5: (['X'], [('ty', 'Person', '?X'), ('tr', 'memberOf', '?X', D0)]),
    6: (['X'], [('ty', 'Student', '?X')]),
    7: (['X', 'Y'], [('ty', 'Student', '?X'), ('ty', 'Course', '?Y'), ('tr', 'takesCourse', '?X', '?Y'),
                     ('tr', 'teacherOf', D0 + '/AssociateProfessor0', '?Y')]),
    8: (['X', 'Y', 'Z'], [('ty', 'Student', '?X'), ('ty', 'Department', '?Y'), ('tr', 'memberOf', '?X', '?Y'),
                          ('tr', 'subOrganizationOf', '?Y', U0), ('tr', 'emailAddress', '?X', '?Z')]),
    9: (['X', 'Y', 'Z'], [('ty', 'Student', '?X'), ('ty', 'Faculty', '?Y'), ('ty', 'Course', '?Z'),
                          ('tr', 'advisor', '?X', '?Y'), ('tr', 'teacherOf', '?Y', '?Z'),
                          ('tr', 'takesCourse', '?X', '?Z')]),
    10: (['X'], [('ty', 'Student', '?X'), ('tr', 'takesCourse', '?X', D0 + '/GraduateCourse0')]),
    11: (['X'], [('ty', 'ResearchGroup', '?X'), ('tr', 'subOrganizationOf', '?X', U0)]),
    12: (['X', 'Y'], [('ty', 'Chair', '?X'), ('ty', 'Department', '?Y'), ('tr', 'worksFor', '?X', '?Y'),
                      ('tr', 'subOrganizationOf', '?Y', U0)]),
    13: (['X'], [('ty', 'Person', '?X'), ('tr', 'hasAlumnus', U0, '?X')]),
    14: (['X'], [('ty', 'UndergraduateStudent', '?X')]),
}


def closure(triples):
    """Semi-naive OWL 2 RL closure.  Returns (ty, tr): ty a set of (C, x),
    tr a set of (P, s, o)."""
    sc, sp, inv, rng, dom = defaultdict(set), defaultdict(set), defaultdict(set), defaultdict(set), defaultdict(set)
    for a, b in SC: sc[a].add(b)
    for a, b in SP: sp[a].add(b)
    for a, b in INV: inv[a].add(b)
    for a, b in DOM: dom[a].add(b)
    for a, b in RNG: rng[a].add(b)
    trans = set(TRANS)
    ty, tr = set(), set()
    tr_s = defaultdict(set)        # (P, s) -> o
    tr_o = defaultdict(set)        # (P, o) -> s
    ty_x = defaultdict(set)        # x -> C
    dty, dtr = set(), set()
    for s, p, o, lit in triples:
        if p == 'type':
            dty.add((o, s))
        else:
            dtr.add((p, s, o))
    ex_by_a, ex_by_p, ex_by_d = defaultdict(list), defaultdict(list), defaultdict(list)
    for e in EX:
        ex_by_a[e[1]].append(e); ex_by_p[e[2]].append(e); ex_by_d[e[3]].append(e)
    while dty or dtr:
        for c, x in dty:
            ty.add((c, x)); ty_x[x].add(c)
        for p, s, o in dtr:
            tr.add((p, s, o)); tr_s[(p, s)].add(o); tr_o[(p, o)].add(s)
        nty, ntr = set(), set()
        for c, x in dty:
            for d in sc[c]: nty.add((d, x))
            for C, A, P, D in ex_by_a[c]:
                if any(D in ty_x[y] for y in tr_s[(P, x)]): nty.add((C, x))
            for C, A, P, D in ex_by_d[c]:
                for s in tr_o[(P, x)]:
                    if A is None or A in ty_x[s]: nty.add((C, s))
        for p, s, o in dtr:
            for q in sp[p]: ntr.add((q, s, o))
            for q in inv[p]: ntr.add((q, o, s))
            if p in trans:
                for z in tr_s[(p, o)]: ntr.add((p, s, z))
                for a in tr_o[(p, s)]: ntr.add((p, a, o))
            for c in dom[p]: nty.add((c, s))
            for c in rng[p]: nty.add((c, o))
            for C, A, P, D in ex_by_p[p]:
                if (A is None or A in ty_x[s]) and D in ty_x[o]: nty.add((C, s))
        dty, dtr = nty - ty, ntr - tr
    return ty, tr


class Index:
    def __init__(self, ty, tr):
        self.ty = set(ty)
        self.tys = defaultdict(list)
        for c, x in ty: self.tys[c].append(x)
        self.trs, self.tr_s, self.tr_o = defaultdict(list), defaultdict(list), defaultdict(list)
        self.tr = set(tr)
        for p, s, o in tr:
            self.trs[p].append((s, o)); self.tr_s[(p, s)].append(o); self.tr_o[(p, o)].append(s)


def answer(query, ty, tr, ix=None):
    """Distinct answers of a conjunctive query: a backtracking join that always
    extends by the remaining atom with the most bound terms."""
    sel, conj = query
    ix = ix or Index(ty, tr)
    isvar = lambda t: t.startswith('?')
    val = lambda t, b: b.get(t) if isvar(t) else t
    out = set()

    def bound_count(atom, b):
        terms = atom[2:] if atom[0] == 'ty' else atom[2:]
        return sum(1 for t in terms if not isvar(t) or t in b)

    def solve(rest, b):
        if not rest:
            out.add(tuple(b['?' + v] for v in sel)); return
        k = max(range(len(rest)), key=lambda i: (bound_count(rest[i], b), rest[i][0] == 'tr'))
        atom, rest2 = rest[k], rest[:k] + rest[k + 1:]
        if atom[0] == 'ty':
            c, t = atom[1], atom[2]
            v = val(t, b)
            if v is not None:
                if (c, v) in ix.ty: solve(rest2, b)
            else:
                for x in ix.tys[c]: solve(rest2, {**b, t: x})
        else:
            _, p, s, o = atom
            vs, vo = val(s, b), val(o, b)
            if vs is not None and vo is not None:
                if (p, vs, vo) in ix.tr: solve(rest2, b)
            elif vs is not None:
                for y in ix.tr_s[(p, vs)]: solve(rest2, {**b, o: y})
            elif vo is not None:
                for y in ix.tr_o[(p, vo)]: solve(rest2, {**b, s: y})
            else:
                for a, c in ix.trs[p]:
                    if s == o and a != c: continue
                    solve(rest2, {**b, s: a, o: c})

    solve(list(conj), {})
    return out


def read_reference(path):
    lines = Path(path).read_text().splitlines()
    if not lines or lines[0].startswith('NO ANSWERS'):
        return set()
    return {tuple(l.split('\t')) if '\t' in l else tuple(l.split()) for l in lines[1:] if l.strip()}


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == '--check':
        ref = Path(sys.argv[2])
        triples = uba.generate(1)
        ty, tr = closure(triples)
        print(f'LUBM(1,0): {len(triples)} triples; closure {len(ty)} types, {len(tr)} property facts')
        ok_all = True
        ix = Index(ty, tr)
        for q in sorted(QUERIES):
            got = answer(QUERIES[q], ty, tr, ix)
            want = read_reference(ref / f'answers_query{q}.txt')
            ok = got == want
            ok_all &= ok
            print(f'Q{q:<2} answers {len(got):6d} reference {len(want):6d} {"same set" if ok else "DIFFERENT"}')
        print('ALL SAME' if ok_all else 'SOME DIFFER')


if __name__ == '__main__':
    main()
