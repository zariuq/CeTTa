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
; A literal field the pattern cannot meet prunes its sibling's calls.
(= (lit-field $n) (Pair 3 (lit-field (+ $n 1))))
(= (via-lit-field $n) (let (Pair 5 $x) (lit-field $n) $x))
!(collapse (via-lit-field 0))
; So does a field holding an argument's value, and a field after the call.
(= (arg-field $m $n) (Pair $m (arg-field $m (+ $n 1))))
(= (via-arg-field $n) (let (Pair 5 $x) (arg-field 3 $n) $x))
!(collapse (via-arg-field 0))
(= (spin $n) (spin (+ $n 1)))
(= (later-field $n) (let (Pair $x 5) (Pair (spin $n) 3) $x))
!(collapse (later-field 0))
; A let body's output meets the destination before the let's value runs.
(= (body-first $n) (let $z (body-first (+ $n 1)) (Pair 3 $z)))
(= (via-body-first $n) (let (Pair 5 $x) (body-first $n) $x))
!(collapse (via-body-first 0))
; A call in a field meets its part of the destination in its own head.
(= (inner-pair $n) (Pair (spin $n) 3))
(= (wrapped $n) (Wrap (inner-pair $n)))
(= (via-wrapped $n) (let (Wrap (Pair $x 5)) (wrapped $n) $x))
!(collapse (via-wrapped 0))
