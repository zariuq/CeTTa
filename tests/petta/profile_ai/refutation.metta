; A pure call whose declared result has another head path than the closed
; type that receives it answers nothing in both profiles; ai compiles it
; not to run.  It is received by a parameter (natchk) or, in a tail
; position, by its caller's range (h).  A result with the parameter's head
; path (chkS) or a parameter at a metatype (echk) is not refuted: the call
; runs, and its answer is checked as in the default profile.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: build (-> Number (List Number) (List Number)))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: natchk (-> Nat Number))
(= (natchk $n) 1)
(: chkS (-> (List String) Number))
(= (chkS $l) 2)
(: echk (-> Expression Number))
(= (echk $e) 4)
(: tostr (-> Number String))
(= (tostr $n) (if (== $n 0) "zero" (tostr (- $n 1))))
(: h (-> Number Number))
(= (h $n) (tostr $n))
!(natchk (build 3 Nil))
!(chkS (build 0 Nil))
!(chkS (build 2 Nil))
!(echk (build 1 Nil))
!(h 3)
!(tostr 2)
