; A set holds a fact once, as a row or in its relation: facts a relation
; holds, added again where it takes none, are not added twice, and a read
; of the set by position meets every fact once.
(edge 2 1)
(edge 1 2)
(edge 2 2)
!(bind! &s (new-space set))
(= (region0 $k) (match &self (edge $k $y) (put0 $k $y)))
!(if (== (collapse (region0 0)) ()) none some)
(edge 2 (: h Number))
!(let $ps (collapse (match &self (, (edge $x $y) (edge $y $z)) (hop $x $z))) (size-atom (collapse (let $a (superpose $ps) (add-atom &s $a)))))
!(let $h (collapse (get-atoms &s)) (sort-atom $h))
