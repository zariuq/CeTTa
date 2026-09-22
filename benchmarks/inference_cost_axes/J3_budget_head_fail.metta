; J3: many open heads, most attempts fail at the first constructor.
; Dummy queries at each level miss every head; the base query hits `result`.
; Size ladder: change the repeat count (256, 1024, 4096).
(= (j3-gtz (: ax (A $x))) (ax $x))
(= (j3-gtz (: by (B $x))) (by $x))
(= (j3-gtz (: cz (C $x))) (cz $x))
(= (j3-gtz (: (mp $f $x) (A $b))) mp-hit)
(= (j3-gtz (: result (A $x))) (ok $x))
(= (j3 $n)
   (if (== $n 0)
       (j3-gtz (: result (A leaf)))
       (let $_ (collapse (j3-gtz (: dummy (Z leaf))))
         (j3 (- $n 1)))))
!(j3 4096)
