; Regression: failed speculative type matches must not leak bindings into the
; next candidate when checking dependent function applicability.

(: Pair (-> $a $b Type))
(: chooseSame (-> (Pair $t $t) $t))
(= (chooseSame $p) witness)

(: ambiguousPair (Pair Number String))
(: ambiguousPair (Pair Bool Bool))

!(assertEqual (get-type (chooseSame ambiguousPair)) Bool)
