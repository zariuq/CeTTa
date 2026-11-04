; Joins over facts holding variables, read from a relation of deep facts:
; subgoals kept in a set, stepped back by inverse modus ponens (a fresh
; variable in each new goal) and by closing a premise with an axiom (the
; axiom's variables and the goal's meet both ways), each round counted.
; The rounds bind a depth by a pure function (upto) before each match.  By
; the time the set holds the subgoals counted toward a relation, later ones
; go to it, so later rounds read rows and kept facts together;
; the same variable across legs; the occurs check; a step whose effects
; meet its own reads; an atom whose equality is not its identity.
(axiom 1 (> $p (> $s $p)))
(axiom 2 (> (> $p (> $s $x)) (> (> $p $s) (> $p $x))))
(axiom 3 (> (> (! $p) (! $s)) (> $s $p)))
!(bind! &t (new-space set))
(target 13 (C (> (> (> p s) x) (> s x)) $x))
!(match &self (target $mps (C $ta $tx)) (add-atom &t (sol $mps 1 (C (/ $ta $ta) I))))
(= (upto $n) (if (< $n 0) (empty) $n))
(= (upto $n) (if (< $n 1) (empty) (let $m (- $n 1) (upto $m))))
(= (axiom-step $sk $k)
   (let* (($hi (upto $k)) ($shi (+ $hi 1)))
     (match &self (axiom $r $c)
       (match &t (sol $sk $shi (C (/ $c $b) $f))
         (add-atom &t (sol $k $hi (C $b ($r $f))))))))
(= (mp-step $sk $k)
   (let* (($hi (upto $k)) ($shi (+ $hi 1)))
     (match &t (sol $sk $hi (C (/ $b $c) $f))
       (add-atom &t (sol $k $shi (C (/ (> $a $b) (/ $a $c)) (M $f)))))))
(= (rounds $sk)
   (if (< $sk 1) done
       (let* (($k (- $sk 1))
              ($x (size-atom (collapse (axiom-step $sk $k))))
              ($y (size-atom (collapse (mp-step $sk $k)))))
         (rounds $k))))
!(match &self (target $mps $_) (rounds $mps))
!(size-atom (collapse (match &t (sol $a $b $c) t)))
!(size-atom (collapse (match &t (sol 3 $b $c) t)))
!(size-atom (collapse (match &t (sol $a 2 (C (/ (> $p $q) $r) $f)) t)))
!(sort-atom (collapse (match &t (sol 0 0 (C $ta $tx)) $tx)))
; the same variable in two legs, and both sides binding
!(bind! &u (new-space set))
(= (fill-u $k $n)
   (if (>= $k $n) done
       (let* (($a (add-atom &u (link $k (pair (f $v) $v))))
              ($b (add-atom &u (link $k (pair $w (g $k))))))
         (fill-u (+ $k 1) $n))))
!(fill-u 0 6)
; additions, which the joins over facts holding variables make before any
; count gives the relation back to the rows:
; a variable shared by two holes of one fact, the occurs check, and a
; template naming the pattern's variables in another order
!(bind! &hits (new-space set))
!(size-atom (collapse (match &u (, (link $k (pair (f $z) $y)) (link $k (pair $y (g $k)))) (add-atom &hits (two $k $z)))))
!(size-atom (collapse (match &u (link $k (pair $q $q)) (add-atom &hits (same $k)))))
!(size-atom (collapse (match &u (link $k (pair $q (f $q))) (add-atom &hits (cycle $k)))))
!(size-atom (collapse (match &u (link $k (pair $q $w)) (add-atom &hits (order $w $q $k)))))
!(sort-atom (collapse (match &hits (two $k (f (g $k))) $k)))
!(sort-atom (collapse (match &hits (same $k) $k)))
!(sort-atom (collapse (match &hits (cycle $k) $k)))
!(sort-atom (collapse (match &hits (order $x (f $x) $k) $k)))
!(sort-atom (collapse (match &hits (order (g $k) $y $k) $k)))
!(size-atom (collapse (match &hits $h t)))
!(size-atom (collapse (match &u (, (link $k (pair (f $z) $y)) (link $k (pair $y (g $k)))) t)))
!(sort-atom (collapse (match &u (, (link $k (pair (f $z) $y)) (link $k (pair $y (g $k)))) ($k $z))))
!(size-atom (collapse (match &u (link $k (pair $q $q)) t)))
; the occurs check: no fact (link k (pair (f v) v)) meets (link k (pair $q (f $q)))
!(size-atom (collapse (match &u (link $k (pair $q (f $q))) t)))
; a chain whose additions meet a later read of its own, which PeTTa reads
; afresh for each row before it: the ordinary path answers
!(bind! &w (new-space set))
!(add-atom &w (seed 0 a))
!(add-atom &w (seed 0 b))
!(add-atom &w (step 0 (s $x)))
(= (grow) (match &w (seed $i $n) (match &w (step $i $t) (add-atom &w (step $i (s $t))))))
!(size-atom (collapse (grow)))
!(size-atom (collapse (match &w (step $i $t) t)))
; a value whose equality is not its identity
!(bind! &v (new-space set))
!(add-atom &v (val (w 1.5 $z)))
!(add-atom &v (val (w 2 $z)))
!(size-atom (collapse (match &v (val (w 1.5 $q)) t)))
; a producer that reads a space its matches write: it is no pure stream, and
; each of its answers is read after the matches before it
!(bind! &q (new-space set))
!(add-atom &q (lvl 0))
(= (chain $n) (if (> $n 2) (empty) (match &q (lvl $n) (superpose ($n (chain (+ $n 1)))))))
(= (grow-q) (let* (($l (chain 0)) ($m (+ $l 1))) (match &q (lvl $l) (add-atom &q (lvl $m)))))
!(size-atom (collapse (grow-q)))
!(size-atom (collapse (match &q (lvl $l) t)))
; a pure stream whose later run the host declines: the evaluator runs it,
; after the runs before it
!(bind! &r (new-space set))
!(add-atom &r (cell 1 (s $z)))
!(add-atom &r (cell 0 (t $z)))
(= (fold-cells) (let* (($l (upto 1))) (match &r (cell $l $x) (add-atom &r (cell 0 (s $x))))))
!(size-atom (collapse (fold-cells)))
!(sort-atom (collapse (match &r (cell $l $x) $l)))
; a part of a stored key read twice in one instance: the instance numbers
; its variables once, whatever numbers the key gave them
!(bind! &pp (new-space set))
!(bind! &kk (new-space set))
!(bind! &twice (new-space set))
(= (fill-pp $k $n)
   (if (>= $k $n) done
       (let $done (add-atom &pp (fact $k (> $a $b) (> $c $d)))
         (fill-pp (+ $k 1) $n))))
!(fill-pp 0 3)
!(size-atom (collapse (match &pp (fact $k $p $q) (add-atom &kk (fact $k $p $q)))))
!(size-atom (collapse (match &kk (fact $k $p $q) (add-atom &twice (twice $k $q $q)))))
!(sort-atom (collapse (match &twice (twice $k (> a b) (> $u $v)) ($k $u $v))))
; a set holding a fact as a row, its head's later facts in a relation:
; the join's instance, a variant of that row, is not added again
!(bind! &once (new-space set))
!(add-atom &once (deep 0 (> $a $b)))
!(add-atom &once (deep 1 (> $a $b)))
!(add-atom &once (deep 2 (> $b $a)))
!(size-atom (collapse (match &kk (fact $k $p $q) (add-atom &once (deep 0 $p)))))
!(size-atom (collapse (match &once (deep $k $f) t)))
