; PeTTa's guard at a typed call evaluates get-type, which a program may
; extend.  An authored alternative that answers the declared type again
; continues the call once more, for an argument and for a result, and what
; the classifier does happens as it runs.
(: inc (-> Number Number))
(= (inc $x) (+ $x 1))
(= (get-type 3) Number)
(: f (-> Number Number))
(= (f $x) 7)
(= (get-type 7) Number)
(= (get-type 11) (let $ok (add-atom &self (classifier-ran yes)) Number))
(= (loop $n $acc) (if (== $n 0) $acc (loop (- $n 1) (inc $acc))))
!(inc 3)
!(f 5)
!(inc 11)
!(match &self (classifier-ran $x) $x)
!(inc 20)
!(loop 3 0)
