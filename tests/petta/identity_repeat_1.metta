; A deep non-tail traversal, run once: every frame identity it held
; is released once its answer is out.
(= (map-flat $f ()) ())
(= (map-flat $f (cons $x $xs)) (cons ($f $x) (map-flat $f $xs)))
(= (range $n) (if (== $n 0) () (cons $n (range (- $n 1)))))
!(let $temp (map-flat (+ 1) (range 5000)) (length $temp))
