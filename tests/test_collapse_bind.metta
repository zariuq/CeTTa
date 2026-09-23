; One collection contains every result with its saved environment. The
; printer exposes a structural view of the otherwise opaque grounded value.
(= (multi) a)
(= (multi) b)
!(collapse-bind (multi))

; Empty branches contribute no occurrences to the saved answer bag.
!(collapse-bind (let $x (superpose (a b)) (if (== $x a) (empty) $x)))

; Independent snapshots of the same environment compare as values.
!(assertEqual (collapse-bind 1) (collapse-bind 1))
