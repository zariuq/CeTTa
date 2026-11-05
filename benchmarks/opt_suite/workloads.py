"""Original Opt application workloads, recovered as reproducible benchmark inputs.

MM2 programs and ordinary MeTTa solutions use the same generated facts.
The two-hop materialization workload is not fixed-point transitive closure.
"""

import random


def transitive(nodes, edges):
    rng = random.Random(0)
    es = sorted({(rng.randrange(nodes), rng.randrange(nodes)) for _ in range(edges)})
    facts = ''.join(f"(edge {i} {j})\n" for i, j in es)
    mm2 = facts + ("(exec 0 (, (edge $x $y) (edge $y $z)) (, (trans $x $z)))\n"
                   "(exec 1 (, (edge $x $y) (edge $y $z) (edge $x $z)) (, (dtrans $x $y $z)))\n")
    metta = facts + (
        "; the derived facts live in a set space, as MM2's do; each rule runs for its effect\n"
        "!(bind! &t (new-space set))\n"
        "(= (rule-trans) (match &self (, (edge $x $y) (edge $y $z)) (add-atom &t (trans $x $z))))\n"
        "(= (rule-dtrans) (match &self (, (edge $x $y) (edge $y $z) (edge $x $z)) (add-atom &t (dtrans $x $y $z))))\n"
        "!(let $r (collapse (rule-trans)) done)\n"
        "!(let $r (collapse (rule-dtrans)) done)\n"
        "!(size-atom (collapse (match &t (trans $x $z) t)))\n"
        "!(size-atom (collapse (match &t (dtrans $x $y $z) t)))\n")
    bag = facts + (
        "; portable form: each join's matches are read as a snapshot and stored once\n"
        "(= (store-once $atoms) (let $set (unique-atom $atoms) (size-atom (collapse (let $a (superpose $set) (add-atom &self $a))))))\n"
        "!(store-once (collapse (match &self (, (edge $x $y) (edge $y $z)) (trans $x $z))))\n"
        "!(store-once (collapse (match &self (, (edge $x $y) (edge $y $z) (edge $x $z)) (dtrans $x $y $z))))\n")
    return mm2, metta, ['trans', 'dtrans'], bag


def clique(nodes, edges, k, mm2_order="lex"):
    # mm2_order "early" checks each new vertex's edges as soon as it is
    # introduced, an order MORK's default relation-at-a-time join favours;
    # the original suite used the lexicographic order for both engines.
    rng = random.Random(0)
    es = set()
    while len(es) < edges:
        i, j = rng.randrange(nodes), rng.randrange(nodes)
        if i != j:
            es.add((min(i, j), max(i, j)))
    facts = ''.join(f"(edge {i} {j})\n" for i, j in sorted(es))
    conj = ''.join(f" (edge $x{i} $x{j})" for i in range(k) for j in range(i + 1, k))
    head = ''.join(f" $x{i}" for i in range(k))
    name = f"{k}-clique"
    early = ''.join(f" (edge $x{i} $x{j})" for j in range(1, k) for i in range(j))
    mm2 = facts + f"(exec 0 (,{early if mm2_order == 'early' else conj}) (, ({name}{head})))\n"
    metta = facts + (
        "!(bind! &t (new-space set))\n"
        f"(= (rule-clique) (match &self (,{conj}) (add-atom &t ({name}{head}))))\n"
        "!(let $r (collapse (rule-clique)) done)\n"
        f"!(size-atom (collapse (match &t ({name}{head}) t)))\n")
    # cliques of an ordered graph are found once each, so a bag stores them as matched
    bag = facts + f"!(size-atom (collapse (match &self (,{conj}) (add-atom &self ({name}{head})))))\n"
    return mm2, metta, [name], bag


