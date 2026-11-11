#!/usr/bin/env python3
"""Knowledge-graph analytics in the shape of MORK's FlyBase workload.

Adam Vandervorst's own real-data case is FlyBase loaded into MORK (about a
billion atoms): triples (SPO subject predicate object), node attributes
(NKV node key value), long identifiers with shared prefixes (FBgn0000123,
FBtr..., FBpp...), indexes built inside MM2 (gene_name_of, chr_of, the
reverse view OPS), and multi-way joins anchored on a gene name or a
chromosome (kernel/src/main.rs, work_mm2).  This generator produces a
synthetic graph with the same schema and degree structure:

  genes      (NKV g gene_name NAME) (NKV g chr CHR) (NKV g start POS)
  gene ->    (SPO g transcribed_to tr) and (SPO tr transcribed_from g), 1-3 per gene
  tr ->      (SPO tr translates_to pp)
  pp <->     (SPO pp interacts_with pp2), symmetric, a few per protein
  gene ->    (SPO g genes_pathways pathway), 1-3 pathways per gene

and runs
  res2     anchored 5-way join (Adam's P5): a named gene's transcripts,
           proteins, interaction partners and pathways;
  res1     anchored 4-way join (P4-like): genes on one chromosome and the
           proteins their products interact with;
  chrgenes genes per chromosome (count);
  pwint    per pathway, the ordered pairs of interacting proteins whose two
           genes are both in the pathway: a 7-way cyclic join, counted.

MM2 materializes the indexes it needs as facts, as Adam's script does
(gene_name_of, chr_of, OPS).  The MeTTa program materializes the same reverse
view OPS: without it, CeTTa's on-demand indexes did not serve the
object-bound probes (kg_500: 1.54 s against 0.05 s with the view).  The
compared relations are the four results.

Size knob: G genes (about 16 facts per gene).
usage: gen_kg.py G [--seed 0]
"""
import argparse, json, random
from collections import defaultdict
from pathlib import Path

