; A nondeterministic parameter is an upper bound on result multiplicity, so a
; declared nondeterministic closure is compatible with that slot.

(: apply-many (-[nondet]-> (-[nondet]-> Number Number) Number Number))
(= (apply-many $fn $value) ($fn $value))

(: choose-neighbor (-[nondet]-> Number Number))
(= (choose-neighbor $value) (superpose ($value (+ $value 1))))

(: chooser-as-nondet (-> (-[nondet]-> Number Number)))
(= (chooser-as-nondet)
   (the (-[nondet]-> Number Number) choose-neighbor))

(: produce-nondet (-> (-[nondet]-> Number Number)))
(= (produce-nondet) choose-neighbor)

(: preserve-nondet-result (-> (-[nondet]-> Number Number)))
(= (preserve-nondet-result) (produce-nondet))

!(test (collapse (apply-many choose-neighbor 2)) (2 3))
!(test
   (collapse
      (apply-many
         (|-> ($value) (superpose ($value (+ $value 1))))
         2))
   (2 3))
