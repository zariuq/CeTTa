; A get-type rule added while a body runs gives the literal 0 a second
; answer at Number: the later calls' guards see it.
(: + (-> Number Number Number))
(= (g $x) (+ (let $_ (add-atom &self (= (get-type 0) Number)) $x) 0))
!(collapse (g 5))
!(collapse (g 6))
