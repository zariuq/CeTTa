#!/usr/bin/env python3
"""LDBC Social Network Benchmark, Business Intelligence reads 9, 11, 12 and 18,
for MORK (MM2) and CeTTa (MeTTa, PeTTa dialect), over the official Datagen
data (Interactive v1 CsvBasic-LongDateFormatter, scale factors 0.1, 0.3, 1, ...).

Source: LDBC SNB specification (Angles et al., arXiv:2001.02299; BI workload:
Szarnyas et al., PVLDB 15(11) 2022), query definitions
github.com/ldbc/ldbc_snb_docs/query-specifications/bi-read-{09,11,12,18}.yaml.
Each query computes its full result: the specification's ORDER BY / LIMIT
(top-k) is left out, since MM2 has no sorting; both engines return every row.

  bi9   Top thread initiators: per person, the posts created in [start, end]
        (threads) and the messages of their reply trees created in that window,
        the root post included.  Reply trees are followed by recursion.
  bi11  Friend triangles: distinct triangles of friendships created in
        [start, end] among persons of one country, for every country at once.
  bi12  How many persons have a given number of messages: per person the
        messages with content, created after start, shorter than a threshold,
        in one of the languages (a comment has its root post's language); then
        the number of persons per message count, the zero bucket included.
  bi18  Friend recommendation: for each parameter tag, every ordered pair of
        persons interested in it who do not know each other, with the number of
        their mutual friends (at least one).

Parameters follow the specification's selection rules and are fixed per scale
factor (stated in each program).  Ids are the data's own integers.

Writes gen/snbSF_Q.{mm2,metta,heads,values,expect.json,oracle_values.json}.
usage: gen_snb.py DATADIR [--queries bi9,bi11,bi12,bi18]
"""
import argparse, csv, datetime, json, sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
INC = "(O (pure (succ $k1 $n) $n (i64_to_string (sum_i64 (i64_from_string $k1) (i64_from_string 1)))))"


def ms(day):
    return int(datetime.datetime.strptime(day, '%Y-%m-%d').replace(tzinfo=datetime.timezone.utc).timestamp() * 1000)


def read(ddir, rel):
    with open(Path(ddir) / rel, encoding='utf-8', newline='') as f:
        rows = list(csv.reader(f, delimiter='|', quoting=csv.QUOTE_NONE))
    return rows[1:]


def in_window(lo, hi, var, then):
    """MM2 pure expression: `then` when lo <= var <= hi, the sentinel `no` otherwise.
    (An ifnz without an else fails the call; in this MORK each failed call keeps
    about 6 KB, so filters return a sentinel and the sentinel fact is dropped.)"""
    return (f"(ifnz (gte_i64 (i64_from_string {var}) (i64_from_string {lo})) then "
            f"(ifnz (lte_i64 (i64_from_string {var}) (i64_from_string {hi})) then {then} else no) else no)")


def facts(lines):
    return '\n'.join(lines) + '\n'


# ------------------------------------------------------------------ BI 9