def odd_even_sort(n):
    rng = random.Random(0)
    values = list(range(n))
    rng.shuffle(values)
    facts = ''.join(f"(A {i} v{values[i]:05d})\n" for i in range(n))
    facts += ''.join(f"(succ {i} {i + 1})\n" for i in range(n))
    facts += ''.join(f"(parity {i} {'even' if i % 2 == 0 else 'odd'})\n" for i in range(n))
    facts += ''.join(f"(lt v{a:05d} v{b:05d})\n" for b in range(n) for a in range(b))
    mm2 = facts + (
        "((phase $p)  (, (parity $i $p) (succ $i $si) (A $i $e) (A $si $se) (lt $se $e))\n"
        "             (O (- (A $i $e)) (- (A $si $se)) (+ (A $i $se)) (+ (A $si $e))))\n"
        "(phase 0 odd) (phase 1 even)\n"
        "(exec repeat (, (A $k $_) (phase $kp $phase) ((phase $phase) $p0 $t0))\n"
        "             (, (exec ($k $kp) $p0 $t0)))\n")
    metta = facts + (
        "; one phase reads every swap it makes, as an MM2 exec reads its matches, then makes them\n"
        "(= (phase $p)\n"
        "   (let $swaps (collapse (match &self (, (parity $i $p) (succ $i $si) (A $i $e) (A $si $se) (lt $se $e))\n"
        "                                (swap $i $e $si $se)))\n"
        "     (size-atom (collapse (let (swap $i $e $si $se) (superpose $swaps)\n"
        "       (let* (($a (remove-atom &self (A $i $e))) ($b (remove-atom &self (A $si $se)))\n"
        "              ($c (add-atom &self (A $i $se))) ($d (add-atom &self (A $si $e))))\n"
        "         t))))))\n"
        "(= (rounds $k) (if (== $k 0) done (let* (($o (phase odd)) ($e (phase even))) (rounds (- $k 1)))))\n"
        f"!(rounds {n})\n"
        "!(size-atom (collapse (match &self (, (A $i $e) (A $si $se) (succ $i $si) (lt $se $e)) bad)))\n"
        "!(size-atom (collapse (match &self (A $i $e) t)))\n")
    return mm2, metta, ['A'], metta


def ancestry(n, window, poi_every):
    rng = random.Random(0)
    parents = []
    for c in range(2, n):
        lo = max(0, c - window)
        for p in rng.sample(range(lo, c), 2) if c - lo >= 2 else range(lo, c):
            parents.append((p, c))
    facts = ''.join(f"(parent p{p} p{c})\n" for p, c in parents)
    facts += ''.join(f"(poi p{c})\n" for c in range(0, n, poi_every))
    mm2 = facts + (
        "(exec (0 0) (, (parent $p $c)) (, (child $c $p)))\n"
        "(exec (0 1) (, (poi $c) (child $c $p)) (, (generation Z $c $p)))\n"
        "(exec (1 Z) (, (exec (1 $l) $ps $ts) (generation $l $c $p) (child $p $gp))\n"
        "            (, (exec (1 (S $l)) $ps $ts) (generation (S $l) $c $gp)))\n"
        "(exec (2 0) (, (generation $_ $p $a)) (, (ancestor $p $a)))\n")
    metta = facts + (
        "; the derived facts live in a set space, as MM2's do\n"
        "!(bind! &t (new-space set))\n"
        "!(let $r (collapse (match &self (parent $p $c) (add-atom &t (child $c $p)))) done)\n"
        "!(let $r (collapse (match &self (poi $c) (add-atom &t (poi $c)))) done)\n"
        "!(let $r (collapse (match &t (, (poi $c) (child $c $p)) (add-atom &t (generation Z $c $p)))) done)\n"
        "; one generation further back, until a generation adds nothing\n"
        "(= (gen-step $l) (match &t (, (generation $l $c $p) (child $p $gp)) (add-atom &t (generation (S $l) $c $gp))))\n"
        "(= (generations $l) (if (== (size-atom (collapse (gen-step $l))) 0) done (generations (S $l))))\n"
        "!(generations Z)\n"
        "!(let $r (collapse (match &t (generation $l $p $a) (add-atom &t (ancestor $p $a)))) done)\n"
        "!(size-atom (collapse (match &t (child $c $p) t)))\n"
        "!(size-atom (collapse (match &t (generation $l $c $p) t)))\n"
        "!(size-atom (collapse (match &t (ancestor $p $a) t)))\n")
    return mm2, metta, ['child', 'generation', 'ancestor'], metta


