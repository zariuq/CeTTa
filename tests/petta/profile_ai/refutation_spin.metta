; In ai a refuted pure call is not run, so the divergence of the call it
; would run is not observed: each row answers nothing at once.  The
; default profile runs that call, which does not terminate.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: natchk (-> Nat Number))
(= (natchk $n) 1)
(: spin (-> Number (List Number)))
(= (spin $n) (spin (+ $n 1)))
(: sspin (-> Number String))
(= (sspin $n) (sspin (+ $n 1)))
(: h (-> Number Number))
(= (h $n) (sspin $n))
!(natchk (spin 0))
!(h 0)
