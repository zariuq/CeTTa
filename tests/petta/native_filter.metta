; Filters on the solutions' values: `(if (== A B) X Y)` with A and B
; variables or atoms, one branch effects and the other an atom or `(empty)`.
!(bind! &g (new-space set))
!(bind! &s (new-space set))
!(add-atom &g (e 0 1))
!(add-atom &g (e 1 2))
!(add-atom &g (e 2 0))
!(add-atom &g (e 0 0))
!(add-atom &g (e 3 3))
!(add-atom &g (e 1 1))
; effects where the values differ, nothing where they are equal
!(size-atom (collapse (match &g (e $x $y) (if (== $x $y) (empty) (add-atom &s (ne $x $y))))))
!(sort-atom (collapse (match &s (ne $x $y) ($x $y))))
; effects where they are equal, an atom where they differ
!(size-atom (collapse (match &g (e $x $y) (if (== $x $y) (add-atom &s (loop $x)) skip))))
!(sort-atom (collapse (match &s (loop $x) $x)))
; an atom as an operand
!(size-atom (collapse (match &g (e $x $y) (if (== $y 0) (add-atom &s (into0 $x)) (empty)))))
!(sort-atom (collapse (match &s (into0 $x) $x)))
; a join over one space summed over a variable the effects do not read:
; the filter reads it
!(size-atom (collapse (match &g (, (e $x $y) (e $y $z)) (if (== $z 0) skip (add-atom &s (far $x))))))
!(sort-atom (collapse (match &s (far $x) $x)))
; words joined on a masked view, the letter masked differing
!(bind! &v (new-space set))
!(bind! &n (new-space set))
!(add-atom &v (mv (N b c) a (a b c)))
!(add-atom &v (mv (N b c) b (b b c)))
!(add-atom &v (mv (a N c) b (a b c)))
!(add-atom &v (mv (a N c) a (a a c)))
!(add-atom &v (mv (a b N) c (a b c)))
!(size-atom (collapse (match &v (mv $key $b1 $x) (match &v (mv $key $b2 $y) (if (== $b1 $b2) (empty) (add-atom &n (nb $x $y)))))))
!(sort-atom (collapse (match &n (nb (a b c) $y) $y)))
