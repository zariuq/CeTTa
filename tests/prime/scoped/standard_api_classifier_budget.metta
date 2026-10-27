; Run with finite evaluator fuel. An unfinished classifier must not turn
; a missing type answer into a completed argument-type rejection.
(= (get-type $x) (get-type $x))
(: needs-missing-type (-> MissingType MissingType))
(= (needs-missing-type $x) $x)
!(try (needs-missing-type 2))
