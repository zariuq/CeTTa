; Differential witness for type declaration deduplication.
; Keep declaration multiplicity distinct from typed-call alternatives.

(: dedup-concrete (-> Number Atom))
(: dedup-concrete (-> Number Atom))
(= (dedup-concrete $x) $x)
!(collapse (dedup-concrete 7))

(: dedup-alpha (-> $a $a))
(: dedup-alpha (-> $b $b))
(= (dedup-alpha $x) $x)
!(collapse (dedup-alpha 7))
!(collapse (match &self (: dedup-alpha $type) DECL))

; These arrows are not variants: one repeats a type variable, the other
; admits independently typed arguments. Both must remain in the family.
(: dedup-sharing (-> $a $a Atom))
(: dedup-sharing (-> $b $c Atom))
(= (dedup-sharing $x $y) OK)
!(collapse (dedup-sharing 7 "seven"))
!(collapse (dedup-sharing 7 8))

(: dedup-roles (-> Number Atom))
(: dedup-roles (-> Atom Atom))
(= (dedup-roles $x) $x)
!(collapse (dedup-roles (+ 2 4)))
