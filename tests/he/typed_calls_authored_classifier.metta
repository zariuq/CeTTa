; HE types a call by the types declared for its arguments and the
; grounded types of values; an equation that extends get-type is not
; consulted there, so each typed call answers once and the classifier
; does not run.
(: inc (-> Number Number))
(= (inc $x) (+ $x 1))
(= (get-type 3) Number)
(: f (-> Number Number))
(= (f $x) 7)
(= (get-type 7) Number)
(= (get-type 11) (let $ok (add-atom &self (classifier-ran yes)) Number))
!(inc 3)
!(f 5)
!(inc 11)
!(collapse (match &self (classifier-ran $x) $x))
!(get-type 3)