CHRS = ['chr2L', 'chr2R', 'chr3L', 'chr3R', 'chr4', 'chrX', 'chrY']

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('G', type=int)
    ap.add_argument('--seed', type=int, default=0)
    ap.add_argument('--outdir', default=str(Path(__file__).resolve().parent / 'gen'))
    a = ap.parse_args()
    rng = random.Random(a.seed)
    G = a.G
    npw = max(4, G // 25)
    genes = [f"FBgn{1000000 + 7 * i:07d}" for i in range(G)]
    names = {}
    for i, g in enumerate(genes):
        names[g] = f"CG{10000 + i}"
    names[genes[G // 3]] = 'BRCA2'
    names[genes[G // 2]] = 'TP73-AS1'
    chr_of = {g: rng.choices(CHRS, [22, 21, 24, 27, 1, 20, 1])[0] for g in genes}
    spo, nkv = [], []
    trs, pps = defaultdict(list), {}
    tcount = pcount = 0
    for g in genes:
        nkv.append((g, 'gene_name', names[g]))
        nkv.append((g, 'chr', chr_of[g]))
        nkv.append((g, 'start', str(rng.randrange(10 ** 7))))
        for _ in range(rng.choice([1, 1, 2, 2, 3])):
            tr = f"FBtr{2000000 + 3 * tcount:07d}"; tcount += 1
            pp = f"FBpp{3000000 + 5 * pcount:07d}"; pcount += 1
            trs[g].append(tr); pps[tr] = pp
            spo.append((g, 'transcribed_to', tr)); spo.append((tr, 'transcribed_from', g))
            spo.append((tr, 'translates_to', pp))
        for pw in rng.sample(range(npw), rng.choice([1, 1, 2, 3])):
            spo.append((g, 'genes_pathways', f"R-DME-{100000 + pw}"))
    allpp = list(pps.values())
    inter = set()
    for p in allpp:
        for _ in range(rng.choice([0, 1, 1, 2, 3])):
            q = rng.choice(allpp)
            if q != p:
                inter.add((p, q)); inter.add((q, p))
    spo.extend((p, 'interacts_with', q) for p, q in sorted(inter))
    # oracle
    S = defaultdict(set)
    for s, p, o in spo:
        S[(s, p)].add(o)
    R = defaultdict(set)
    for s, p, o in spo:
        R[(o, p)].add(s)
    anchor = 'BRCA2'
    g0 = [g for g in genes if names[g] == anchor]
    res2 = {(x, y, z, p, q) for x in g0 for y in S[(x, 'transcribed_to')] for z in S[(y, 'translates_to')]
            for p in R[(z, 'interacts_with')] for q in S[(x, 'genes_pathways')]}
    res1 = {(x, w) for x in genes if chr_of[x] == 'chrX' for y in S[(x, 'transcribed_to')]
            for z in S[(y, 'translates_to')] for w in S[(z, 'interacts_with')]}
    chrgenes = defaultdict(int)
    for g in genes:
        chrgenes[chr_of[g]] += 1
    pwint = defaultdict(set)
    for g1 in genes:
        for pw in S[(g1, 'genes_pathways')]:
            for t1 in S[(g1, 'transcribed_to')]:
                for p1 in S[(t1, 'translates_to')]:
                    for p2 in S[(p1, 'interacts_with')]:
                        for t2 in R[(p2, 'translates_to')]:
                            for g2 in R[(t2, 'transcribed_to')]:
                                if pw in S[(g2, 'genes_pathways')]:
                                    pwint[pw].add((p1, p2))
    name = f'kg_{G}'
    out = Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    facts = ''.join(f"(NKV {s} {k} {v})\n" for s, k, v in nkv) + ''.join(f"(SPO {s} {p} {o})\n" for s, p, o in spo)
    mm2 = f"""; {name}: FlyBase-shaped knowledge graph, {len(nkv) + len(spo)} facts
{facts}
; indexes, as MM2 builds them for FlyBase
(exec (0 0) (, (NKV $x gene_name $y)) (, (gene_name_of $y $x)))
(exec (0 1) (, (NKV $x chr $y)) (, (chr_of $y $x)))
(exec (0 2) (, (SPO $s $p $o)) (, (OPS $o $p $s)))
; queries
(exec (1 0) (, (gene_name_of {anchor} $x) (SPO $x transcribed_to $y) (SPO $y translates_to $z)
               (OPS $z interacts_with $p) (SPO $x genes_pathways $q))
            (, (res2 $x $y $z $p $q)))
(exec (1 1) (, (chr_of chrX $x) (SPO $x transcribed_to $y) (SPO $y translates_to $z) (SPO $z interacts_with $w))
            (, (res1 $x $w)))
(exec (1 2) (, (chr_of $c $g)) (O (count (chrgenes $c $n) $n $g)))
(exec (1 3) (, (SPO $g1 genes_pathways $pw) (SPO $g1 transcribed_to $t1) (SPO $t1 translates_to $p1)
               (SPO $p1 interacts_with $p2) (OPS $p2 translates_to $t2) (OPS $t2 transcribed_to $g2)
               (SPO $g2 genes_pathways $pw))
            (O (count (pwint $pw $n) $n ($p1 $p2))))
"""
    (out / f'{name}.mm2').write_text(mm2)
    metta = f"""; {name}: the same queries in MeTTa (PeTTa dialect), with the same reverse view
{facts}
!(bind! &t (new-space set))
; the reverse view, as MM2 builds it
!(bind! &o (new-space set))
!(size-atom (collapse (match &self (SPO $s $p $x) (add-atom &o (OPS $x $p $s)))))
!(size-atom (collapse (match &self (NKV $x gene_name {anchor}) (match &self (SPO $x transcribed_to $y)
     (match &self (SPO $y translates_to $z) (match &o (OPS $z interacts_with $p)
       (match &self (SPO $x genes_pathways $q) (add-atom &t (res2 $x $y $z $p $q)))))))))
!(size-atom (collapse (match &self (NKV $x chr chrX) (match &self (SPO $x transcribed_to $y)
     (match &self (SPO $y translates_to $z) (match &self (SPO $z interacts_with $w) (add-atom &t (res1 $x $w))))))))
!(bind! &g (new-space set))
!(size-atom (collapse (match &self (NKV $g chr $c) (add-atom &g (chr-g $c)))))
!(size-atom (collapse (match &g (chr-g $c) (let $n (size-atom (collapse (match &self (NKV $g chr $c) t))) (add-atom &t (chrgenes $c $n))))))
!(bind! &w (new-space set))
!(size-atom (collapse (match &self (SPO $g1 genes_pathways $pw) (match &self (SPO $g1 transcribed_to $t1)
     (match &self (SPO $t1 translates_to $p1) (match &self (SPO $p1 interacts_with $p2)
       (match &o (OPS $p2 translates_to $t2) (match &o (OPS $t2 transcribed_to $g2)
         (match &self (SPO $g2 genes_pathways $pw) (let $_ (add-atom &w (pw-w $pw $p1 $p2)) (add-atom &g (pw-g $pw))))))))))))
!(size-atom (collapse (match &g (pw-g $pw) (let $n (size-atom (collapse (match &w (pw-w $pw $p1 $p2) t))) (add-atom &t (pwint $pw $n))))))
!(match &t (chrgenes $c $n) (chrgenes $c $n))
!(match &t (pwint $c $n) (pwint $c $n))
!(size-atom (collapse (match &t (res2 $a $b $c $d $e) t)))
!(size-atom (collapse (match &t (res1 $a $b) t)))
!(size-atom (collapse (match &t (chrgenes $a $b) t)))
!(size-atom (collapse (match &t (pwint $a $b) t)))
"""
    (out / f'{name}.metta').write_text(metta)
    (out / f'{name}.heads').write_text('res2 res1 chrgenes pwint\n')
    (out / f'{name}.values').write_text('chrgenes pwint\n')
    (out / f'{name}.expect.json').write_text(json.dumps({'res2': len(res2), 'res1': len(res1),
                                                          'chrgenes': len(chrgenes), 'pwint': len(pwint)}))
    (out / f'{name}.oracle_values.json').write_text(json.dumps({
        'chrgenes': sorted(f"(chrgenes {c} {n})" for c, n in chrgenes.items()),
        'pwint': sorted(f"(pwint {pw} {len(v)})" for pw, v in pwint.items())}))
    print(name, 'facts', len(nkv) + len(spo), 'res2', len(res2), 'res1', len(res1), 'pwint', len(pwint),
          'pathway pairs', sum(len(v) for v in pwint.values()))

if __name__ == '__main__':
    main()
