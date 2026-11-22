; A semideterministic closure fits a semideterministic parameter exactly.

(: apply-semi (-[semidet]-> (-[semidet]-> Number Number) Number Number))
(= (apply-semi $fn $value) ($fn $value))

(: successor-of-zero (-[semidet]-> Number Number))
(= (successor-of-zero 0) 1)

(: successor-as-semi (-> (-[semidet]-> Number Number)))
(= (successor-as-semi)
   (the (-[semidet]-> Number Number) successor-of-zero))

(: produce-semi (-> (-[semidet]-> Number Number)))
(= (produce-semi) successor-of-zero)

(: preserve-semi-result (-> (-[semidet]-> Number Number)))
(= (preserve-semi-result) (produce-semi))

!(test (apply-semi successor-of-zero 0) 1)
!(test
   (apply-semi
      (|-> ($value)
         (if (== $value 0) (empty) (+ $value 1)))
      4)
   5)
