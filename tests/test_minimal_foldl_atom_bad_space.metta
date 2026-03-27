; HE-backed helper surface: the explicit space argument is checked eagerly and
; a non-space value is rejected with BadArgType.

!(_minimal-foldl-atom () 0 $a $b (eval (+ $a $b)) 1)
