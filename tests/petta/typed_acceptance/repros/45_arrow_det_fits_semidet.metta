; Arrow effects are ordered by the number of answers a closure may produce.
; A deterministic closure is therefore admissible in a semideterministic
; parameter slot.

(: apply-semi (-[semidet]-> (-[semidet]-> Number Number) Number Number))
(= (apply-semi $fn $value) ($fn $value))

(: increment (-[det]-> Number Number))
(= (increment $value) (+ $value 1))

(: increment-as-semi (-> (-[semidet]-> Number Number)))
(= (increment-as-semi)
   (the (-[semidet]-> Number Number) increment))

(: produce-det (-> (-[det]-> Number Number)))
(= (produce-det) increment)

(: widen-det-result (-> (-[semidet]-> Number Number)))
(= (widen-det-result) (produce-det))

!(test (apply-semi increment 4) 5)
!(test (apply-semi (|-> ($value) (+ $value 2)) 4) 6)
