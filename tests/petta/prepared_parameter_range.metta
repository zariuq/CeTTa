; A signature whose range is the type variable of one parameter, as
; (-> Number $t $t): the argument there is a value of some class, and the
; answer has to be of that class.
(: cnt (-> Number $t $t))
(= (cnt $n $acc) (if (== $n 0) $acc (cnt (- $n 1) (+ $acc 1))))
!(cnt 1000 0)
!(+ 1 (cnt 10 0))

; The answer is a Number where the argument is a String.
(: flip (-> Number $t $t))
(= (flip $n $x) (if (== $n 0) 7 (flip (- $n 1) $x)))
!(collapse (flip 2 "s"))
!(collapse (flip 2 5))

; The argument is a symbol declared Number, whose type comes from its
; declaration rather than its class.
(: seven Number)
!(collapse (flip 2 seven))

; A head that answers its argument as it is given.
(: same (-> Number $t $t))
(= (same $n $x) $x)
!(collapse (same 1 "s"))
!(collapse (same 1 seven))

; The variable at an earlier parameter takes that argument's class too.
(: lead (-> $t Number $t))
(= (lead $x $n) (if (== $n 0) $x (lead $x (- $n 1))))
!(collapse (lead "s" 3))
!(collapse (lead 4 2))
