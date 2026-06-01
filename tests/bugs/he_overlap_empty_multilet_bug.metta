; Repro: multi-result let must not evaluate its body for Empty outcomes.

(= (case-empty-fragment)
   (case (superpose (a b))
         (($x (if (== $x a) Empty ok)))))

!(assertEqual (let $tmp (case-empty-fragment) done) done)
