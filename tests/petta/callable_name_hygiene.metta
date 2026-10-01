; Fresh callable labels must not take an authored function's name.
(= (lambda_1 $x) (* $x 100))
(= (lambda_2 $x) (* $x 200))
(= (lambda_3 $x) (* $x 300))
!(collapse (lambda_1 2))
!(collapse (let $f (|-> ($x) (+ $x 1)) ($f 2)))
!(collapse (lambda_1 2))
!(collapse (lambda_2 2))
!(collapse (lambda_3 2))
; Transporting the generated code must not reclassify the authored symbol.
!(collapse (let $f (|-> ($x) (+ $x 1)) (let $_ (translatePredicate (atom $f)) (let $g (copy_term lambda_1) ($g 2)))))
!(collapse (let $f (|-> ($x) (+ $x 1)) (let $_ (translatePredicate (atom $f)) (let $g (copy_term lambda_2) ($g 2)))))
; repr returns a string; repra returns a Prolog atom. They remain distinct.
!(let $f (|-> ($x) (+ $x 1)) (== (repr $f) (repra $f)))
!(let $f (|-> () 42) (== (repr $f) (repra $f)))
!(let $f (|-> ($x) (+ $x 1)) (let $g (copy_term $f) (== (repr $f) (repr $g))))
; A transported callable's public rendering recovers the same owned code.
!(let $f (|-> ($x) (+ $x 1)) (let $_ (translatePredicate (atom $f)) (let $_ (translatePredicate (atom_to_term (repra $f) $g $bindings)) (== $f $g))))
!(let $f (|-> () 42) (let $_ (translatePredicate (atom $f)) (let $_ (translatePredicate (atom_to_term (repra $f) $g $bindings)) (== $f $g))))
!(let $f (|-> ($x $y) (+ $x $y)) (let $g ($f 1) (let $h (copy_term $g) (== (repra $g) (repra $h)))))
!(let $f (|-> ($x $y) (+ $x $y)) (let $g ($f 1) (let $_ (translatePredicate (compound $g)) (let $_ (translatePredicate (atom_to_term (repra $g) $h $bindings)) ($h 2)))))
; Authored lookalike expressions remain ordinary data.
!(is-expr (quote (Lam (identity 1) (idx 0))))
!(is-expr (quote (partial lambda_1 (1))))
!(let $f (|-> ($x) (+ $x 1)) (== (repr $f) (repr (quote (Lam (identity 1) (idx 0))))))
