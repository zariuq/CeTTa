; A removal takes every row its instance meets, a row holding a variable
; too: (q $z) meets both (q 7), which the removal names, and (q 5), which a
; later pattern reads, though no instance of one is an instance of the other.
(p 1)
(p 2)
(q $z)
(q 5)
!(collapse (match &self (p $x) (match &self (q 5) (remove-atom &self (q 7)))))
!(collapse (match &self (q $v) $v))
!(bind! &c (new-space))
!(add-atom &c (p 1))
!(add-atom &c (p 2))
!(add-atom &c (q $z))
!(collapse (match &c (, (p $x) (q 5)) (remove-atom &c (q 7))))
!(collapse (match &c (q $v) $v))
; A removal whose head holds no row with a variable reads its rows on the
; snapshot as before: the rules of (= ...) are under another head.
(= (twice $n) (* 2 $n))
(edge 1 2)
(edge 2 3)
(edge 3 1)
(seen 1)
(seen 2)
!(size-atom (collapse (match &self (edge $x $y) (match &self (edge $y $z) (remove-atom &self (seen $x))))))
!(collapse (match &self (seen $s) $s))
!(collapse (match &self (edge $x $y) (pair $x $y)))
!(twice 4)

; Counted, a removal's answers are only numbered, so the native path may
; make it: it still declines where the removal can take a row holding a
; variable that a leg reads, as removing (q 7) takes (q $z).
!(bind! &n (new-space))
!(add-atom &n (p 1))
!(add-atom &n (p 2))
!(add-atom &n (q $z))
!(add-atom &n (q 5))
!(size-atom (collapse (match &n (p $x) (match &n (q 5) (remove-atom &n (q 7))))))
!(collapse (match &n (q $v) $v))