def bi9(D, sf):
    start, end = ms('2011-06-01'), ms('2011-08-30') + 86400000 - 1
    posts = read(D, 'dynamic/post_0_0.csv'); comments = read(D, 'dynamic/comment_0_0.csv')
    pc = dict(read(D, 'dynamic/post_hasCreator_person_0_0.csv'))
    rp = read(D, 'dynamic/comment_replyOf_post_0_0.csv'); rc = read(D, 'dynamic/comment_replyOf_comment_0_0.csv')
    data = facts(f"(post {p[0]} {pc[p[0]]} {p[2]})" for p in posts) + facts(f"(cmt {c[0]} {c[1]})" for c in comments) \
        + facts(f"(rp {c} {p})" for c, p in rp) + facts(f"(rc {c} {q})" for c, q in rc)
    mm2 = f"""; snb{sf} bi9: LDBC SNB BI 9, top thread initiators, window [{start}, {end}] (2011-06-01 .. 2011-08-30)
{data}(succ 0 1)
; threads: posts created in the window; in-window comments; reply edges parent -> child
(exec (0 0 0) (, (post $p $who $d)) (O (pure (wp $x) $x {in_window(start, end, '$d', '(tuple $p $who)')})))
(exec (0 0 1) (, (cmt $c $d)) (O (pure (wc $x) $x {in_window(start, end, '$d', '$c')})))
(exec (0 0 2) (, (wp ($p $who))) (O (+ (wpost $p $who)) (- (wp ($p $who)))))
(exec (0 0 3) (, (rp $c $p)) (, (ch $p $c)))
(exec (0 0 4) (, (rc $c $q)) (, (ch $q $c)))
(exec (0 0 5) (, (wpost $p $who)) (, (tm $p $p) (fr 0 $p $p) (live 0)))
(exec (0 0 6) (, (wc no)) (O (- (wc no)) (- (wp no))))
; down the reply trees, one level per round: (tm root m) for every in-window message m of the tree
(exec (2 0 c) (, (exec (2 $k c) $ps $ts) (succ $k $k1) (live $k))
              (, (exec (2 $k1 c) $ps $ts)
                 (exec (1 $k a) (, (fr $k $m $r) (ch $m $c)) (, (fr $k1 $c $r) (live $k1)))
                 (exec (1 $k b) (, (fr $k1 $c $r) (wc $c)) (, (tm $r $c)))
                 (exec (1 $k d) (, (fr $k $m $r)) (O (- (fr $k $m $r))))
                 (exec (1 $k e) (, (live $k1)) {INC})))
(exec (3 0 a) (, (wpost $r $who)) (O (count (thr $who $n) $n $r)))
(exec (3 0 b) (, (wpost $r $who) (tm $r $m)) (O (count (msgs $who $n) $n $m)))
(exec (4 0 a) (, (thr $who $t) (msgs $who $m)) (, (bi9 $who $t $m)))
"""
    metta = f"""; snb{sf} bi9: LDBC SNB BI 9 in MeTTa (PeTTa dialect), window [{start}, {end}]
{data}!(bind! &t (new-space set))
; reply edges parent -> child
!(size-atom (collapse (match &self (rp $c $p) (add-atom &t (ch $p $c)))))
!(size-atom (collapse (match &self (rc $c $q) (add-atom &t (ch $q $c)))))
(= (in-window $d) (and (>= $d {start}) (<= $d {end})))
; the replies of a message, transitively
(= (replies $m) (match &t (ch $m $c) $c))
(= (replies $m) (match &t (ch $m $c) (replies $c)))
; a thread: its root post (in the window) and the replies created in the window
(= (counted $c) (match &self (cmt $c $d) (if (in-window $d) $c (empty))))
(= (thread $p $who)
   (let $n (size-atom (collapse (counted (replies $p))))
     (let $n1 (+ $n 1) (add-atom &t (tm $who $p $n1)))))
!(size-atom (collapse (match &self (post $p $who $d) (if (in-window $d) (thread $p $who) (empty)))))
!(bind! &r (new-space set))
!(size-atom (collapse (match &t (tm $who $p $n) (add-atom &r (who $who)))))
(= (summary $who)
   (let* (($ts (collapse (match &t (tm $who $p $n) $n)))
          ($t (size-atom $ts))
          ($m (foldl-atom $ts 0 $a $b (+ $a $b))))
     (add-atom &r (bi9 $who $t $m))))
!(size-atom (collapse (match &r (who $who) (summary $who))))
!(match &r (bi9 $w $t $m) (bi9 $w $t $m))
!(size-atom (collapse (match &r (bi9 $w $t $m) t)))
"""
    # oracle
    cdate = {c[0]: int(c[1]) for c in comments}
    ch = defaultdict(list)
    for c, p in rp: ch[p].append(c)
    for c, q in rc: ch[q].append(c)
    thr, msgs = Counter(), Counter()
    for p in posts:
        d = int(p[2])
        if start <= d <= end:
            who = pc[p[0]]
            thr[who] += 1
            n = 1
            stack = list(ch[p[0]])
            while stack:
                m = stack.pop()
                if start <= cdate[m] <= end: n += 1
                stack.extend(ch[m])
            msgs[who] += n
    rows = sorted(f"(bi9 {w} {thr[w]} {msgs[w]})" for w in thr)
    return mm2, metta, ['bi9'], ['bi9'], {'bi9': len(rows)}, {'bi9': rows}


