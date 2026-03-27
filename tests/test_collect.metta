; collect: evaluator-backed deterministic collection over a nondeterministic
; stream, paired with select.
; Unlike select, collect always packages results into a single expression, so
; an expression-valued result becomes one collected item.

(= (coin) (superpose (heads tails)))
(= (identity $x) $x)
(= (mixed) (superpose ((Error bad boom) ok done)))

!(add-atom &self (= marker a))

!(assertEqual (collect (coin)) (heads tails))
!(assertEqual (collect (identity (a b c))) ((a b c)))
!(assertEqual (collect (superpose ())) ())
!(assertEqual (collect (mixed)) (ok done))
!(assertEqual (collect (match &self (= marker $x) $x)) (a))
!(assertEqual (let $r (collect (match &self (= marker $x) $x)) $r) (a))
