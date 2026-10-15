; A stored value that reaches a declared Number argument at run time and is
; not a Number is a type mismatch.
(stored "text")
(: double (-> Number Number))
(= (double $n) (* 2 $n))
(: from-store (-> Number))
(= (from-store) (double (match &self (stored $v) $v)))

!(from-store)