def peano(n):
    t = 'Z'
    for _ in range(n): t = f'(S {t})'
    return t


def process_calculus(steps, x, y):
    # MORK kernel/src/main.rs process_calculus_bench(steps, x, y)
    procs = (f"(? (add $ret) ((S $x) $y) (| (! (add (PN $x $y)) ($x $y)) (? (PN $x $y) $z (! $ret (S $z)))))\n"
             f"(? (add $ret) (Z $y) (! $ret $y))\n"
             f"(! (add result) ({peano(x)} {peano(y)}))\n")
    mm2 = (f"(exec (IC 0 1 {peano(steps)})\n"
           "      (, (exec (IC $x $y (S $c)) $sp $st) ((exec $x) $p $t))\n"
           "      (, (exec (IC $y $x $c) $sp $st) (exec (R $x) $p $t)))\n"
           "((exec 0) (, (petri (? $channel $payload $body)) (petri (! $channel $payload))) (, (petri $body)))\n"
           "((exec 1) (, (petri (| $lprocess $rprocess))) (, (petri $lprocess) (petri $rprocess)))\n"
           + ''.join(f"(petri {p})\n" for p in procs.strip().split('\n')))
    named = procs.replace('(!', '(send').replace('(?', '(recv').replace('(|', '(par')
    metta = ("; the processes live in a set space, as MM2's petri dish does; send, recv and\n"
             "; par spell MM2's !, ? and |\n"
             "!(bind! &p (new-space set))\n"
             + ''.join(f"!(add-atom &p {p})\n" for p in named.strip().split('\n')) +
             "; a send meets a receive with the same channel and payload, and the receive's body runs\n"
             "(= (communicate) (match &p (, (recv $channel $payload $body) (send $channel $payload)) (add-atom &p $body)))\n"
             "; a parallel composition runs both sides\n"
             "(= (split) (match &p (par $l $r) (let $_ (add-atom &p $l) (add-atom &p $r))))\n"
             "(= (rounds $k) (if (== $k 0) done (let* (($c (collapse (communicate))) ($s (collapse (split)))) (rounds (- $k 1)))))\n"
             f"!(rounds {(steps + 1) // 2})\n"
             "!(size-atom (collapse (match &p $process t)))\n")
    return mm2, metta, ['petri']


PROGRAM = [('JZ', 2, 5), ('DEC', 2), ('INC', 3), ('INC', 1), ('JZ', 0, 0), ('JZ', 1, 9), ('DEC', 1), ('INC', 2), ('JZ', 0, 5), ('H',)]


