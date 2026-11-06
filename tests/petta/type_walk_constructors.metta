; A ground tree of constructors read at a closed type answers as the
; service does node by node: once when every node fits, never otherwise.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: lsum (-> (List Number) Number))
(= (lsum Nil) 0)
(= (lsum (Cons $x $xs)) (+ $x (lsum $xs)))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))
!(lsum (build 30 Nil))
; An element of another class: the list is no (List Number).
!(collapse (lsum (Cons 1 (Cons "two" Nil))))
; Nested lists.
(: lens (-> (List (List Number)) Number))
(= (lens Nil) 0)
(= (lens (Cons $l $ls)) (+ (lsum $l) (lens $ls)))
!(lens (Cons (Cons 1 (Cons 2 Nil)) (Cons (Cons 3 Nil) Nil)))
!(collapse (lens (Cons (Cons 1 Nil) (Cons (Cons "x" Nil) Nil))))
; Peano numbers.
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: toint (-> Nat Number))
(= (toint Z) 0)
(= (toint (S $n)) (+ 1 (toint $n)))
!(toint (S (S (S Z))))
; A constructor with two declarations keeps every answer.
(: Pair (-> Number Number (Two Number)))
(: Pair (-> Number Number (Two Atom)))
(: first2 (-> (Two Number) Number))
(= (first2 (Pair $a $b)) $a)
!(collapse (first2 (Pair 4 5)))
; A parameter the range does not fix leaves the walk.
(: Wrap (-> $b (Box Number)))
(: unbox (-> (Box Number) Number))
(= (unbox (Wrap $v)) 7)
!(collapse (unbox (Wrap "anything")))
; A literal annotated as a subject turns the walk off for the program.
(: 9 Odd)
!(collapse (lsum (Cons 9 Nil)))
