; The first of two overlapping equations answers while the second is still
; untried, so each iteration of this loop leaves a pending alternative behind.
(= (cnt ()) 0)
(= (cnt $xs) (let ($h $t) (decons-atom $xs) (+ 1 (cnt $t))))
(= (tick) (and True (case (cnt (a b c)) ((3 True) ($o False)))))
(= (run $n) (if (== $n 0) done (let $_ (tick) (run (- $n 1)))))
!(run 200)
