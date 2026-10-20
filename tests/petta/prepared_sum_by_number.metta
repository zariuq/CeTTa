!(import! &self (library list))

(= (prepared-sum:identity $value) $value)
(= (prepared-sum:pair-value (pair $value)) $value)
(= (prepared-sum:choice $value)
   (superpose ($value (+ $value 100))))
(= (prepared-sum:effect $value)
   (let $_ (println! (PreparedSumEffect $value)) $value))
(= (prepared-sum:singleton $value) ($value))
(= (prepared-sum:append $left $right) (union-atom $left $right))
(= (prepared-sum:add $left $right) (+ $left $right))

; Integers, floats and duplicate occurrences retain left-fold semantics.
!(list:sum-by prepared-sum:identity (1 2 3 3))
!(list:sum-by prepared-sum:identity (1 2.5 -0.5))
!(list:sum-by prepared-sum:pair-value
    ((pair 2) (pair 5) (pair 2)))

; The public abstraction is a general, source-ordered fold-map.  It is useful
; for non-numeric monoids without asking the runtime to infer algebraic laws.
!(list:fold-map prepared-sum:singleton prepared-sum:append () (a b a))

; Empty input has the additive identity through the authored fallback.
!(list:sum-by prepared-sum:identity ())

; A nondeterministic projection is outside the fused region.  The authored
; recursive fold remains authoritative and preserves every choice.
!(collapse (list:sum-by prepared-sum:choice (1 2)))
!(collapse (list:fold-map prepared-sum:choice prepared-sum:add 0 (1 2)))

; Effects exclude the fused region and occur exactly once, left to right, in
; the authored recursive fold.
!(list:sum-by prepared-sum:effect (1 3 2))
