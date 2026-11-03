; The list of collector_first_baseline.metta, with no stable bytes first.
(= (map-flat $f ()) ())
(= (map-flat $f (cons $x $xs)) (cons ($f $x) (map-flat $f $xs)))
(= (range $n) (if (== $n 0) () (cons $n (range (- $n 1)))))
!(length (let $w 1 (map-flat (+ 1) (range 100000))))