def counter_machine(n):
    # MORK kernel/resources/counter_machine_5.mm2 with register 2 holding n: the
    # machine copies register 2 into register 3 through register 1
    def instr(i):
        if i[0] == 'H': return 'H'
        if i[0] == 'JZ': return f'(JZ {i[1]} {peano(i[2])})'
        return f'({i[0]} {i[1]})'
    regs = [0, 0, n, 0, 0]
    mm2 = ''.join(f'(program {peano(k)} {instr(i)})\n' for k, i in enumerate(PROGRAM))
    mm2 += ''.join(f'(state Z (REG {r} {peano(v)}))\n' for r, v in enumerate(regs)) + '(state Z (IC Z))\n'
    mm2 += '(if (S $n) $x $y $x)\n(if Z $x $y $y)\n'
    mm2 += ''.join(f'({a} != {b})\n' for a in range(5) for b in range(5) if a != b)
    mm2 += """((step JZ $ts)
  (, (state $ts (IC $i)) (program $i (JZ $r $j)) (state $ts (REG $r $v)) (if $v (S $i) $j $ni) (state $ts (REG $k $kv)))
  (, (state (S $ts) (IC $ni)) (state (S $ts) (REG $k $kv))))
((step INC $ts)
  (, (state $ts (IC $i)) (program $i (INC $r)) (state $ts (REG $r $v)) ($r != $o) (state $ts (REG $o $ov)))
  (, (state (S $ts) (IC (S $i))) (state (S $ts) (REG $r (S $v))) (state (S $ts) (REG $o $ov))))
((step DEC $ts)
  (, (state $ts (IC $i)) (program $i (DEC $r)) (state $ts (REG $r (S $v))) ($r != $o) (state $ts (REG $o $ov)))
  (, (state (S $ts) (IC (S $i))) (state (S $ts) (REG $r $v)) (state (S $ts) (REG $o $ov))))
((step HALT $ts)
  (, (state $ts (IC $i)) (program $i H))
  (, (HALTED $ts)))
(exec (clocked Z)
  (, (exec (clocked $ts) $p1 $t1) (state $ts (IC $_)) ((step $k $ts) $p0 $t0))
  (, (exec ($k $ts) $p0 $t0) (exec (clocked (S $ts)) $p1 $t1)))
"""
    def minstr(i):
        if i[0] == 'H': return 'H'
        if i[0] == 'JZ': return f'(JZ {i[1]} {i[2]})'
        return f'({i[0]} {i[1]})'
    metta = ''.join(f'(program {k} {minstr(i)})\n' for k, i in enumerate(PROGRAM))
    metta += """; the machine runs as a function over its clock, instruction counter and
; registers, and records each state as MM2 does; register values and the
; clock are peano numbers, as MM2's are
!(bind! &m (new-space set))
(= (peano $n) (if (== $n 0) Z (S (peano (- $n 1)))))
(= (set-reg $regs $r $v)
   (let ($x $rest) (decons-atom $regs)
     (if (== $r 0) (cons-atom $v $rest) (cons-atom $x (set-reg $rest (- $r 1) $v)))))
(= (record $pt $regs $r)
   (if (== $regs ()) done
     (let ($v $rest) (decons-atom $regs)
       (let $_ (add-atom &m (state $pt (REG $r $v))) (record $pt $rest (+ $r 1))))))
(= (run $pt $ic $regs)
   (let* (($pic (peano $ic)) ($_ (add-atom &m (state $pt (IC $pic)))) ($_r (record $pt $regs 0)))
     (case (match &self (program $ic $i) $i)
       ((H (add-atom &m (HALTED $pt)))
        ((JZ $r $j) (run (S $pt) (if (== (index-atom $regs $r) Z) $j (+ $ic 1)) $regs))
        ((INC $r) (run (S $pt) (+ $ic 1) (set-reg $regs $r (S (index-atom $regs $r)))))
        ((DEC $r) (case (index-atom $regs $r)
                    (((S $v) (run (S $pt) (+ $ic 1) (set-reg $regs $r $v)))
                     (Z stuck))))))))
"""
    metta += f'!(run Z 0 (Z Z {peano(n)} Z Z))\n'
    metta += '!(size-atom (collapse (match &m (state $t $s) t)))\n!(size-atom (collapse (match &m (HALTED $t) t)))\n'
    return mm2, metta, ['state', 'HALTED']


NEIGHBORS = """(neighbors ($q $r (S $s)) ($q (S $r) $s))
(neighbors ($q $r (S $s)) ((S $q) $r $s))
(neighbors ($q (S $r) $s) ((S $q) $r $s))
(neighbors ($q (S $r) $s) ($q $r (S $s)))
(neighbors ((S $q) $r $s) ($q (S $r) $s))
(neighbors ((S $q) $r $s) ($q $r (S $s)))
"""


