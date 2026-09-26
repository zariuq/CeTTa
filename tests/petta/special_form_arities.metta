; A special form is syntax only at the arities SWI-PeTTa's translator reads.
; At any other arity it is an ordinary application of its name: data, or a
; partial for `let` and `let*`, which SWI-PeTTa registers as functions
; without defining them.  Positive controls at the read arities follow, and
; the same forms built at run time.
(= (g $c) (Clause $c))
!(g (if X))
!(g (case 1))
!(g (let a b))
!(g (chain a b))
!(g (let* a))
!(g (sealed a))
!(g (once a b))
!(g (collapse a b))
!(if True 1 2)
!(if False 1)
!(case 1 ((1 one) (2 two)))
!(let $x 1 (+ $x 1))
!(let* (($x 1)) $x)
!(once (superpose (1 2)))
!(let $f (cons-atom case (1)) (g $f))
!(let $f (cons-atom sealed (a)) (eval $f))
