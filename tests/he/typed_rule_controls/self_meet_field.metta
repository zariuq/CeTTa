; A head's own tail call meets its instance by the call's checks where the
; argument at the formal the range reads is the parameter there, a checked
; build over it at a field its constructor types as its range, or such a
; field of it; any other argument is met at run time.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: SL (List String))
(: NL (List Number))
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: tl (-> Nat (List $a) (List $a)))
(= (tl Z $l) $l)
(= (tl (S $k) (Cons $x $xs)) (tl $k $xs))
(: utl (-> (List String) %Undefined%))
(= (utl $l) (tl (S Z) $l))
!(utl (Cons "a" (Cons "b" Nil)))
!(tl (S (S Z)) (Cons 1 (Cons 2 (Cons 3 Nil))))
!(tl (S Z) Nil)
