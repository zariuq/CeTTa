; A let's pattern is the destination of its value's call: PeTTa meets it with
; each equation's exposed output before that equation's body runs, so an
; output the pattern cannot meet makes none of its calls.
(= (h $n) (Right (h (+ $n 1))))
(= (outer $n) (let (Left $x) (h $n) $x))
!(collapse (outer 0))
(= (g2 $n) (h $n))
(= (via-g2 $n) (let (Left $x) (g2 $n) $x))
!(collapse (via-g2 0))
(= (nested $n) (let (Pair (Left $x) $y) (Pair (h $n) 1) $x))
!(collapse (nested 0))
; Under an if, in one branch.
(= (branchy $n) (if (> $n 0) (let (Left $x) (h $n) $x) none))
!(collapse (branchy 1))
!(branchy 0)
; Patterns that every exposed output meets compute their values as before.
(= (lookup $k Nil) Nothing)
(= (lookup $k (Cons (Pair $k2 $v) $rest))
   (if (== $k $k2) (Just $v) (lookup $k $rest)))
(= (find-or-zero $k $t) (let (Just $v) (lookup $k $t) $v))
!(find-or-zero b (Cons (Pair a 1) (Cons (Pair b 2) Nil)))
!(collapse (find-or-zero z (Cons (Pair a 1) Nil)))
(= (halves Nil) (pair Nil Nil))
(= (halves (Cons $x Nil)) (pair (Cons $x Nil) Nil))
(= (halves (Cons $x (Cons $y $rest)))
   (let (pair $l $r) (halves $rest) (pair (Cons $x $l) (Cons $y $r))))
!(halves (Cons 1 (Cons 2 (Cons 3 (Cons 4 (Cons 5 Nil))))))
; A pattern whose variable repeats.
(= (twin $n) (Pair $n $n))
(= (same $n) (let (Pair $a $a) (twin $n) $a))
!(same 4)