# ------------------------------------------------------------------ BI 11

def bi11(D, sf):
    start, end = ms('2012-05-01'), ms('2012-08-08') + 86400000 - 1
    knows = read(D, 'dynamic/person_knows_person_0_0.csv')
    loc = read(D, 'dynamic/person_isLocatedIn_place_0_0.csv')
    places = read(D, 'static/place_0_0.csv'); part = read(D, 'static/place_isPartOf_place_0_0.csv')
    data = facts(f"(knows {a} {b} {d})" for a, b, d in knows) + facts(f"(loc {p} {c})" for p, c in loc) \
        + facts(f"(part {a} {b})" for a, b in part) + facts(f"(country {p[0]})" for p in places if p[3] == 'country')
    lt = lambda a, b: f"(ifnz (lt_i64 (i64_from_string {a}) (i64_from_string {b})) then {{}} else no)"
    mm2 = f"""; snb{sf} bi11: LDBC SNB BI 11, friend triangles, every country, window [{start}, {end}] (2012-05-01 .. 2012-08-08)
{data}; friendships in the window, oriented from the smaller id to the larger
(exec (0 0 0) (, (knows $a $b $d)) (O (pure (ow $x) $x {lt('$a', '$b').format(in_window(start, end, '$d', '(tuple $a $b)'))})))
(exec (0 0 1) (, (knows $a $b $d)) (O (pure (ow $x) $x {lt('$b', '$a').format(in_window(start, end, '$d', '(tuple $b $a)'))})))
(exec (0 0 2) (, (ow ($a $b))) (O (+ (kw $a $b)) (- (ow ($a $b)))))
(exec (0 0 3) (, (loc $p $c) (part $c $k) (country $k)) (, (pc $p $k)))
; each triangle once (a < b < c), counted per country
(exec (1 0 0) (, (kw $a $b) (kw $b $c) (kw $a $c) (pc $a $k) (pc $b $k) (pc $c $k)) (O (count (tri $k $n) $n ($a $b $c))))
"""
    metta = f"""; snb{sf} bi11: LDBC SNB BI 11 in MeTTa (PeTTa dialect), every country, window [{start}, {end}]
{data}!(bind! &g (new-space set))
; friendships in the window, oriented from the smaller id to the larger
!(size-atom (collapse (match &self (knows $a $b $d)
    (if (and (>= $d {start}) (<= $d {end})) (if (< $a $b) (add-atom &g (kw $a $b)) (add-atom &g (kw $b $a))) (empty)))))
!(size-atom (collapse (match &self (loc $p $c) (match &self (part $c $k) (match &self (country $k) (add-atom &g (pc $p $k)))))))
!(bind! &t (new-space set))
!(size-atom (collapse (match &g (kw $a $b) (match &g (kw $b $c) (match &g (kw $a $c)
    (match &g (pc $a $k) (match &g (pc $b $k) (match &g (pc $c $k) (let $_ (add-atom &t (tk $k)) (add-atom &t (t3 $k $a $b $c)))))))))))
!(bind! &r (new-space set))
!(size-atom (collapse (match &t (tk $k) (let $n (size-atom (collapse (match &t (t3 $k $a $b $c) t))) (add-atom &r (tri $k $n))))))
!(match &r (tri $k $n) (tri $k $n))
!(size-atom (collapse (match &r (tri $k $n) t)))
"""
    country = {p[0] for p in places if p[3] == 'country'}
    parent = dict(part)
    pcn = {}
    for p, c in loc:
        if parent.get(c) in country: pcn[p] = parent[c]
    adj = defaultdict(set)
    for a, b, d in knows:
        if start <= int(d) <= end:
            x, y = sorted((int(a), int(b)))
            adj[x].add(y)
    tri = Counter()
    for a in adj:
        for b in adj[a]:
            for c in adj[a] & adj.get(b, set()):
                ka = pcn.get(str(a))
                if ka is not None and ka == pcn.get(str(b)) == pcn.get(str(c)):
                    tri[ka] += 1
    rows = sorted(f"(tri {k} {n})" for k, n in tri.items())
    return mm2, metta, ['tri'], ['tri'], {'tri': len(rows)}, {'tri': rows}


