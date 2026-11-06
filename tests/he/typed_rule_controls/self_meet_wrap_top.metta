; A head's own tail call meets its instance by the call's checks where the
; argument at the formal the range reads is the parameter there, a checked
; build over it at a field its constructor types as its range, or such a
; field of it; any other argument is met at run time.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: SL (List String))
(: NL (List Number))
(: Wrap (-> (List $b) (List Number)))
(: grow (-> Number (List $a) (List $a)))
(= (grow $n $acc) (if (== $n 0) $acc (grow (- $n 1) (Wrap $acc))))
(: ug (-> (List String) %Undefined%))
(= (ug $l) (grow 1 $l))
!(grow 1 SL)
