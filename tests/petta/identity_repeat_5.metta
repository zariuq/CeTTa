; The same deep non-tail traversal, run five times: every frame identity
; it held is released once its answer is out, so the repeated runs leave
; the identities of one run live and peak at the same count.
(= (map-flat $f ()) ())
(= (map-flat $f (cons $x $xs)) (cons ($f $x) (map-flat $f $xs)))
(= (range $n) (if (== $n 0) () (cons $n (range (- $n 1)))))
!(let $temp (map-flat (+ 1) (range 5000)) (length $temp))
!(let $temp (map-flat (+ 1) (range 5000)) (length $temp))
!(let $temp (map-flat (+ 1) (range 5000)) (length $temp))
!(let $temp (map-flat (+ 1) (range 5000)) (length $temp))
!(let $temp (map-flat (+ 1) (range 5000)) (length $temp))
