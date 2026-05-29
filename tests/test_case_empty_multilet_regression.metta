; Regression: multi-result let must not run its body for Empty branch outcomes.

(= (case-empty-fragment)
   (case (superpose (a b))
         (($x (if (== $x a) Empty ok)))))

!(assertEqual (collapse (case-empty-fragment)) (ok))
!(assertEqual (let $tmp (case-empty-fragment) done) done)