def hexlife(plane, cells, generations, seed=0):
    # MORK wiki "hexlife": the neighbours relation as facts over peano cube
    # coordinates (q + r + s is kept), a cell lives iff two neighbours do
    rng = random.Random(seed)
    tri = [(q, r, plane - q - r) for q in range(plane + 1) for r in range(plane + 1 - q)]
    soup = sorted(rng.sample(tri, cells))
    alive = ''.join(f'(alive ({peano(q)} {peano(r)} {peano(s)}))\n' for q, r, s in soup)
    mm2 = NEIGHBORS + alive + '(cell lives 2)\n'
    mm2 += ''.join(f'(gens {peano(t)})\n' for t in range(generations))
    mm2 += """((0 $t) (, (alive $co) (neighbors $co $nco) (alive $nco)) (O (count (anbs $co $k) $k $nco)))
((0 $t) (, (alive $co) (neighbors $co $nco) (neighbors $nco $nnco) (alive $nnco)) (O (count (anbs $nco $k) $k $nnco)))
((1 $t) (, (alive $co)) (O (- (alive $co))))
((2 $t) (, (anbs $co $c) (cell lives $c)) (O (+ (alive $co))))
((3 $t) (, (anbs $co $c)) (O (- (anbs $co $c))))
(exec (clk Z) (, (exec (clk $t) $p1 $t1) (gens $t) (($k $t) $p0 $t0))
              (, (exec ($k $t) $p0 $t0) (exec (clk (S $t)) $p1 $t1)))
"""
    metta = NEIGHBORS + alive + """; a cell's live neighbours: the neighbours relation is read by unifying a
; ground cell with the facts' constructor patterns, as MM2 reads it
(= (live-count $c) (size-atom (collapse (match &self (, (neighbors $c $n) (alive $n)) $n))))
; one generation: a cell next to a live cell lives iff it has exactly two
; live neighbours
(= (step)
   (let* (($cands (unique-atom (collapse (match &self (, (alive $a) (neighbors $a $c)) $c))))
          ($next (collapse (let $c (superpose $cands) (if (== (live-count $c) 2) $c (empty)))))
          ($old (collapse (match &self (alive $c) $c)))
          ($_1 (collapse (let $c (superpose $old) (remove-atom &self (alive $c)))))
          ($_2 (collapse (let $c (superpose $next) (add-atom &self (alive $c))))))
     (size-atom $next)))
(= (generations $k) (if (== $k 0) done (let $n (step) (generations (- $k 1)))))
"""
    metta += f'!(generations {generations})\n!(size-atom (collapse (match &self (alive $c) t)))\n'
    return mm2, metta, ['alive']


def backward_chain(m, depth=6):
    # MORK kernel/src/main.rs bc3: backward chaining by goals and evidence on a
    # clock (its rev step reads any goal, as bc0's does), for m targets, each
    # proved by modus ponens from a fact and a relation
    kb = ''.join(f'(kb (: a{i} A{i}))\n(kb (: r{i} (R A{i} B{i})))\n' for i in range(m))
    kb += '(kb (: MP (-> (R $p $q) (-> $p $q))))\n'
    targets = ''.join(f'(target B{i})\n' for i in range(m))
    mm2 = kb + targets + ''.join(f'(goal Z (: $proof B{i}))\n' for i in range(m))
    mm2 += ''.join(f'(budget {peano(t)})\n' for t in range(depth)) + """((step (0 base) $ts)
  (, (goal $ts (: $proof $conclusion)) (kb (: $proof $conclusion)))
  (, (ev (: $proof $conclusion))))
((step (1 abs) $ts)
  (, (goal $k (: $proof $conclusion)))
  (, (goal (S $ts) (: $lhs (-> $synth $conclusion)))))
((step (2 rev) $ts)
  (, (ev (: $lhs (-> $a $r))) (goal $j (: $k $r)))
  (, (goal (S $ts) (: $rhs $a))))
((step (3 app) $ts)
  (, (ev (: $lhs (-> $a $r))) (ev (: $rhs $a)))
  (, (ev (: (@ $lhs $rhs) $r))))
(exec (clocked Z)
  (, ((step $x $ts) $p0 $t0) (exec (clocked $ts) $p1 $t1) (budget $ts))
  (, (exec (a $x) $p0 $t0) (exec (clocked (S $ts)) $p1 $t1)))
(exec proofs (, (target $c) (ev (: $p $c))) (, (proof $p $c)))
"""
    metta = kb + targets + """; backward chaining: a goal is a fact of the knowledge base, or the
; application of a proof of an implication to a proof of its premise
(= (bc (: $proof $conclusion) $depth) (match &self (kb (: $proof $conclusion)) (: $proof $conclusion)))
(= (bc (: (@ $f $x) $conclusion) (S $depth))
   (let* (((: $f (-> $premise $conclusion)) (bc (: $f (-> $premise $conclusion)) $depth))
          ((: $x $premise) (bc (: $x $premise) $depth)))
     (: (@ $f $x) $conclusion)))
!(bind! &p (new-space set))
!(let $r (collapse (match &self (target $c) (let (: $proof $c) (bc (: $proof $c) (S (S (S Z)))) (add-atom &p (proof $proof $c))))) done)
!(size-atom (collapse (match &p (proof $p $c) t)))
"""
    return mm2, metta, ['proof']


