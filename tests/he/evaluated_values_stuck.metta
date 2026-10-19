; A call that comes back stuck after its arguments are evaluated is a value:
; (fr (h)) answers (fr a), twice, and a one-line function over it answers
; the same.  he answers so; he-compat answers as Rust HE does, which marks
; only an answer equal to the call before its arguments were evaluated and
; so evaluates (fr a) again.
(= (fr $x) NotReducible)
(= (fr $x) NotReducible)
(= (h) a)
(= (g $x) (wrap $x))
(= (g1 $x) (g $x))
!(fr (h))
!(wrap (fr (h)))
!(g (fr (h)))
!(g1 (fr (h)))
!(let $c (collapse (fr (h))) (superpose $c))
