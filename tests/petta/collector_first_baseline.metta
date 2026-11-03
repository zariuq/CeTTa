; A list built after the compiled tier wrote a few stable bytes into the
; machine's old generation: the first collection sets the collector's
; baseline, so the list is promoted once (collector_first_baseline_control
; builds it with none written).
(= (map-flat $f ()) ())
(= (map-flat $f (cons $x $xs)) (cons ($f $x) (map-flat $f $xs)))
(= (big) (T (T (T (T (T (T (T (T a)))))))))
(= (pick) (superpose (1 2)))
(= (twice) (let $k (pick) (let $b (big) (let $u (case $b (((Q $z) no) ($other yes))) (let $v (case $b (((Q $z) no) ($other yes))) $k)))))
(= (range $n) (if (== $n 0) () (cons $n (range (- $n 1)))))
!(length (let $w (once (twice)) (map-flat (+ 1) (range 100000))))
