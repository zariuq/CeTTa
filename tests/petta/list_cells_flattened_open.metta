; Lists whose tail is open when they are built: a cons onto an unbound tail,
; and the tail of a list decomposed before it is known.  Each stays a chain of
; cells until the tail is bound, and a reader then flattens it.
!(let $l (cons-atom 1 $t) (let $t (2 3) (== $l (1 2 3))))
!(let ($h $t) (decons-atom $xs) (let $xs (a b c) (size-atom $t)))
