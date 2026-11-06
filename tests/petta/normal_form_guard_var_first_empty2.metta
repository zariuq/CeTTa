; Admission by normal form keeps its guards (a let's variable bound by its
; own bound expression compares; a case whose first-arm variable is bound
; falls through to its next arm; an arm written Empty is the case's own).
(= (f) (case (empty) (($v 1) (Empty 2))))
!(collapse (f))
(= (q $x) (case (if (> $x 0) $x (empty)) (($v (* $v 2)) (Empty 0))))
!(q 0)
!(q 3)
!(collapse (q 0))
(= (r $x) (case (if (> $x 0) $x (empty)) (($v (* $v 2)) (Empty 0))))
(= (s $n) (r $n))
!(collapse (s 0))
!(s 0)
