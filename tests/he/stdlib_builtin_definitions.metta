; HE built-ins answer as Rust HE's definitions of them: get-type types its
; subject in a space it is given, switch-minimal returns its template as
; written, and assert's message holds True as the parser reads it.
(: a A)
!(switch-minimal 5 ((4 False) (5 (+ 1 2))))
!(collapse (switch-minimal 5 ((5 NotReducible) ($_ other))))
!(switch-minimal (a 7) (((a $x) (+ $x 1)) ($_ other)))
!(eval (get-type 5 &self))
!(eval (get-type a &self))
!(type-cast 42 Number &self)
!(type-cast a A &self)
!(collapse (type-cast a B &self))
!(let $c (collapse (assert (and True False))) (unify $c ((Error $x ($b not True))) same differ))
!(collapse (eval (get-type 5 foo)))
