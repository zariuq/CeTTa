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
(: Wrap2 (-> (List String) (List Number)))
(: tw (-> Nat (List $a) (List $a)))
(= (tw Z $l) $l)
(= (tw (S $k) (Wrap2 $xs)) (tw $k $xs))
(: utw (-> (List Number) %Undefined%))
(= (utw $l) (tw (S Z) $l))
!(tw Z (Wrap2 SL))
!(utw (Wrap2 SL))
