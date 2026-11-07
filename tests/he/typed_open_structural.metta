; HE typed heads with open structural formals, as (List $a): each argument
; is checked by unifying one of its typings with its formal; an open range
; is shown by every return position or the call is left to the canonical
; evaluator.  hyperon-experimental answers alike up to variable names, and
; prints the first error's expected type unbound, (List $a).
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: build (-> Number (List $a) (List $a)))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))
(: sum (-> (List $a) Number))
(= (sum Nil) 0)
(= (sum (Cons $x $xs)) (+ $x (sum $xs)))
!(sum (build 20 Nil))
!(collapse (build 2 (Cons "s" Nil)))
!(collapse (sum (Cons "a" (Cons "b" Nil))))
(: two (-> (List $a) (List $a) Number))
(= (two $x $y) 1)
!(collapse (two (Cons 1 Nil) (Cons "s" Nil)))
!(two (Cons 1 Nil) (Cons 2 Nil))
(: wrapl (-> Number (List $a)))
(= (wrapl $n) (Cons $n Nil))
!(wrapl 3)
; An open range is bound per activation: a tail call into a head with the
; same declared range, or a constructor whose declared range is the range,
; does not show this activation's instance.
(: NList (List Number))
(: SList (List String))
(: range-step (-> Number (List $a) (List $a)))
(= (range-step $n $xs) (if (== $n 0) $xs (range-step 0 SList)))
!(range-step 0 NList)
!(collapse (range-step 1 NList))
(: rewrap (-> Number (List $a) (List $a)))
(= (rewrap $n $xs) (if (== $n 0) $xs (Cons "s" Nil)))
!(collapse (rewrap 1 NList))
!(rewrap 1 (Cons "t" Nil))
; A closed range met by an open callee's instance at a tail call.
(: pass (-> Number (List $a) (List $a)))
(= (pass $n $xs) $xs)
(: lnum (-> Number (List Number)))
(= (lnum $n) (pass $n (Cons "s" Nil)))
(: lnum2 (-> Number (List Number)))
(= (lnum2 $n) (pass $n (Cons 1 Nil)))
!(collapse (lnum 1))
!(lnum2 1)
; An instance left open by Nil is fixed by the next call's arguments.
(: mix (-> Number (List $a) (List $a)))
(= (mix $n $xs) (if (== $n 0) (Cons "s" $xs) (mix (- $n 1) (Cons $n $xs))))
!(mix 0 Nil)
!(collapse (mix 1 Nil))
