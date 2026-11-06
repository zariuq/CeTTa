; Admission by normal form keeps its guards (a let's variable bound by its
; own bound expression compares; a case whose first-arm variable is bound
; falls through to its next arm; an arm written Empty is the case's own).
(= (f) (let $y (let $y 1 2) $y))
!(f)
(= (g) (let* (($y (let $y 1 2))) $y))
!(g)
(= (h) (let* (($z 0) ($y (let $y 1 2))) $y))
!(h)
(= (k) (let $y (let $y 2 2) $y))
!(k)
