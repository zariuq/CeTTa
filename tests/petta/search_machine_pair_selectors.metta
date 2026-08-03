!(first-from-pair (a b))
!(second-from-pair (a b))
!(first (c d))

; The selectors retain their relational direction: constraining the selected
; element constructs the corresponding open pair.
!(let (first-from-pair $pair) a $pair)

; A malformed one-element input has no relational answer.
!(first-from-pair (a))
!(first (a))
