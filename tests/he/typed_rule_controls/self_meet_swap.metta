; A head's own tail call meets its instance by the call's checks where the
; argument at the formal the range reads is the parameter there, a checked
; build over it at a field its constructor types as its range, or such a
; field of it; any other argument is met at run time.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: SL (List String))
(: NL (List Number))
(: sw (-> Number (List $a) (List $b) (List $a)))
(= (sw $n $x $y) (if (== $n 0) $x (sw (- $n 1) $y $x)))
(: usw (-> (List String) %Undefined%))
(= (usw $l) (sw 1 $l NL))
!(sw 2 SL NL)
!(usw SL)
