; Guards the native path reads as the ordinary path reads them.
; A guard reading a variable no effect reads, in a join over one space,
; which the native path sums over: the guard reads every value of it.
!(bind! &g (new-space set))
!(bind! &m (new-space set))
!(bind! &s (new-space set))
!(add-atom &g (e 0 1))
!(add-atom &g (e 1 2))
!(add-atom &g (e 2 0))
!(add-atom &g (e 0 0))
!(add-atom &g (e 3 3))
!(add-atom &m (mark 0))
!(add-atom &m (mark 1))
!(add-atom &m (mark 3))
!(size-atom (collapse (match &g (, (e $x $y) (e $y $z)) (if (== (collapse (match &m (mark $z) t)) ()) (add-atom &s (got $x)) no))))
!(sort-atom (collapse (match &s (got $x) $x)))
; A round of a least fixpoint, its guard a call of a function of one
; equation that negates an existence test, its branch on presence empty,
; and its branch on absence two effects: an insert of the guard's own
; instance and a frontier named by it.  Two solutions in one round meet the
; same new state: the second reads it held, and answers nothing.
!(bind! &t (new-space set))
!(bind! &w (new-space set))
!(bind! &r (new-space set))
(= (in $f $x) (not (== (collapse (once (match &t (sat $f $x) t))) ())))
!(add-atom &r (rt 1 0))
!(add-atom &r (rt 2 0))
!(add-atom &r (rt 3 1))
!(add-atom &r (rt 3 2))
!(add-atom &r (rt 3 3))
!(add-atom &t (sat f0 3))
!(add-atom &w (fr f0 0 3))
(= (round $f $k $k1)
   (size-atom (collapse (match &w (fr $f $k $y) (match &r (rt $y $x)
     (if (in $f $x) (empty) (let $_ (add-atom &t (sat $f $x)) (add-atom &w (fr $f $k1 $x)))))))))
!(round f0 0 1)
!(sort-atom (collapse (match &w (fr f0 1 $x) $x)))
!(round f0 1 2)
!(sort-atom (collapse (match &w (fr f0 2 $x) $x)))
!(round f0 2 3)
!(sort-atom (collapse (match &t (sat f0 $x) $x)))
; The same rounds over one space, its relations binary, which the native
; path may sum over: a visit standing for several solutions makes the
; state once and answers once.
!(bind! &u (new-space set))
!(bind! &v (new-space set))
!(add-atom &u (pr 1 0))
!(add-atom &u (pr 2 0))
!(add-atom &u (pr 3 1))
!(add-atom &u (pr 3 2))
!(add-atom &u (pr 3 3))
!(add-atom &u (f0 3))
!(add-atom &v (in1 3))
(= (seen $x) (not (== (collapse (match &v (in1 $x) t)) ())))
(= (step $k $k1)
   (size-atom (collapse (match &u (, ($k $y) (pr $y $x))
     (if (seen $x) (empty) (let $_ (add-atom &v (in1 $x)) (add-atom &u ($k1 $x))))))))
!(step f0 f1)
!(step f1 f2)
!(step f2 f3)
!(sort-atom (collapse (match &v (in1 $x) $x)))
; The branch on presence an atom: every solution answers.
!(bind! &z (new-space set))
(= (mark-all $k)
   (size-atom (collapse (match &r (rt $y $x)
     (if (== (collapse (match &z (hit $x) t)) ()) (add-atom &z (hit $x)) old)))))
!(mark-all 0)
!(sort-atom (collapse (match &z (hit $x) $x)))
; An effect naming a variable the guard does not: the ordinary path makes
; it with the first solution meeting each state, which the native path
; does not choose.
!(bind! &q (new-space set))
!(bind! &p (new-space set))
(= (via $k)
   (size-atom (collapse (match &r (rt $y $x)
     (if (== (collapse (match &q (got $x) t)) ()) (let $_ (add-atom &q (got $x)) (add-atom &p (from $x $y))) old)))))
!(via 0)
!(size-atom (collapse (match &p (from $x $y) t)))
; Two additions of the guard's instance into a bag: the branch makes both,
; once for each instance.
!(bind! &bag (new-space))
(= (twice)
   (size-atom (collapse (match &r (rt $y $x)
     (if (== (collapse (match &bag (hit $x) t)) ()) (let $_ (add-atom &bag (hit $x)) (add-atom &bag (hit $x))) old)))))
!(twice)
!(size-atom (collapse (match &bag (hit $x) t)))
; A join over one space, summed over a variable no effect or guard reads:
; the visit standing for the two paths from 0 to 3 makes the pair once and
; answers once.
!(bind! &n (new-space set))
!(bind! &k (new-space set))
!(add-atom &n (e 0 1))
!(add-atom &n (e 0 2))
!(add-atom &n (e 1 3))
!(add-atom &n (e 2 3))
!(size-atom (collapse (match &n (, (e $x $y) (e $y $z)) (if (== (collapse (match &k (got $x $z) t)) ()) (add-atom &k (got $x $z)) (empty)))))
!(size-atom (collapse (match &k (got $x $z) t)))