# ------------------------------------------------------------------ BI 12

def bi12(D, sf):
    start, threshold, langs = ms('2011-07-01'), 20, ['uz', 'tk']
    persons = read(D, 'dynamic/person_0_0.csv')
    posts = read(D, 'dynamic/post_0_0.csv'); comments = read(D, 'dynamic/comment_0_0.csv')
    pc = dict(read(D, 'dynamic/post_hasCreator_person_0_0.csv'))
    cc = dict(read(D, 'dynamic/comment_hasCreator_person_0_0.csv'))
    rp = read(D, 'dynamic/comment_replyOf_post_0_0.csv'); rc = read(D, 'dynamic/comment_replyOf_comment_0_0.csv')
    flag = lambda content: 't' if content != '' else 'e'
    lang = lambda l: l if l != '' else 'none'
    data = facts(f"(person {p[0]})" for p in persons) \
        + facts(f"(post {p[0]} {pc[p[0]]} {p[2]} {p[7]} {lang(p[5])} {flag(p[6])})" for p in posts) \
        + facts(f"(cmt {c[0]} {cc[c[0]]} {c[1]} {c[5]} {flag(c[4])})" for c in comments) \
        + facts(f"(rp {c} {p})" for c, p in rp) + facts(f"(rc {c} {q})" for c, q in rc) \
        + facts(f"(lang {l})" for l in langs)
    ok = lambda d, n, then: (f"(ifnz (gt_i64 (i64_from_string {d}) (i64_from_string {start})) then "
                             f"(ifnz (lt_i64 (i64_from_string {n}) (i64_from_string {threshold})) then {then} else no) else no)")
    mm2 = f"""; snb{sf} bi12: LDBC SNB BI 12, persons per message count; after {start} (2011-07-01), length < {threshold}, languages {' '.join(langs)}
{data}(succ 0 1) (live 0)
; the language of a comment is its root post's: roots are found down the reply trees
(exec (0 0 0) (, (rp $c $p)) (, (ch $p $c)))
(exec (0 0 1) (, (rc $c $q)) (, (ch $q $c)))
(exec (0 0 2) (, (post $p $who $d $n $l $f) (lang $l)) (, (lp $p) (fr 0 $p $p)))
(exec (2 0 c) (, (exec (2 $k c) $ps $ts) (succ $k $k1) (live $k))
              (, (exec (2 $k1 c) $ps $ts)
                 (exec (1 $k a) (, (fr $k $m $r) (ch $m $c)) (, (fr $k1 $c $r) (lc $c) (live $k1)))
                 (exec (1 $k d) (, (fr $k $m $r)) (O (- (fr $k $m $r))))
                 (exec (1 $k e) (, (live $k1)) {INC})))
; qualifying messages: content, date after start, length under the threshold, language
(exec (3 0 a) (, (lp $p) (post $p $who $d $n $l t)) (O (pure (qq $x) $x {ok('$d', '$n', '(tuple $p $who)')})))
(exec (3 0 b) (, (lc $c) (cmt $c $who $d $n t)) (O (pure (qq $x) $x {ok('$d', '$n', '(tuple $c $who)')})))
(exec (3 0 c) (, (qq ($m $who))) (O (+ (q $m $who)) (- (qq ($m $who)))))
(exec (4 0 a) (, (q $m $who)) (O (count (mc $who $n) $n $m)))
(exec (4 0 b) (, (person $p)) (, (zero $p)))
(exec (4 0 c) (, (mc $p $n)) (O (- (zero $p))))
(exec (5 0 a) (, (mc $p $n)) (O (count (hist $n $c) $c $p)))
(exec (5 0 b) (, (zero $p)) (O (count (hist 0 $c) $c $p)))
"""
    metta = f"""; snb{sf} bi12: LDBC SNB BI 12 in MeTTa (PeTTa dialect); after {start}, length < {threshold}, languages {' '.join(langs)}
{data}!(bind! &t (new-space set))
!(size-atom (collapse (match &self (rp $c $p) (add-atom &t (ch $p $c)))))
!(size-atom (collapse (match &self (rc $c $q) (add-atom &t (ch $q $c)))))
; the replies, transitively, of a post written in one of the languages
(= (replies $m) (match &t (ch $m $c) (let $_ (add-atom &t (lc $c)) (replies $c))))
!(size-atom (collapse (match &self (post $p $who $d $n $l $f) (match &self (lang $l) (replies $p)))))
(= (ok $d $n) (and (> $d {start}) (< $n {threshold})))
!(bind! &q (new-space set))
!(size-atom (collapse (match &self (post $p $who $d $n $l t) (match &self (lang $l) (if (ok $d $n) (add-atom &q (q $who $p)) (empty))))))
; (the content flag is matched as a variable and tested: with the constant t in the last position
;  this probe took over a minute in r34, against a fraction of a second)
!(size-atom (collapse (match &t (lc $c) (match &self (cmt $c $who $d $n $f) (if (and (== $f t) (ok $d $n)) (add-atom &q (q $who $c)) (empty))))))
!(bind! &r (new-space set))
; messages are filed under their creator, counts under their value
(= (count-of $p) (size-atom (collapse (match &q (q $p $m) t))))
!(size-atom (collapse (match &self (person $p) (let $n (count-of $p) (add-atom &r (mc $n $p))))))
!(size-atom (collapse (match &r (mc $n $p) (add-atom &r (bucket $n)))))
!(size-atom (collapse (match &r (bucket $n) (let $c (size-atom (collapse (match &r (mc $n $p) t))) (add-atom &r (hist $n $c))))))
!(match &r (hist $n $c) (hist $n $c))
!(size-atom (collapse (match &r (hist $n $c) t)))
"""
    # oracle
    ch = defaultdict(list)
    for c, p in rp: ch[p].append(c)
    for c, q in rc: ch[q].append(c)
    cdict = {c[0]: c for c in comments}
    count = Counter()
    for p in posts:
        if p[5] in langs:
            if p[6] != '' and int(p[2]) > start and int(p[7]) < threshold: count[pc[p[0]]] += 1
            stack = list(ch[p[0]])
            while stack:
                m = stack.pop(); c = cdict[m]
                if c[4] != '' and int(c[1]) > start and int(c[5]) < threshold: count[cc[m]] += 1
                stack.extend(ch[m])
    hist = Counter(count.get(p[0], 0) for p in persons)
    rows = sorted(f"(hist {n} {k})" for n, k in hist.items())
    return mm2, metta, ['hist'], ['hist'], {'hist': len(rows)}, {'hist': rows}


