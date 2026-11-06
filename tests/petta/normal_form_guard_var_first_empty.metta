; Admission by normal form keeps its guards (a let's variable bound by its
; own bound expression compares; a case whose first-arm variable is bound
; falls through to its next arm; an arm written Empty is the case's own).
(= (f) (case (empty) (($v 1) (Empty 2))))
!(f)
(= (g) (case (empty) ((Empty 2) ($v 1))))
!(g)
(= (h) (case (empty) ((1 1) (Empty 2))))
!(h)
(= (k) (case (superpose (3 4)) (($v (+ $v 10)) (Empty 2))))
!(k)
(= (m) (let $v (empty) 1))
!(m)
