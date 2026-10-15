; A recursion over a long list built cell by cell stays linear: matching an
; equation head of fixed length against the list reads only as many cells as
; the pattern has elements, whatever the list ends in.  A count-only binding
; of the mapped list sends each step through the pure-call matcher.
(= (lc-range $n) (if (== $n 0) () (cons $n (lc-range (- $n 1)))))
(= (lc-map $f ()) ())
(= (lc-map $f (cons $x $xs)) (cons ($f $x) (lc-map $f $xs)))
!(let $r (lc-range 20000) (let $m (lc-map (+ 1) $r) (length $m)))
!(let $r (lc-range 3) (let $m (lc-map (+ 1) $r) $m))
