(scope:profile lexical-fresh per-call reference)
!(bind! &late-collection (new-space))

(= (late:seed) warmed)
!(late:seed)

; Installing a rule after an authored call must retain its collection word.
(= (late:empty? $space)
  (if (== (collapse (match $space (row $x) $x)) ()) yes no))
(= (late:choose $space)
  (if (== (collapse (match $space (row $x) $x)) ())
    (let $done (add-atom $space (chosen empty)) empty)
    (let $done (add-atom $space (chosen nonempty)) nonempty)))

!(late:empty? &late-collection)
!(late:choose &late-collection)
!(add-atom &late-collection (row a))
!(add-atom &late-collection (row a))
!(late:empty? &late-collection)
!(late:choose &late-collection)
!(collapse (match &late-collection (row $x) $x))
!(collapse (match &late-collection (chosen $branch) $branch))
