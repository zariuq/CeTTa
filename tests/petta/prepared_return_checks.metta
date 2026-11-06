; A typed head whose body the machine cannot show to be of its range's
; class has its value checked when the call returns.  A value outside the
; class answers nothing, as the protocol's result check does, and a tail
; call keeps the check its caller owes.
(: cnt (-> Number %Undefined% Number))
(= (cnt $n $acc) (if (== $n 0) $acc (cnt (- $n 1) (+ $acc 1))))
!(cnt 1000 0)
!(+ 1 (cnt 10 0))

; The accumulator is not a Number when it reaches the answer.
(: keep (-> Number %Undefined% Number))
(= (keep $n $acc) (if (== $n 0) $acc (keep (- $n 1) $acc)))
!(collapse (keep 3 "s"))
!(collapse (keep 3 7))

; A tail call into a head whose range is another class: the caller's
; Number check still holds the String it answers.
(: label (-> Number String))
(= (label $x) "done")
(: hand (-> Number %Undefined% Number))
(= (hand $n $acc) (if (== $n 0) (label $acc) (hand (- $n 1) $acc)))
!(collapse (hand 4 0))

; A tail call into an untyped head answers through the caller's check.
(= (plain $x) $x)
(: via (-> Number %Undefined% Number))
(= (via $n $acc) (if (== $n 0) (plain $acc) (via (- $n 1) $acc)))
!(collapse (via 2 5))
!(collapse (via 2 "t"))
