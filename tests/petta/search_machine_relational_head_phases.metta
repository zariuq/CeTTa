; The complete structural head constrains a call before any relational LHS
; occurrence runs.  Output traces are observable even when the answer bag is
; empty.
!(progn (translatePredicate (set_prolog_flag occurs_check True)) true)

(= (phase-mark $label) (progn (println! $label) fixed-result))
(= (phase-repeat (phase-mark forbidden) $x $x) accepted)

!(println! ground-conflict)
!(collapse (phase-repeat fixed-result a b))
!(println! open-conflict)
!(collapse (phase-repeat fixed-result (rigid-f $a) (rigid-g $b)))
!(println! occurs-conflict)
!(collapse (phase-repeat fixed-result $x (rigid-f $x)))

; A cycle created by one field must fail before another field revisits it.
(= (phase-cycle $x $x $x) accepted)
!(println! intermediate-occurs-conflict)
!(collapse (phase-cycle (rigid-f $a) $a $a))
(= (phase-cycle-call (phase-mark forbidden-cycle) $x $x $x) accepted)
!(println! intermediate-occurs-prefix)
!(collapse (phase-cycle-call fixed-result (rigid-f $a) $a $a))
!(println! repeated-compatible)
!(collapse (phase-cycle (rigid-f a) (rigid-f a) (rigid-f a)))

; Shared source variables keep one identity across structural head matching
; and the relational prefix.
(= (phase-alias (phase-mark $x) $x) accepted)
!(println! bound-before-prefix)
!(collapse (phase-alias fixed-result payload))

; A callable below an open enclosing value becomes a fresh output hole.
(= (phase-nested (box (phase-mark nested))) accepted)
!(println! open-enclosing-value)
!(collapse (phase-nested $q))

; Relational occurrences keep their source order and multiplicity.
(= (phase-pair (phase-mark first) (phase-mark second)) accepted)
!(println! prefix-order)
!(collapse (phase-pair fixed-result fixed-result))
(= (phase-duplicates (phase-mark repeated) (phase-mark repeated)) accepted)
!(println! prefix-duplicates)
!(collapse (phase-duplicates fixed-result fixed-result))

; A logical-list constructor is part of the structural skeleton.  Its
; callable head element runs only after the list shape has matched.
(= (phase-cons (cons (phase-mark list-element) $tail)) $tail)
!(println! list-conflict)
!(collapse (phase-cons ()))
!(println! list-compatible)
!(collapse (phase-cons (fixed-result tail)))

; Output holes remain connected to an enclosing caller value.
(= (phase-reified (box (phase-mark reify) $x) $x) accepted)
!(println! hole-reification)
!(let $q $q (let $_ (phase-reified $q kept) $q))

; An expression-valued head is also part of the pattern traversal.
(= (phase-head ((phase-mark expression-head) $x) $x) accepted)
!(println! expression-head-value)
!(collapse (phase-head (fixed-result payload) payload))
!(println! expression-head-open)
!(collapse (phase-head $q payload))

; A failed prefix does not enter its remaining occurrence or its RHS.
(= (phase-zero) (progn (println! prefix-stopped) (let a b rejected)))
(= (phase-abort (phase-zero) (phase-mark unreachable)) accepted)
!(println! prefix-failure)
!(collapse (phase-abort $q fixed-result))
