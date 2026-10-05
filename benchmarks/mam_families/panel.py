#!/usr/bin/env python3
"""Generate correctness oracles and HE/PeTTa/JeTTa workload adapters.

The MAM families retain their independently implemented evaluator. HE's
unknown-call result is syntax rather than relational failure, so HM lookup
gets an explicit empty-environment failure clause. This adapter is distinct
from the original PeTTa program. JeTTa uses Int annotations, identifier
renaming and an explicit print boundary; compilation remains a separate phase.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from families import (HM_RENAMED, PARSE_RENAMED, PROOF_RENAMED, E, V, answer,
                      affine_case, hm_case, mutation_case, parse_case, proof_case,
                      source_text)
from gen import circulant, path2_witnesses

ROOT = Path(__file__).resolve().parents[2]


def sha(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def fib(n: int) -> int:
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a


JETTA_TYPES = {
    'fibonacci': [('fib', 'Int Int')],
    'interpreter': [('lookup', 'Atom Atom Int'), ('ev', 'Atom Atom Int'),
                    ('interpreter-once', 'Int Int'), ('interpreter-loop', 'Int Int Int'),
                    ('Lit', 'Int Atom'), ('Var', 'Atom Atom'), ('Add', 'Atom Atom Atom'),
                    ('Mul', 'Atom Atom Atom'), ('Let', 'Atom Atom Atom Atom'),
                    ('Cons', 'Atom Atom Atom'), ('Bind', 'Atom Int Atom')],
    'differentiation': [('d', 'Atom Int Atom'), ('eval-poly', 'Atom Int'),
                        ('derivative-value', 'Atom Int Int'),
                        ('differentiation-loop', 'Int Int Int'),
                        ('Lit', 'Int Atom'), ('Plus', 'Atom Atom Atom'),
                        ('Mul', 'Atom Atom Atom')],
    'synthesis': [('sq', 'Atom Int Int'), ('len', 'Atom Int'),
                  ('gen', 'Int Int Atom'), ('synth-eval', 'Atom Atom Int Int'),
                  ('check', 'Atom Atom Int Bool'), ('solve', 'Atom Int Int Atom'),
                  ('synthesis-once', 'Int Int'), ('synthesis-loop', 'Int Int Int'),
                  ('C', 'Int Atom'), ('X', 'Int Atom'), ('Bin', 'Atom Atom Atom Atom')],
    'backward_chaining': [('deduce', 'Atom Atom'), ('and', 'Atom Atom Atom'),
                          ('deduction-once', 'Int Int'), ('deduction-loop', 'Int Int Int')],
}


def jetta_source(name: str, text: str, size: int) -> str:
    # A ':' inside an identifier is tokenized separately by this JeTTa parser.
    text = text.replace('mam:', 'mam_')
    # JeTTa's declared constructor fields retain syntax. Force the coefficient
    # before constructing a typed literal, rather than relying on later demand.
    text = text.replace('(Lit (+ 1 $zero))',
                        '(let $coefficient (+ 1 $zero) (Lit $coefficient))')
    text = text.replace('(let $zero (* 0 $iteration)\n     (superpose (N (C (+ 1 $zero)) (C 2) (X 1) (X 2))))',
                        '(let $zero (* 0 $iteration)\n     (let $constant (+ 1 $zero)\n       (superpose (N (C $constant) (C 2) (X 1) (X 2)))))')
    text = text.replace('(if (== (mam_deduce (Evaluation (mortal $subject))) T)\n           1\n           0)',
                        '(let $deduction (mam_deduce (Evaluation (mortal $subject)))\n         (if (== $deduction T) 1 0))')
    declarations = []
    for symbol, types in JETTA_TYPES[name]:
        if symbol not in {'Lit', 'Var', 'Add', 'Mul', 'Let', 'Cons', 'Bind',
                          'Plus', 'C', 'X', 'Bin'}:
            symbol = 'mam_' + symbol
        declarations.append(f'(: {symbol} (-> {types}))')
    if name in {'synthesis','backward_chaining'}:
        # The JVM adapter publishes the actual per-iteration witnesses. It
        # avoids the pinned compiler's list/scalar and JIT observer failures.
        # This is a different observer from the HE/PeTTa checksum, explicitly
        # declared in the manifest; it is not a runtime speed ratio for the same
        # driver. All solutions still pass through the original search kernel.
        pivot='(= (mam_synthesis-once ' if name=='synthesis' else '(= (mam_deduction-once '
        text=text.split(pivot)[0]
        query=(lambda i:f'!(println (mam_solve fib 1 {i}))\n') if name=='synthesis' else (lambda i:'!(println (mam_deduce (Evaluation (mortal Plato))))\n')
        return '\n'.join(declarations)+'\n'+text+''.join(query(i) for i in range(size))
    body, query = text.rsplit('!', 1)
    return '\n'.join(declarations) + '\n' + body + '!(println ' + query.strip() + ')\n'


PATRICK = """\
; Patrick Hammer's higher-order iterator, with an explicit tagged state.
(= (panel:iterate $i $n $state $step)
   (if (== $n 0) $state
       (panel:iterate (+ $i 1) (- $n 1) ($step $i $state) $step)))
