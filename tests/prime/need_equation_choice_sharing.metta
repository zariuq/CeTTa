; One source suspension is shared across strict and wildcard equation
; candidates.  Its effect occurs once even though the demanded choice has two
; outcomes and the wildcard result later observes the same suspension.
!(bind! &prime-need:choice-state (new-state 0))
(= (prime-need:choice-source)
   (let $old (get-state &prime-need:choice-state)
     (let $_ (change-state! &prime-need:choice-state (+ $old 1))
       (superpose (A B)))))
(= (prime-need:choice A) (left A))
(= (prime-need:choice B) (right B))
(= (prime-need:choice $x) (wild $x))
!(prime-need:choice (prime-need:choice-source))
!(get-state &prime-need:choice-state)