# ------------------------------------------------------------------ BI 18

def bi18(D, sf):
    interest = read(D, 'dynamic/person_hasInterest_tag_0_0.csv')
    knows = read(D, 'dynamic/person_knows_person_0_0.csv')
    tags = [t for t, n in sorted(Counter(t for p, t in interest).items(), key=lambda kv: (-kv[1], int(kv[0])))[:3]]
    data = facts(f"(knows {a} {b} {d})" for a, b, d in knows) + facts(f"(int {p} {t})" for p, t in interest) \
        + facts(f"(ptag {t})" for t in tags)
    mm2 = f"""; snb{sf} bi18: LDBC SNB BI 18, friend recommendation for tags {' '.join(tags)} (the three with most interested persons)
{data}(exec (0 0 0) (, (knows $a $b $d)) (, (k $a $b) (k $b $a)))
(exec (0 0 1) (, (int $p $t)) (, (it $t $p)))
; pairs of interested persons with a mutual friend, counted; then pairs of friends and of a person with itself are dropped
(exec (1 0 0) (, (ptag $t) (it $t $p1) (k $p1 $m) (k $m $p2) (int $p2 $t)) (O (count (rec $t $p1 $p2 $n) $n $m)))
(exec (2 0 0) (, (rec $t $p1 $p2 $n) (k $p1 $p2)) (O (- (rec $t $p1 $p2 $n))))
(exec (2 0 1) (, (rec $t $p $p $n)) (O (- (rec $t $p $p $n))))
"""
    metta = f"""; snb{sf} bi18: LDBC SNB BI 18 in MeTTa (PeTTa dialect), tags {' '.join(tags)}
{data}!(bind! &g (new-space set))
!(size-atom (collapse (match &self (knows $a $b $d) (let $_ (add-atom &g (k $a $b)) (add-atom &g (k $b $a))))))
!(size-atom (collapse (match &self (int $p $t) (add-atom &g (it $t $p)))))
(= (strangers $a $b) (and (not (== $a $b)) (== (collapse (match &g (k $a $b) t)) ())))
!(bind! &m (new-space set))
!(size-atom (collapse (match &self (ptag $t) (match &g (it $t $p1) (match &g (k $p1 $m) (match &g (k $m $p2)
    (match &self (int $p2 $t) (if (strangers $p1 $p2) (let $_ (add-atom &m (pair $t $p1 $p2)) (add-atom &m (mf $t $p1 $p2 $m))) (empty)))))))))
!(bind! &r (new-space set))
!(size-atom (collapse (match &m (pair $t $p1 $p2) (let $n (size-atom (collapse (match &m (mf $t $p1 $p2 $x) t))) (add-atom &r (rec $t $p1 $p2 $n))))))
!(match &r (rec $t $a $b $n) (rec $t $a $b $n))
!(size-atom (collapse (match &r (rec $t $a $b $n) t)))
"""
    friends = defaultdict(set)
    for a, b, d in knows:
        friends[a].add(b); friends[b].add(a)
    inter = defaultdict(set)
    for p, t in interest: inter[t].add(p)
    rows = []
    for t in tags:
        I = inter[t]
        for p1 in I:
            cnt = Counter()
            for m in friends[p1]:
                for p2 in friends[m]:
                    if p2 in I and p2 != p1 and p2 not in friends[p1]:
                        cnt[p2] += 1
            rows.extend(f"(rec {t} {p1} {p2} {n})" for p2, n in cnt.items())
    rows.sort()
    return mm2, metta, ['rec'], ['rec'], {'rec': len(rows)}, {'rec': rows}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('datadir', help='an extracted social_network-sfX-CsvBasic-LongDateFormatter directory')
    ap.add_argument('--queries', default='bi9,bi11,bi12,bi18')
    ap.add_argument('--outdir', default=str(HERE / 'gen'))
    a = ap.parse_args()
    D = Path(a.datadir)
    sf = D.name.split('-')[1][2:]
    out = Path(a.outdir); out.mkdir(parents=True, exist_ok=True)
    for q in a.queries.split(','):
        mm2, metta, heads, vals, expect, ov = {'bi9': bi9, 'bi11': bi11, 'bi12': bi12, 'bi18': bi18}[q](D, sf)
        name = f'snb{sf}_{q}'
        (out / f'{name}.mm2').write_text(mm2)
        (out / f'{name}.metta').write_text(metta)
        (out / f'{name}.heads').write_text(' '.join(heads) + '\n')
        (out / f'{name}.values').write_text(' '.join(vals) + '\n')
        (out / f'{name}.expect.json').write_text(json.dumps(expect))
        (out / f'{name}.oracle_values.json').write_text(json.dumps(ov))
        print(name, expect, (out / f'{name}.mm2').stat().st_size, 'bytes')


if __name__ == '__main__':
    main()
