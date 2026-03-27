; Probe how collapse/case interact with expression-shaped results.

(= (identity $x) $x)
(= (coin) (superpose (heads tails)))

!(eval (identity (a b c)))
!(collapse (eval (identity (a b c))))
!(collapse (eval (coin)))

(= (once_via_collapse $expr)
   (let $results (collapse (eval $expr))
     (if (== $results ()) Empty (car-atom $results))))

(= (once_via_case $expr)
   (case (eval $expr)
     (($v $v)
      (Empty Empty))))

!(once_via_collapse (identity (a b c)))
!(once_via_case (identity (a b c)))
!(once_via_collapse (coin))
!(once_via_case (coin))
