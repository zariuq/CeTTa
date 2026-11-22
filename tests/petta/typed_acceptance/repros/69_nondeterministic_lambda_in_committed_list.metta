; A list promises deterministic unary closures, but its sole element is an
; inline lambda whose body has two results.  This reaches contextual closure
; checking through the list former rather than through a direct call slot.

(: committed-closures
   (-> (List (-[det]-> Number Number))))

(= (committed-closures)
   (cons (|-> ($value) (superpose ($value (+ $value 1)))) ()))