def reach_paths(nodes, out_degree, seed=0):
    # MORK wiki "MM2 tutorial: Reachability P4": breadth-first reachability
    # from one state, keeping each path to a newly reached state as a list of
    # actions and one step fact per action, over a generated transition graph
    rng = random.Random(seed)
    actions = [f'a{i}' for i in range(out_degree)]
    edges = sorted({(x, rng.randrange(1, nodes + 1), rng.choice(actions))
                    for x in range(1, nodes + 1) for _ in range(out_degree)})
    facts = ''.join(f'(t {x} {y} {a})\n' for x, y, a in edges)
    mm2 = '(init 1)\n' + facts + """(exec (0 1) (, (init $s) (t $s $t $a))
            (, (reachable Z $s $t) (step Z (path $s $t ($a)) $a) (visited $s) (visited $t)))
(exec (1 Z) (, (exec (1 $l) $ps $ts) (reachable $l $s $t) (t $t $u $a) (step $i (path $s $t $pa) $a2) (visited $v))
            (O (+ (exec (1 (S $l)) $ps $ts))
               (+ (reachable (S $l) $s $u))
               (- (reachable (S $l) $s $v))
               (+ (step $i (path $s $u ($a $pa)) $a2))
               (- (step $i (path $s $v ($a $pa)) $a2))
               (+ (step (S $l) (path $s $u ($a $pa)) $a))
               (- (step (S $l) (path $s $v ($a $pa)) $a))
               (+ (visited $u))))
"""
    metta = facts + """; the derived facts live in a set space, as MM2's do
!(bind! &r (new-space set))
!(let $r (collapse (match &self (t 1 $t $a)
    (let* (($x (add-atom &r (reachable Z 1 $t))) ($y (add-atom &r (step Z (path 1 $t ($a)) $a))))
      (add-atom &r (visited $t))))) done)
!(add-atom &r (visited 1))
; one level further: each move from a state reached at this level to one not
; yet visited extends every path to that state, as MM2's rule does; the
; visited set is read before the level marks its new states
(= (moves $l)
   (collapse (match &r (reachable $l $s $t)
     (match &self (t $t $u $a)
       (if (== (collapse (match &r (visited $u) t)) ()) (move $s $t $u $a) (empty))))))
(= (extend $l $s $t $u $a)
   (match &r (step $i (path $s $t $pa) $a2)
     (let* (($x (add-atom &r (reachable (S $l) $s $u)))
            ($y (add-atom &r (step $i (path $s $u ($a $pa)) $a2))))
       (add-atom &r (step (S $l) (path $s $u ($a $pa)) $a)))))
(= (level $l)
   (let $moves (moves $l)
     (if (== $moves ()) done
       (let* (($x (collapse (let (move $s $t $u $a) (superpose $moves) (extend $l $s $t $u $a))))
              ($y (collapse (let (move $s $t $u $a) (superpose $moves) (add-atom &r (visited $u))))))
         (level (S $l))))))
!(level Z)
!(size-atom (collapse (match &r (reachable $l $s $t) t)))
!(size-atom (collapse (match &r (step $i $p $a) t)))
!(size-atom (collapse (match &r (visited $v) t)))
"""
    return mm2, metta, ['reachable', 'step', 'visited']
