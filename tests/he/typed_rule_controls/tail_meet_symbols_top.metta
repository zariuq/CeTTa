; A tail call shows an open range only through its meet with the
; activation's instance.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: SL (List String))
(: NL (List Number))
(: g (-> (List $u) (List Number)))
(= (g $l) NL)
(: f (-> (List $t) (List $t)))
(= (f $l) (g $l))
(: u (-> (List String) %Undefined%))
(= (u $l) (f $l))
!(f SL)
