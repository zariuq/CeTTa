; Outside the licence: two is no function and offers Nat.  The fold keeps
; its cell test; (toint two) answers nothing, as its equations do.
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: two Nat)
(: fromint (-> Number Nat))
(= (fromint $n) (if (== $n 0) Z (S (fromint (- $n 1)))))
(: toint (-> Nat Number))
(= (toint Z) 0)
(= (toint (S $k)) (+ 1 (toint $k)))
!(toint (fromint 100))
!(toint two)
