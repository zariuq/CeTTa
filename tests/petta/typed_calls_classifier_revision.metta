; A get-type equation added after a typed call was prepared gives its value a
; second type answer from then on, and removing it takes the answer away.
(: inc (-> Number Number))
(= (inc $x) (+ $x 1))
(= (loop $n $acc) (if (== $n 0) $acc (loop (- $n 1) (inc $acc))))
!(inc 3)
!(loop 3 0)
!(add-atom &self (= (get-type 3) Number))
!(inc 3)
!(loop 3 0)
!(remove-atom &self (= (get-type 3) Number))
!(inc 3)
