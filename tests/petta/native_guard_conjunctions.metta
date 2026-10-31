; Guards whose tests join comparisons of the solutions' values to an
; absence: `and` that must hold, `or` that must fail, `not`, a function of
; one equation, and ifs nested with the same other branch.  Each reads as
; one native query or keeps the ordinary path; the answers are the same.
!(bind! &g (new-space set))
!(add-atom &g (k 1 2))
!(add-atom &g (k 2 1))
!(add-atom &g (k 2 3))
!(add-atom &g (k 3 2))
!(add-atom &g (k 3 4))
!(add-atom &g (k 4 3))
!(add-atom &g (k 1 3))
!(add-atom &g (k 3 1))
; strangers two steps apart, as LDBC's BI 18 asks
(= (strangers $a $b) (and (not (== $a $b)) (== (collapse (match &g (k $a $b) t)) ())))
!(bind! &m (new-space set))
!(size-atom (collapse (match &g (k $p $q) (match &g (k $q $r)
   (if (strangers $p $r) (add-atom &m (two $p $r)) (empty))))))
!(sort-atom (collapse (match &m (two $p $r) ($p $r))))
; the same, as nested ifs sharing their other branch
!(bind! &n (new-space set))
!(size-atom (collapse (match &g (k $p $q) (match &g (k $q $r)
   (if (== $p $r) (empty) (if (== (collapse (match &g (k $p $r) t)) ()) (add-atom &n (two $p $r)) (empty)))))))
!(sort-atom (collapse (match &n (two $p $r) ($p $r))))
; or that must fail, with != for the comparison and for the absence
!(bind! &o (new-space set))
!(size-atom (collapse (match &g (k $p $q) (match &g (k $q $r)
   (if (or (== $p $r) (!= (collapse (match &g (k $p $r) t)) ())) (empty) (add-atom &o (two $p $r)))))))
!(sort-atom (collapse (match &o (two $p $r) ($p $r))))
; two comparisons and an absence, the other branch an atom
!(bind! &w (new-space))
!(size-atom (collapse (match &g (k $p $q) (match &g (k $q $r)
   (if (and (!= $p $q) (and (!= $q $r) (== () (collapse (match &g (k $r $p) t))))) (add-atom &w (far $p $r)) skipped)))))
!(size-atom (collapse (match &w (far $p $r) t)))
; an addition of the guard's own instance under a comparison: a set holds
; each new pair once, and a pair the comparison refuses is never added
!(bind! &s (new-space set))
!(size-atom (collapse (match &g (k $p $q) (match &g (k $q $r)
   (if (and (!= $p $r) (== (collapse (match &s (seen $p $r) t)) ())) (add-atom &s (seen $p $r)) (empty))))))
!(sort-atom (collapse (match &s (seen $p $r) ($p $r))))
; and that must fail is a disjunction: the ordinary path answers it
!(bind! &d (new-space set))
!(size-atom (collapse (match &g (k $p $q) (match &g (k $q $r)
   (if (and (== $p $r) (!= (collapse (match &d (dead $p) t)) ())) (empty) (add-atom &d (alive $p $r)))))))
!(size-atom (collapse (match &d (alive $p $r) t)))
; a test asking for a row, not its absence, keeps the ordinary path
!(bind! &e (new-space set))
!(size-atom (collapse (match &g (k $p $q) (match &g (k $q $r)
   (if (and (!= $p $r) (!= (collapse (match &g (k $p $r) t)) ())) (add-atom &e (tri $p $q $r)) (empty))))))
!(sort-atom (collapse (match &e (tri $p $q $r) ($p $q $r))))
; values equal by value, not by id: (== 1 1.0) holds
!(bind! &f (new-space set))
!(add-atom &f (val a 1))
!(add-atom &f (val b 2))
!(add-atom &f (val c 1))
!(bind! &u (new-space set))
!(size-atom (collapse (match &f (val $p $x) (if (== $x 1.0) (add-atom &u (one $p)) (empty)))))
!(sort-atom (collapse (match &u (one $p) $p)))
!(bind! &h (new-space set))
!(add-atom &h (hv a (n 1.0)))
!(add-atom &h (hv b (m 2)))
!(add-atom &h (hv c (n 1)))
!(bind! &v (new-space set))
!(size-atom (collapse (match &h (, (hv $p $x) (hv $q $y)) (if (and (!= $p $q) (== $x $y)) (add-atom &v (alike $p $q)) (empty)))))
!(sort-atom (collapse (match &v (alike $p $q) ($p $q))))