(= (panel:quad-step $dummy (Q $t $i $sum))
   (let $next-sum (+ $sum (* $t $i))
     (if (== $i $t)
         (let $next-t (+ $t 1) (Q $next-t 1 $next-sum))
         (let $next-i (+ $i 1) (Q $t $next-i $next-sum)))))
(= (panel:quad-result (Q $t $i $sum)) $sum)
(= (panel:fib-step $i (F $a $b))
   (let $sum (+ $a $b) (F $b $sum)))
(= (panel:fib-result (F $a $b)) $a)
"""


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('out', type=Path)
    ap.add_argument('--scale', choices=['smoke', 'measure', 'paper'], default='measure')
    ap.add_argument('--only', help='comma-separated families')
    ap.add_argument('--mork-act', type=Path, help='also generate ACT preparation and warm-attach queries')
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    selected = set(args.only.split(',')) if args.only else None
    known={'jetta','patrick','hm','parse','proof','affine','mutation','graph','bio','bio-index','bio-mork'}
    if selected and selected-known:
        ap.error('unknown families: '+', '.join(sorted(selected-known)))
    rows = []

    def emit(name, family, role, sources, expected, params=None, contract='exact-stream', notes='', preamble=None, variant_expected=None, variant_contract=None, dependencies=None):
        if selected and family not in selected:
            return
        exp = ''.join(v + '\n' for v in expected)
        (out/f'{name}.expected').write_text(exp)
        dialects = {}
        for dialect, text in sources.items():
            folder = out/dialect
            folder.mkdir(exist_ok=True)
            file = folder/f'{name}.metta'
            file.write_text(text)
            dialects[dialect] = {'source': str(file.relative_to(out)), 'sha256': sha(text)}
            if preamble and dialect in preamble:
                dialects[dialect]['preamble']=preamble[dialect]
            if variant_contract and dialect in variant_contract:
                dialects[dialect]['contract']=variant_contract[dialect]
            if dependencies and dialect in dependencies:
                dialects[dialect]['dependencies']=dependencies[dialect]
            if variant_expected and dialect in variant_expected:
                alternate=''.join(v+'\n' for v in variant_expected[dialect])
                alt_path=folder/f'{name}.expected'
                alt_path.write_text(alternate)
                dialects[dialect].update(expected=str(alt_path.relative_to(out)),expected_sha256=sha(alternate))
        rows.append(dict(case=name, family=family, role=role, params=params or {},
                         answers=len(expected), expected=f'{name}.expected',
                         expected_sha256=sha(exp), contract=contract,
                         dialects=dialects, notes=notes))
        print(f'{name}: {len(expected)} answers', flush=True)

    sizes = {
        'smoke': {'fibonacci': [15], 'interpreter': [2], 'differentiation': [2],
                  'synthesis': [1], 'backward_chaining': [3]},
        'measure': {'fibonacci': [20, 30], 'interpreter': [100, 10000],
                    'differentiation': [100, 10000], 'synthesis': [2, 40],
                    'backward_chaining': [100, 1000]},
        'paper': {'fibonacci': [30], 'interpreter': [1000000],
                  'differentiation': [1000000], 'synthesis': [40],
                  'backward_chaining': [1000]},
    }[args.scale]
    if not selected or 'jetta' in selected:
        for name, ns in sizes.items():
            template = (ROOT/'benchmarks/mam_jetta'/f'{name}.metta.in').read_text()
            for n in ns:
                text = template.replace('@N@', str(n))
                value = fib(n) if name=='fibonacci' else n * {'interpreter':144, 'differentiation':27}.get(name,1)
                alt=None
                note='Same kernel; authored repetition driver; JeTTa compile separately.'
                if name in {'synthesis','backward_chaining'}:
                    alt={'jetta':(['(Bin + (X 1) (X 2))','(Bin + (X 2) (X 1))']*n if name=='synthesis' else ['T']*n)}
                    note+=' JeTTa publishes per-iteration witnesses, HE/PeTTa a checksum; these driver timings are not direct speed ratios.'
                emit(f'jetta_{name}_{n}', 'jetta', 'size',
                     {'he':text, 'petta':text, 'jetta':jetta_source(name,text,n)},
                     [str(value)], {'size':n}, notes=note,variant_expected=alt)
                if alt:
                    pivot='(= (mam:synthesis-once ' if name=='synthesis' else '(= (mam:deduction-once '
                    kernel=text.split(pivot)[0]
                    queries=''.join(f'!(mam:solve fib 1 {i})\n' if name=='synthesis'
                                    else '!(mam:deduce (Evaluation (mortal Plato)))\n'
                                    for i in range(n))
                    emit(f'jetta_{name}_witnesses_{n}', 'jetta', 'witness-observer',
                         {'he':kernel+queries,'petta':kernel+queries,
                          'jetta':jetta_source(name,text,n)}, alt['jetta'],
                         {'size':n}, notes='Matched per-iteration witness observer on all engines; ordered duplicates retained.')

    if not selected or 'patrick' in selected:
        for n in ([10] if args.scale=='smoke' else [100,1000]):
            count = n*(n+1)//2
            expected = sum(t*t*(t+1)//2 for t in range(1,n+1))
            text = PATRICK + f'!(panel:quad-result (panel:iterate 0 {count} (Q 1 1 0) panel:quad-step))\n'
            emit(f'patrick_quad_{n}', 'patrick', 'size', {'he':text,'petta':text},
                 [str(expected)], {'size':n,'iterations':count}, notes='Tagged-state adapter of patrick_iterate_quad; same higher-order step calls.')
        for n in ([10] if args.scale=='smoke' else [40]):
            text = PATRICK + f'!(panel:fib-result (panel:iterate 0 {n} (F 0 1) panel:fib-step))\n'
            emit(f'patrick_fib_{n}', 'patrick', 'size', {'he':text,'petta':text}, [str(fib(n))], {'size':n})

    def equation_case(name, family, role, built, params):
        p, q, header = built
        expected, _counts = answer(p,q)
        text = source_text(p,q,'')
        he = text
        if family=='hm':
            lookup = 'find-r' if 'renamed' in name else 'lookup'
            nil = 'empty-env-r' if 'renamed' in name else 'nil'
            he = f'(= ({lookup} {nil} $name) (empty))\n' + text
        emit(name, family, role, {'he':he,'petta':header+text}, expected, params,
             notes='HE HM adapter explicitly fails lookup in the empty environment.' if family=='hm' else '',
             preamble={'petta':['$_0']} if header else None)

    if not selected or 'hm' in selected:
        depths = [3] if args.scale=='smoke' else [8,12]
        for k in depths:
            equation_case(f'hm_k{k}', 'hm', 'size', hm_case(k), {'depth':k})
        k = depths[0]
        equation_case(f'hm_renamed_k{k}', 'hm', 'holdout-renamed', hm_case(k,HM_RENAMED), {'depth':k})
        equation_case(f'hm_nearmiss_k{k}', 'hm', 'near-miss', hm_case(k,near_miss=True), {'depth':k})
    if not selected or 'parse' in selected:
        scales = [(2,2)] if args.scale=='smoke' else [(3,3),(4,3)]
        for d,w in scales:
            equation_case(f'parse_d{d}w{w}', 'parse', 'size', parse_case(d,w), {'depth':d,'width':w})
        d,w=scales[0]
        equation_case(f'parse_renamed_d{d}w{w}', 'parse', 'holdout-renamed', parse_case(d,w,PARSE_RENAMED), {'depth':d,'width':w})
        equation_case(f'parse_nearmiss_d{d}w{w}', 'parse', 'near-miss', parse_case(d,w,near_miss=True), {'depth':d,'width':w})
    if not selected or 'proof' in selected:
        sizes = [15] if args.scale=='smoke' else [17,19]
        for s in sizes:
            equation_case(f'proof_loowoz_s{s}', 'proof', 'size', proof_case(s), {'size':s})
        # axiom-instance witness plus genuinely failing goal, even in smoke mode.
        p,q,header=proof_case(3)
        q=E('obc',3,E(':',V('prf'),E('→','pa',E('→','pb','pa'))))
        equation_case('proof_axiom_s3','proof','positive', (p,q,header), {'size':3})
        pa,pb,pc='𝜑','𝜓','𝜒'
        def imp(a,b): return E('→',a,b)
        theorems=[('jarr',13,imp(imp(imp(pa,pb),pc),imp(pb,pc))),
                  ('pm2_27',13,imp(pa,imp(imp(pa,pb),pb))),
                  ('imim1',15,imp(imp(pa,pb),imp(imp(pb,pc),imp(pa,pc))))]
        for theorem,s,goal in theorems:
            p,_,header=proof_case(s)
            q=E('obc',s,E(':',V('prf'),goal))
            equation_case(f'proof_{theorem}_s{s}','proof','positive',(p,q,header),{'size':s})
        s = 11 if args.scale=='smoke' else 15
        equation_case(f'proof_unprovable_s{s}','proof','near-miss',proof_case(s,goal='unprovable'), {'size':s})
        if args.scale!='smoke':
            equation_case('proof_renamed_s17','proof','holdout-renamed', proof_case(17,PROOF_RENAMED), {'size':17})
    if not selected or 'affine' in selected:
        for count in ([100] if args.scale=='smoke' else [10000,100000]):
            facts,query,expected=affine_case(count,'alt3')
            he=facts+'!(let $items (collapse (match &self (obs $k $v) $v))\n'
            he+='   (foldl-atom $items 0 $acc $item (upd $item $acc)))\n'
            emit(f'affine_alt3_n{count}','affine','size',
                 {'he':he,'petta':facts+'!'+query+'\n'},expected,{'count':count},
                 notes='PeTTa foldall; HE materializes the ordered pure match stream before foldl-atom.')
    if not selected or 'mutation' in selected:
        text=''.join(f'(= (panel:choice) {i})\n' for i in [1,2,2])
        text+='!(panel:choice)\n'
        text+='!(let $_ (add-atom &self (= (panel:choice) 3)) ())\n'
        text+='!(panel:choice)\n'
        emit('mutation_staged_choices','mutation','effect-control',{'he':text,'petta':text},
             ['1','2','2','()','1','2','2','3'],notes='A fresh call sees the added rewrite; the completed earlier call and its duplicate occurrences remain unchanged. Interleaved consumer mutation is a separate dialect diagnostic.')
        for depth in ([3] if args.scale=='smoke' else [4,6]):
            prog,query,_=mutation_case(depth,3,2)
            live,_=answer(prog,query)
            snapshot,_=answer(prog,E('num',depth))
            held=E('let',V('answers'),E('collapse',E('num',depth)),
                   E('let',V('v'),E('superpose',V('answers')),query[3]))
            emit(f'mutation_frontier_n{depth}','mutation','size',
                 {'petta':source_text(prog,query,''),'he':source_text(prog,held,'')},live,
                 {'depth':depth,'digits':3},variant_expected={'he':snapshot},
                 variant_contract={'he':'occurrence-bag'},
                 notes='PeTTa preserves live logical-update choices. The HE adapter explicitly completes collapse before running the consumer; its result observer is a bag. The different policies have separate independent oracles and are not same-work speed ratios.')
    if not selected or 'graph' in selected:
        for n,d in ([(20,2)] if args.scale=='smoke' else [(300,4),(1500,8)]):
            edges=circulant(n,d)
            facts=''.join(f'(edge {a} {b})\n' for a,b in edges)
            witnesses=path2_witnesses(edges)
            query='!(match &self (, (edge $x $y) (edge $y $z)) ($x $y $z))\n'
            emit(f'graph_path2_n{n}d{d}','graph','size',{'he':facts+query,'petta':facts+query},witnesses,{'nodes':n,'degree':d})
            miss='!(match &self (, (edge $x $y) (edge $y missing-node)) ($x $y))\n'
            if n==20 or n==300:
                emit(f'graph_miss_n{n}d{d}','graph','near-miss',{'he':facts+miss,'petta':facts+miss},[],{'nodes':n,'degree':d})

    if not selected or selected & {'bio','bio-index','bio-mork'}:
        data=ROOT/'benchmarks/data/kb_eqtl_gene_links.metta'
        lines=[s.strip() for s in data.read_text().splitlines() if s.startswith('(')]
        # A real-data prefix is a separate scaling rung, never the full corpus.
        for count in ([1000] if args.scale=='smoke' else [10000,len(lines)]):
            facts=lines[:count]
            query=''
            expected=[]
            selectors=[('Q1','eqtl-link','rs10000544',None),
                       ('Q2','eqtl-link','rs10000620',None),
                       ('Q3','eqtl-link',None,'ensg00000138660'),
                       ('Q4','tissue-class','ensg00000138660',None),
                       ('Q5','effect-class','ensg00000138660',None)]
            for label,rel,a,b in selectors:
                pattern=f'({rel} {a or "$x"} {b or "$x"})'
                query+=f'!(match &self {pattern} ({label} $x))\n'
                for fact in facts:
                    rr,aa,bb=fact[1:-1].split()
                    if rr==rel and (a is None or aa==a) and (b is None or bb==b):
                        expected.append(f'({label} {aa if a is None else bb})')
            # Observe each binding, not just a count; preserve per-query order.
            text='\n'.join(facts)+'\n'+query
            emit(f'bio_eqtl_{count}','bio','size',{'he':text,'petta':text},expected,
                 {'facts':count,'full_facts':len(lines),'dataset_sha256':sha(data.read_text())},
                 notes='Actual GTEx eQTL corpus; ordered five-query binding stream.')
            inverse=[]
            for fact in facts:
                rel,a,b=fact[1:-1].split()
                if rel=='eqtl-link': inverse.append(f'(eqtl-by-gene {b} {a})')
            indexed='\n'.join(facts+inverse)+'\n'+query.replace(
                '(eqtl-link $x ensg00000138660)',
                '(eqtl-by-gene ensg00000138660 $x)')
            emit(f'bio_eqtl_indexed_{count}','bio-index','derived-index',
                 {'he':indexed,'petta':indexed},expected,
                 {'facts':count,'derived_index_facts':len(inverse),
                  'dataset_sha256':sha(data.read_text())},
                 notes='Explicit gene-to-SNP index, included in cold source loading on every engine. A separate storage adapter, checked against the same binding oracle; the direct-query fixture remains separate.')
            if count==len(lines):
                prefix='!(import! &self mork)\n'
                load='!(bind! &kb (mork:new-space))\n'
                load+='!(let $_ (collapse (match &self (eqtl-link $a $b) (mork:add-atom &kb (eqtl-link $a $b)))) ())\n'
                for rel in ['tissue-class','effect-class']:
                    load+=f'!(let $_ (collapse (match &self ({rel} $a $b) (mork:add-atom &kb ({rel} $a $b)))) ())\n'
                text='\n'.join(facts)+'\n'+prefix+load+query.replace('match &self','mork:match &kb')
                emit(f'bio_mork_source_{count}','bio-mork','storage',{'cetta-mork':text},expected,
                     {'facts':count,'dataset_sha256':sha(data.read_text())},contract='occurrence-bag',
                     notes='MORK has set storage and its own traversal order; input facts are unique. Run CeTTa HE with --quiet.')
                rules=''
                for label,rel,a,b in selectors:
                    pattern=f'({rel} {a or "$x"} {b or "$x"})'
                    rules+=f'(exec (0 {label}) (, {pattern}) (, ({label} $x)))\n'
                emit(f'bio_native_mork_{count}','bio-mork','native-storage',
                     {'mork':'\n'.join(facts)+'\n'+rules},expected,
                     {'facts':count,'dataset_sha256':sha(data.read_text()),
                      'projection_heads':[s[0] for s in selectors]},contract='occurrence-bag',
                     notes='Native MORK execution; project the five labeled query relations from the final store. Full-process timing also includes dumping the store, unlike CeTTa binding-only output.')
                if args.mork_act:
                    act=json.dumps(str(args.mork_act.resolve()))
                    preparation=out/'cetta-mork'/'prepare_eqtl_act.metta'
                    preparation.write_text(text[:text.index(query.replace('match &self','mork:match &kb'))]
                                           +f'!(mork:dump! &kb {act})\n')
                    warm=prefix+f'!(bind! &kb (mork:open-act {act}))\n'+query.replace('match &self','mork:match &kb')
                    emit(f'bio_mork_act_{count}','bio-mork','warm-storage',{'cetta-mork':warm},expected,
                         {'facts':count,'dataset_sha256':sha(data.read_text()),
                          'preparation':str(preparation.relative_to(out)),
                          'preparation_sha256':sha(preparation.read_text())},contract='occurrence-bag',
                         dependencies={'cetta-mork':[str(args.mork_act.resolve())]},
                         notes='Prepare the ACT artifact separately using the generated preparation program. Timing includes process start, attachment and all five queries; it excludes preparation.')

    manifest=out/'manifest.json'
    if selected and manifest.exists():
        rows=[r for r in json.loads(manifest.read_text()) if r['family'] not in selected]+rows
    manifest.write_text(json.dumps(rows,indent=2,ensure_ascii=False)+'\n')


if __name__=='__main__':
    main()
