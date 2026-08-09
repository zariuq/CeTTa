; Substitution supplies a value; it does not retroactively turn the authored
; variable into a call.
!(let $code (quote (+ 1 2)) $code)

; eval and reduce are explicit value-to-code boundaries.
!(let $code (quote (+ 1 2)) (eval $code))
!(let $code (quote (+ 1 2)) (reduce $code))

; Typed-data fields are computations at authored call occurrences.  A value
; substituted through a variable occurrence remains data and is not
; retroactively reclassified as code.
!(-> (+ 1 2) Result)
!(let $code (quote (+ 1 2)) (-> $code Result))
!(: Tag (+ 1 2))
!(let $code (quote (+ 1 2)) (: Tag $code))
