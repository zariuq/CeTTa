; Deep facts kept as a skeleton and its holes answer as rows do: facts of
; a set holding variables (kept as their alpha keys), read inside equations
; with fixed and open holes, counted, added again as variants, removed;
; ground deep facts of a multiset with copies; facts of the head that do not
; fit the skeleton; and joins over the relation.
!(bind! &t (new-space set))
; goals (g depth weight (C (/ premise rest) proof)), some premises open
(= (seed $k $n)
   (if (>= $k $n) done
       (let* (($k1 (+ $k 1))
              ($d (% $k 4))
              ($w (% $k 3))
              ($a (add-atom &t (g $d $w (C (/ (> p (q $k)) (> $x $x)) (r $k)))))
              ($b (add-atom &t (g $d $w (C (/ (> $y (q $k)) (/ $y s)) (m (r $k)))))))
         (seed $k1 $n))))
!(seed 0 30)
; a fact of the head with another spine, and one of another length
!(add-atom &t (g 1 1 (C done (r 99))))
!(add-atom &t (g 1 1))
; counts: every fact, by depth, by weight, by premise
!(size-atom (collapse (match &t (g $d $w $c) t)))
!(size-atom (collapse (match &t (g 2 $w $c) t)))
!(size-atom (collapse (match &t (g $d 1 (C (/ $a $b) $f)) t)))
!(size-atom (collapse (match &t (g $d $w (C (/ (> p $z) $b) $f)) $z)))
; the same variable twice
!(size-atom (collapse (match &t (g $d $d $c) t)))
; read inside an equation: a step that closes a premise with a rule
(rule (> $u (q $v)) (closed $v))
(= (step $d)
   (match &self (rule $p $out)
     (match &t (g $d $w (C (/ $p $rest) $f)) (add-atom &t (done $out $w)))))
!(size-atom (collapse (step 0)))
!(size-atom (collapse (step 3)))
!(size-atom (collapse (match &t (done $o $w) t)))
; a step that derives deeper goals from goals, as a search does
(= (extend $d $e)
   (match &t (g $d $w (C (/ $p $rest) $f))
     (add-atom &t (g $e $w (C $rest (x $f))))))
!(size-atom (collapse (extend 1 7)))
!(size-atom (collapse (match &t (g 7 $w $c) t)))
; variants of held facts add nothing to a set
!(add-atom &t (g 0 0 (C (/ (> p (q 0)) (> $u $u)) (r 0))))
!(add-atom &t (g 0 0 (C (/ (> $v (q 0)) (/ $v s)) (m (r 0)))))
!(size-atom (collapse (match &t (g $d $w $c) t)))
; removal, of a ground fact and of an open one by a variant
!(remove-atom &t (g 1 1 (C done (r 99))))
!(remove-atom &t (g 2 2 (C (/ (> p (q 2)) (> $a $a)) (r 2))))
!(size-atom (collapse (match &t (g $d $w $c) t)))
!(size-atom (collapse (match &t (g 2 2 $c) t)))
; the facts themselves, as a bag
!(sort-atom (collapse (match &t (g 3 $w (C $c (r $n))) ($w $n))))
; a join over the relation and the rules
!(size-atom (collapse (match &t (, (g $d $w (C (/ $p $r) $f)) (g $d $w (C (/ $p2 $r2) (m $f)))) t)))
; ground deep facts in a multiset, with copies
!(bind! &b (new-space))
(= (fill $k $n)
   (if (>= $k $n) done
       (let* (($k1 (+ $k 1))
              ($a (add-atom &b (e $k (f (h $k) (i 1)))))
              ($b (add-atom &b (e $k (f (h $k) (i 1))))))
         (fill $k1 $n))))
!(fill 0 10)
!(size-atom (collapse (match &b (e $k $v) t)))
!(size-atom (collapse (match &b (e 3 (f $x $y)) $x)))
!(remove-atom &b (e 3 (f (h 3) (i 1))))
!(size-atom (collapse (match &b (e 3 $v) t)))
!(size-atom (collapse (match &b (e $k (f (h $k) $y)) t)))
