; A walk over a ten-cell list.  Each step takes the list through
; (cons $p $rest), which structural matching does not discharge, so the
; equation is opened by solving its head against the call, and its body reads
; the head's bindings through the activation's frame.
(= (inner-empty $p) (let $c (empty) $p))
(= (count-cells ()) 0)
(= (count-cells (cons $p $rest)) (let $u (collapse (inner-empty $p)) (+ 1 (count-cells $rest))))
!(count-cells (1 2 3 4 5 6 7 8 9 10))
