; Substitution supplies a value; it does not retroactively turn the authored
; variable into a call.
!(let $code (quote (+ 1 2)) $code)

; eval and reduce are explicit value-to-code boundaries.
!(let $code (quote (+ 1 2)) (eval $code))
!(let $code (quote (+ 1 2)) (reduce $code))
