; A guard probing facts a relation holds with no record of each: facts a
; join added, which the relation took by their columns, are held all the
; same, and the guard takes the other branch for each.
!(bind! &a (new-space set))
!(bind! &b (new-space set))
!(bind! &t (new-space set))
!(bind! &h (new-space set))
!(bind! &u (new-space set))
(= (fill $s $k $n) (if (>= $k $n) done (let $_ (add-atom $s (src $k)) (fill $s (+ $k 1) $n))))
!(fill &a 0 10)
!(fill &b 0 5)
!(size-atom (collapse (match &a (src $x) (add-atom &t (seen $x)))))
!(size-atom (collapse (match &b (src $x) (add-atom &h (half $x)))))
; every probe held: no addition
!(size-atom (collapse (match &a (src $x) (if (== (collapse (match &t (seen $x) t)) ()) (add-atom &u (new $x)) old))))
!(size-atom (collapse (match &u (new $x) t)))
; half the probes held: the rest added
!(size-atom (collapse (match &a (src $x) (if (== (collapse (match &h (half $x) t)) ()) (add-atom &u (rest $x)) old))))
!(sort-atom (collapse (match &u (rest $x) $x)))
