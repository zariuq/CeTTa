; Positive control for contextual closure checking: a list promises
; deterministic unary closures and the inline lambda has exactly one result.

(: committed-closures-ok
   (-> (List (-[det]-> Number Number))))

(= (committed-closures-ok)
   (cons (|-> ($value) (+ $value 1)) ()))
