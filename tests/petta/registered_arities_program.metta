; A program's equations of a registered name define it at their arity; the
; registration's arities keep their builtins.
(= (max $list) (car-atom $list))
(= (sqrt $x) (* $x $x))
!(max (7 8))
!(max 3 4)
!(max)
!(sqrt 3)
